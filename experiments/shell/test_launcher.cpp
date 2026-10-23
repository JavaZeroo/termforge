// Native desktop binding for the Python-owned shell integration tests.
#include "../conpty/session.hpp"
#include "hosted_ime_input.hpp"
#include <filesystem>
#include <iostream>

using tf_conpty::Handle;
using tf_conpty::fail;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::wstring object_name(HANDLE object) {
    wchar_t name[256]{}; DWORD needed = 0;
    if (!GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &needed))
        fail("test launcher station identity");
    return name;
}
static std::wstring quote_argument(std::wstring_view value) {
    // CommandLineToArgv/CRT quoting, including quotes and trailing backslashes.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(character == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += character; slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result += L'\"';
    return result;
}
static void read_gate() {
    // Do not buffer unbounded input or consume any user-interactive input.
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    char gate[4]{}; size_t count = 0;
    while (count != sizeof(gate)) {
        DWORD received = 0;
        if (!ReadFile(input, gate + count, 1, &received, nullptr) || received != 1)
            throw std::runtime_error("test launcher supervisor gate closed");
        if (gate[count++] == '\n') break;
    }
    require(std::string_view(gate, count) == "GO\n" || std::string_view(gate, count) == "GO\r\n",
            "test launcher supervisor gate must be GO");
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned)) fail("test launcher outer Job query");
    require(owned != FALSE, "test launcher requires an outer supervisor Job");
}
static std::wstring validate_desktop(std::wstring value) {
    const auto prefix = object_name(GetProcessWindowStation()) + L"\\TermForgeShell-";
    require(value.size() > prefix.size() && value.size() < 256 && value.compare(0, prefix.size(), prefix) == 0,
            "test launcher requires the current station and a TermForgeShell-owned desktop");
    for (size_t i = prefix.size(); i < value.size(); ++i) {
        const wchar_t c = value[i];
        require((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'-' || c == L'_',
                "test launcher desktop suffix is invalid");
    }
    return value;
}
static void duplicate_output(DWORD id, Handle& destination) {
    const HANDLE source = GetStdHandle(id);
    require(source && source != INVALID_HANDLE_VALUE, "test launcher output is unavailable");
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), destination.put(),
                         0, TRUE, DUPLICATE_SAME_ACCESS)) fail("test launcher output duplication");
}
static DWORD active_processes(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
        fail("test launcher Job accounting");
    return accounting.ActiveProcesses;
}
static bool drain(HANDLE job, HANDLE process, unsigned timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if ((!process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0) &&
            (!job || active_processes(job) == 0)) return true;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    return (!process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0) &&
           (!job || active_processes(job) == 0);
}
static void hosted_summary(HANDLE job,HANDLE process,const std::wstring& mode,DWORD code,bool timed_out) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)) fail("hosted IME inner Job summary");
    std::cout<<"{\"schema\":\"termforge-hosted-ime-launcher-v1\",\"mode\":\""<<tf_shell_test::ime_utf8(mode)
        <<"\",\"child_exit_code\":"<<code<<",\"child_signaled\":"<<(WaitForSingleObject(process,0)==WAIT_OBJECT_0?"true":"false")
        <<",\"timed_out\":"<<(timed_out?"true":"false")<<",\"inner_job_after\":{\"active\":"<<info.ActiveProcesses
        <<",\"total\":"<<info.TotalProcesses<<",\"terminated\":"<<info.TotalTerminatedProcesses<<"}}\n"<<std::flush;
}
int wmain(int argc, wchar_t** argv) {
    Handle job, process;
    try {
        require(argc == 4, "usage: shell_test_launcher station\\TermForgeShell-name mode frame.bmp");
        read_gate();
        const bool hosted=std::wstring_view(argv[1])==L"--hosted-ime";
        std::wstring desktop;
        if(hosted) {
            const auto input=tf_shell_test::require_hosted_ime_input();
            require(tf_shell_test::is_product_ime_mode(argv[2]),"invalid explicit hosted IME case");
            desktop=tf_shell_test::ime_object_name(GetProcessWindowStation())+L"\\"+input;
        } else desktop=validate_desktop(argv[1]);
        require(*argv[2] && *argv[3], "test launcher mode and frame path are required");
        std::wstring executable(32768, 0);
        const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!count || count == executable.size()) fail("test launcher executable path");
        executable.resize(count);
        executable = (std::filesystem::path(executable).parent_path() / L"termforge_shell.exe").wstring();
        auto command = quote_argument(executable) + (hosted?L" --test-owned-hosted-ime ":L" --test-owned ") + quote_argument(argv[2]) + L" " + quote_argument(argv[3]);
        require(command.size() < 32767, "test launcher command is too long");

        Handle output, error, null,child_input,child_gate;
        duplicate_output(STD_OUTPUT_HANDLE, output); duplicate_output(STD_ERROR_HANDLE, error);
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        null.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (null.get() == INVALID_HANDLE_VALUE) fail("test launcher null input");
        if(hosted) {
            if(!CreatePipe(child_input.put(),child_gate.put(),&security,0)) fail("hosted IME inner GO pipe");
            if(!SetHandleInformation(child_gate.get(),HANDLE_FLAG_INHERIT,0)) fail("hosted IME private GO writer");
        }
        job.reset(CreateJobObjectW(nullptr, nullptr));
        if (!job.get()) fail("test launcher Job creation");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            fail("test launcher Job limits");

        SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<unsigned char> storage(size);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) fail("test launcher attributes");
        struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
        const HANDLE inherited[] = {output.get(), error.get(), hosted?child_input.get():null.get()};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE*>(inherited), sizeof(inherited), nullptr, nullptr)) fail("test launcher handle allowlist");
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        startup.StartupInfo.lpDesktop = desktop.data(); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = inherited[2]; startup.StartupInfo.hStdOutput = output.get(); startup.StartupInfo.hStdError = error.get();
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                &startup.StartupInfo, &info)) fail("test launcher child creation");
        process.reset(info.hProcess); Handle thread(info.hThread);
        if (!AssignProcessToJobObject(job.get(), process.get())) fail("test launcher child Job assignment");
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) fail("test launcher child resume");
        if(hosted) {
            DWORD written=0;
            if(!WriteFile(child_gate.get(),"GO\n",3,&written,nullptr) || written!=3) fail("hosted IME inner Job GO release");
            child_gate.reset(); child_input.reset();
        }
        // Only the child owns these inherited writer handles after startup.
        output.reset(); error.reset(); null.reset();

        const DWORD limit=hosted?50000:std::wstring_view(argv[2])==L"ui-workflow-sessions"?80000:45000;
        const DWORD waited = WaitForSingleObject(process.get(), limit);
        if (waited == WAIT_TIMEOUT) {
            if (!TerminateJobObject(job.get(), 124)) fail("test launcher timeout termination");
            require(drain(job.get(), process.get(), 3000), "test launcher timed-out Job did not drain");
            if(hosted) hosted_summary(job.get(),process.get(),argv[2],124,true);
            std::cerr << "test launcher child exceeded "<<limit/1000<<" seconds\n"; return 124;
        }
        if (waited != WAIT_OBJECT_0) fail("test launcher child wait");
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process.get(), &exit_code)) fail("test launcher child exit code");
        require(drain(job.get(), process.get(), 3000), "test launcher child left live Job descendants");
        if(hosted) hosted_summary(job.get(),process.get(),argv[2],exit_code,false);
        return static_cast<int>(exit_code);
    } catch (const std::exception& error) {
        // Covers assignment failure too: a suspended child may not be in Job.
        if (process.get()) TerminateProcess(process.get(), 125);
        if (job.get()) TerminateJobObject(job.get(), 125);
        try {
            if (!drain(job.get(), process.get(), 3000)) std::cerr << "test launcher cleanup deadline exceeded\n";
        } catch (const std::exception& cleanup) { std::cerr << cleanup.what() << '\n'; }
        std::cerr << error.what() << '\n'; return 125;
    }
}
