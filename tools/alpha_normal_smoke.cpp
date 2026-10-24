// Real no-argument package entry on a supervisor-owned non-input desktop.
// This probe never switches desktops, reads the clipboard, or sends physical input.
#include "../experiments/conpty/session.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using tf_conpty::Handle;
using tf_conpty::fail;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::wstring object_name(HANDLE object) {
    wchar_t value[256]{}; DWORD needed = 0;
    if (!GetUserObjectInformationW(object, UOI_NAME, value, sizeof(value), &needed)) fail("desktop identity");
    return value;
}
static void read_gate() {
    char value[4]{}; size_t count = 0;
    while (count < sizeof(value)) {
        DWORD received = 0;
        require(ReadFile(GetStdHandle(STD_INPUT_HANDLE), value + count, 1, &received, nullptr) && received == 1,
                "supervisor gate closed");
        if (value[count++] == '\n') break;
    }
    require(std::string_view(value, count) == "GO\n" || std::string_view(value, count) == "GO\r\n", "supervisor gate must be GO");
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned)) fail("outer Job query");
    require(owned != FALSE, "outer supervisor Job required");
}
static JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &value, sizeof(value), nullptr)) fail("inner Job accounting");
    return value;
}
static bool drain(HANDLE job, HANDLE process, unsigned limit = 3000) {
    const auto deadline = GetTickCount64() + limit;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && accounting(job).ActiveProcesses == 0) return true;
        Sleep(10);
    } while (GetTickCount64() < deadline);
    return WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && accounting(job).ActiveProcesses == 0;
}
struct Windows {
    DWORD pid{};
    HWND workspace{}, terminal{};
};
static bool own_class(HWND window, DWORD pid, const wchar_t* expected) {
    DWORD actual = 0; GetWindowThreadProcessId(window, &actual);
    if (actual != pid) return false;
    wchar_t name[256]{};
    return GetClassNameW(window, name, 256) && std::wstring_view(name) == expected;
}
static BOOL CALLBACK child_window(HWND window, LPARAM data) {
    auto& found = *reinterpret_cast<Windows*>(data);
    if (own_class(window, found.pid, L"TermForgeShell") && IsWindowVisible(window)) found.terminal = window;
    return TRUE;
}
static BOOL CALLBACK top_window(HWND window, LPARAM data) {
    auto& found = *reinterpret_cast<Windows*>(data);
    if (own_class(window, found.pid, L"TermForgeWorkspace") && IsWindowVisible(window)) {
        found.workspace = window;
        EnumChildWindows(window, child_window, data);
    }
    return TRUE;
}
static bool real_cmd(HANDLE job) {
    struct List { DWORD assigned, count; ULONG_PTR ids[32]; } list{};
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, &list, sizeof(list), nullptr)) fail("inner Job process list");
    require(list.assigned <= 32 && list.count <= 32, "unexpected package process count");
    wchar_t system[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    require(length && length < MAX_PATH, "system directory query failed");
    const auto expected = (std::filesystem::path(system) / L"cmd.exe").wstring();
    for (DWORD index = 0; index < list.count; ++index) {
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(list.ids[index])));
        if (!process.get()) continue; // A short-lived descendant may already have exited.
        wchar_t path[32768]{}; DWORD count = 32768;
        if (QueryFullProcessImageNameW(process.get(), 0, path, &count) && _wcsicmp(path, expected.c_str()) == 0) return true;
    }
    return false;
}
static void message(HWND window, UINT type, WPARAM value) {
    DWORD_PTR result = 0;
    require(SendMessageTimeoutW(window, type, value, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) != 0,
            "owned window message failed");
}
static RECT client(HWND window) {
    RECT result{};
    require(GetClientRect(window, &result) && result.right > 0 && result.bottom > 0, "owned client rectangle unavailable");
    return result;
}
int wmain(int argc, wchar_t** argv) {
    Handle job, process;
    HDESK desktop = nullptr;
    try {
        require(argc == 4, "usage: alpha_normal_smoke station\\TermForgeShell-name bundle-exe owned-state");
        read_gate();
        std::wstring full_desktop = argv[1];
        const auto prefix = object_name(GetProcessWindowStation()) + L"\\TermForgeShell-";
        require(full_desktop.size() > prefix.size() && full_desktop.size() < 256 && full_desktop.compare(0, prefix.size(), prefix) == 0,
                "only the supervisor-owned desktop is permitted");
        for (size_t index = prefix.size(); index < full_desktop.size(); ++index) {
            const auto c = full_desktop[index];
            require((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_',
                    "invalid owned desktop suffix");
        }
        const auto name = full_desktop.substr(full_desktop.find(L'\\') + 1);
        desktop = OpenDesktopW(name.c_str(), 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS);
        require(desktop != nullptr, "owned desktop could not be opened");
        require(object_name(desktop) == name, "opened desktop identity mismatch");
        const auto executable = std::filesystem::canonical(argv[2]);
        require(executable.filename() == L"termforge_shell.exe", "package entry name mismatch");
        const auto state = std::filesystem::canonical(argv[3]);
        require(state.filename() == L"state", "private state directory required");
        wchar_t local[32768]{};
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        require(count && count < 32768 && std::filesystem::canonical(local) == state, "private LOCALAPPDATA identity mismatch");
        const auto work = state.parent_path();
        const auto result_file = work / L"alpha-command.txt";
        require(!std::filesystem::exists(result_file), "command result must be fresh");
        job.reset(CreateJobObjectW(nullptr, nullptr));
        require(job.get() != nullptr, "inner Job creation failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) fail("inner Job limits");
        auto command = L"\"" + executable.wstring() + L"\""; // Deliberately no product arguments.
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = full_desktop.data();
        PROCESS_INFORMATION created{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, work.c_str(), &startup, &created)) fail("normal package creation");
        process.reset(created.hProcess); Handle thread(created.hThread);
        if (!AssignProcessToJobObject(job.get(), process.get())) fail("normal package inner Job assignment");
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) fail("normal package resume");
        Windows found{created.dwProcessId};
        const auto ready_deadline = GetTickCount64() + 15000;
        bool ready = false;
        do {
            require(WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT, "normal package exited before readiness");
            found.workspace = found.terminal = nullptr;
            SetLastError(ERROR_SUCCESS);
            if (!EnumDesktopWindows(desktop, top_window, reinterpret_cast<LPARAM>(&found))) {
                const auto error = GetLastError();
                // A newly created, still-empty desktop can return FALSE with
                // no extended error. It is not readiness: keep the deadline
                // and require both real, PID-bound windows below.
                if (error != ERROR_SUCCESS) fail("owned desktop enumeration", error);
            }
            if (found.workspace && found.terminal && accounting(job.get()).ActiveProcesses >= 2 && real_cmd(job.get())) { ready = true; break; }
            Sleep(20);
        } while (GetTickCount64() < ready_deadline);
        require(ready, "normal package workspace, terminal or real cmd was not ready");
        const auto workspace_before = client(found.workspace), terminal_before = client(found.terminal);
        const auto active_ready = accounting(job.get()).ActiveProcesses;
        // Native messages target only this owned child; this is not physical-key latency evidence.
        for (const auto c : std::wstring_view(L"echo TF_ALPHA_PACKAGE_NORMAL>alpha-command.txt\r")) message(found.terminal, WM_CHAR, c);
        bool command_verified = false;
        const auto command_deadline = GetTickCount64() + 10000;
        do {
            std::ifstream input(result_file, std::ios::binary);
            if (input) {
                const std::string bytes((std::istreambuf_iterator<char>(input)), {});
                command_verified = bytes == "TF_ALPHA_PACKAGE_NORMAL\r\n";
                if (command_verified) break;
            }
            require(WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT, "normal package exited during command");
            Sleep(20);
        } while (GetTickCount64() < command_deadline);
        require(command_verified, "real cmd output differs from independent literal");
        RECT outer{}; require(GetWindowRect(found.workspace, &outer) != FALSE, "workspace outer rectangle unavailable");
        require(SetWindowPos(found.workspace, nullptr, 0, 0, outer.right - outer.left + 100, outer.bottom - outer.top + 80,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "owned normal resize failed");
        const auto resize_deadline = GetTickCount64() + 3000;
        bool resized = false;
        RECT workspace_after{};
        do {
            workspace_after = client(found.workspace);
            resized = workspace_after.right != workspace_before.right && workspace_after.bottom != workspace_before.bottom;
            if (resized) break;
            Sleep(20);
        } while (GetTickCount64() < resize_deadline);
        require(resized, "normal workspace did not change client size");
        message(found.workspace, WM_CLOSE, 0);
        require(WaitForSingleObject(process.get(), 20000) == WAIT_OBJECT_0, "normal package close deadline exceeded");
        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(process.get(), &code)) fail("normal package exit code");
        require(code == 0, "normal package did not exit cleanly");
        require(drain(job.get(), process.get()), "normal package left inner Job descendants");
        const auto final = accounting(job.get());
        require(CloseDesktop(desktop) != FALSE, "owned desktop close failed"); desktop = nullptr;
        std::cout << "{\"schema\":\"termforge-alpha-normal-smoke-v1\",\"no_arguments\":true,\"private_state\":true,"
            "\"workspace_visible\":true,\"terminal_visible\":true,\"real_system_cmd\":true,\"command_verified\":true,"
            "\"workspace_client_before\":[" << workspace_before.right << ',' << workspace_before.bottom << "],"
            "\"terminal_client_before\":[" << terminal_before.right << ',' << terminal_before.bottom << "],"
            "\"workspace_client_after\":[" << workspace_after.right << ',' << workspace_after.bottom << "],"
            "\"resize_verified\":true,\"active_at_ready\":" << active_ready << ",\"gui_exit_code\":" << code
            << ",\"inner_job_after\":{\"active\":" << final.ActiveProcesses << ",\"total\":" << final.TotalProcesses
            << ",\"terminated\":" << final.TotalTerminatedProcesses << "},\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        if (process.get()) TerminateProcess(process.get(), 125);
        if (job.get()) TerminateJobObject(job.get(), 125);
        try { if (process.get() && job.get() && !drain(job.get(), process.get())) std::cerr << "normal cleanup deadline exceeded\n"; }
        catch (const std::exception& cleanup) { std::cerr << cleanup.what() << '\n'; }
        if (desktop) CloseDesktop(desktop);
        std::cerr << error.what() << '\n';
        return 125;
    }
}
