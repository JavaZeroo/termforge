// Real console title changes through the owned candidate ShellWindow transport.
#define WinMain included_shell_entry
#include "shell.cpp"
#undef WinMain
#include "ime_desktop_process.hpp"

namespace {
std::filesystem::path executable_path() {
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(nullptr, path, 32768);
    require(size && size < 32768, "title fixture executable path unavailable");
    return std::filesystem::path(std::wstring(path, size));
}

void require_owned_job() {
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned,
            "title fixture requires an owned Job");
}

int console_title_client(const std::string& event_name) {
    require_owned_job();
    constexpr std::string_view prefix = "Local\\TermForgeTitle-";
    require(event_name.size() > prefix.size() && event_name.size() <= 96 && event_name.rfind(prefix, 0) == 0,
            "title release event identity invalid");
    for (size_t index = prefix.size(); index < event_name.size(); ++index)
        require((event_name[index] >= '0' && event_name[index] <= '9') || event_name[index] == '-',
                "title release event suffix invalid");
    tf_conpty::Handle release(OpenEventW(SYNCHRONIZE, FALSE, utf16(event_name).c_str()));
    require(release.get() != nullptr, "title release event missing");
    DWORD mode = 0;
    require(GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode),
            "title fixture requires its own real console");
    require(SetConsoleTitleW(L"TF_TITLE_NONEMPTY"), "nonempty console title failed");
    require(SetConsoleTitleW(L""), "empty console title failed");
    constexpr char marker[] = "\r\nTF_TITLE_EMPTY_DONE\r\n";
    DWORD written = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), marker, sizeof(marker)-1, &written, nullptr) &&
            written == sizeof(marker)-1, "title fixture marker write failed");
    require(WaitForSingleObject(release.get(), 5000) == WAIT_OBJECT_0, "title fixture release deadline");
    return 0;
}

bool empty_title_sequence(const std::string& bytes) {
    return bytes.find("\x1b]0;\a") != std::string::npos ||
           bytes.find("\x1b]2;\a") != std::string::npos ||
           bytes.find("\x1b]0;\x1b\\") != std::string::npos ||
           bytes.find("\x1b]2;\x1b\\") != std::string::npos;
}

template<class Predicate>
void title_wait(ShellWindow& window, Predicate predicate, const char* reason) {
    const auto deadline = GetTickCount64() + 5000;
    while (!predicate()) {
        require(GetTickCount64() < deadline, reason);
        MSG message{};
        for (int count = 0; count < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            if (!window.route_find_message(message) && !window.route_paste_message(message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
}

int title_window_case() {
    require_owned_job();
    require(tf_shell::is_ime_desktop_child(), "title fixture requires its owned non-input desktop");
    require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
            "title fixture DPI setup failed");
    wchar_t configured[32768]{};
    const auto size = GetEnvironmentVariableW(L"TERMFORGE_TEST_ARTIFACTS", configured, 32768);
    require(size < 32768, "title artifact root exceeds bound");
    const auto base = size ? std::filesystem::path(std::wstring(configured, size)) :
                             std::filesystem::current_path() / L"test-artifacts";
    const auto private_root = base / L"candidate-title" / L"_private";
    std::filesystem::create_directories(private_root);
    const auto out = private_root / (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(out), "title fixture evidence must be fresh");
    const auto event_name = L"Local\\TermForgeTitle-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    tf_conpty::Handle release(CreateEventW(nullptr, TRUE, FALSE, event_name.c_str()));
    const auto event_error = GetLastError();
    require(release.get() && event_error != ERROR_ALREADY_EXISTS, "title release event must be new");
    ShellWindow window("", "");
    window.create(); ShowWindow(window.handle(), SW_SHOWNOACTIVATE);
    title_wait(window, [&] { return window.ready() && window.settled(); }, "title fixture shell ready deadline");
    window.begin_output_observation();
    size_t saved = SIZE_MAX;
    const auto save_prefix = [&] {
        const auto& bytes = window.observed_output();
        if (saved == bytes.size()) return;
        std::ofstream file(out / L"transport.bin", std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close(); require(bool(file), "title fixture prefix write failed"); saved = bytes.size();
    };
    try {
        window.command(L"\"" + executable_path().wstring() + L"\" --title-client \"" + event_name + L"\"\r");
        title_wait(window, [&] {
            save_prefix();
            return !IsWindow(window.handle()) ||
                (empty_title_sequence(window.observed_output()) && window.live_contains_line("TF_TITLE_EMPTY_DONE"));
        }, "title fixture actual empty OSC and marker deadline");
        save_prefix();
        const bool empty = empty_title_sequence(window.observed_output());
        const bool marker = window.live_contains_line("TF_TITLE_EMPTY_DONE");
        const bool alive = IsWindow(window.handle()) != FALSE;
        require(SetEvent(release.get()), "title fixture release signal failed");
        std::cout << "{\"title_case\":\"real-console-empty\",\"empty_title_osc_observed\":" << (empty?"true":"false")
                  << ",\"after_marker_observed\":" << (marker?"true":"false")
                  << ",\"window_alive_after_empty\":" << (alive?"true":"false") << "}\n";
        window.close();
        title_wait(window, [&] { return !IsWindow(window.handle()); }, "title fixture close deadline");
        const auto result = window.collect();
        require(empty && marker && alive && result.complete && result.active_children == 0,
                "real empty title must preserve candidate UI and owned cleanup");
        std::cout << "{\"title_case_cleanup\":true,\"session_job_active_after\":0}\n";
        return 0;
    } catch (...) {
        save_prefix();
        window.close();
        throw;
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--title-client") return console_title_client(argv[2]);
        if (argc == 2 && std::string(argv[1]) == "--owned-desktop-child") {
            return title_window_case();
        }
        require(argc == 1, "unexpected title fixture arguments");
        for (int round = 0; round < 2; ++round)
            require(tf_shell::run_ime_desktop_child(15000) == 0, "owned candidate title case failed");
        std::cout << "{\"candidate_empty_title\":\"passed\",\"owned_job_zero\":true,\"desktop_deleted\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
