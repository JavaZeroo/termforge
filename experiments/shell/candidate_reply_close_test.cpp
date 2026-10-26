// Exercise the real receive -> parser -> reply route while closing the input
// side. The existing owned desktop/Job supervises the window and console child.
#define WinMain included_shell_entry
#include "shell.cpp"
#undef WinMain
#include "ime_desktop_process.hpp"

namespace {
template<class Predicate>
void reply_wait(Predicate predicate, const char* detail) {
    const auto deadline = GetTickCount64() + 3000;
    while (!predicate()) {
        require(GetTickCount64() < deadline, detail);
        Sleep(1);
    }
}

int reply_client(const wchar_t* name) {
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "reply client requires owned Job");
    const std::wstring prefix(name);
    const auto open = [&](const wchar_t* suffix) {
        return tf_conpty::Handle(OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (prefix + suffix).c_str()));
    };
    auto ready = open(L"-ready"), release = open(L"-release"), written = open(L"-written");
    require(ready.get() && release.get() && written.get(), "reply client gates unavailable");
    DWORD mode = 0;
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    require(GetConsoleMode(output, &mode) && SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING),
            "reply client output mode failed");
    require(SetEvent(ready.get()), "reply client ready failed");
    require(WaitForSingleObject(release.get(), 3000) == WAIT_OBJECT_0, "reply client release deadline");
    constexpr char bytes[] = "TF_REPLY_CLOSE_READY\r\n\x1b[6n";
    DWORD count = 0;
    require(WriteFile(output, bytes, sizeof(bytes) - 1, &count, nullptr) && count == sizeof(bytes) - 1,
            "reply client query write failed");
    require(SetEvent(written.get()), "reply client written signal failed");
    // The parent owns cancellation; do not let a natural child exit precede it.
    Sleep(10000);
    return 2;
}

int reply_window_case() {
    require(tf_shell::is_ime_desktop_child(), "reply test requires owned non-input desktop");
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "reply test requires owned Job");
    require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2), "reply test DPI setup failed");
    const auto prefix = L"Local\\TermForgeReply-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto create = [&](const wchar_t* suffix) {
        const auto event = CreateEventW(nullptr, TRUE, FALSE, (prefix + suffix).c_str());
        const auto error = GetLastError();
        if (!event || error == ERROR_ALREADY_EXISTS) {
            if (event) CloseHandle(event);
            throw std::runtime_error("reply gates must be fresh");
        }
        return event;
    };
    tf_conpty::Handle ready(create(L"-ready")), release(create(L"-release")), written(create(L"-written"));
    ShellWindow window("", "");
    window.enable_runtime_observation(); window.begin_output_observation();
    tf_shell::ShellLaunch launch;
    wchar_t path[32768]{};
    const auto path_size = GetModuleFileNameW(nullptr, path, 32768);
    require(path_size && path_size < 32768, "reply fixture executable path unavailable");
    launch.id = L"reply-close"; launch.executable.assign(path, path_size);
    launch.arguments = L"--reply-client \"" + prefix + L"\"";
    window.create(nullptr, launch);
    require(WaitForSingleObject(ready.get(), 3000) == WAIT_OBJECT_0, "reply console ready deadline");
    const auto before = window.runtime_snapshot();
    require(SetEvent(release.get()), "reply release failed");
    require(WaitForSingleObject(written.get(), 3000) == WAIT_OBJECT_0, "reply console write deadline");
    reply_wait([&] { return window.runtime_snapshot().output.queued_bytes > before.output.queued_bytes; },
               "reply transport receive deadline");
    require(window.runtime_snapshot().consumed_bytes == 0, "query must remain unparsed before close");
    window.close();
    reply_wait([&] { return window.runtime_snapshot().input.stopping; }, "reply input stop deadline");
    const auto deadline = GetTickCount64() + 5000;
    while (IsWindow(window.handle())) {
        require(GetTickCount64() < deadline, "reply window cleanup deadline");
        MSG message{};
        for (int count = 0; count < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
    const bool query = window.observed_output().find("\x1b[6n") != std::string::npos;
    const auto final = window.runtime_snapshot();
    std::cout << "{\"query_consumed_after_close\":" << (query ? "true" : "false")
              << ",\"consumed_bytes\":" << final.consumed_bytes
              << ",\"input_stopping\":" << (final.input.stopping ? "true" : "false")
              << ",\"input_rejected\":" << final.input.rejected << "}\n";
    const auto result = window.collect();
    require(query && window.live_contains_line("TF_REPLY_CLOSE_READY") && final.consumed_bytes > 4 &&
            final.input.stopping && final.input.rejected == 0 && result.complete && result.active_children == 0,
            "closing input must drain the real query without attempting a reply");
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--reply-client") return reply_client(utf16(argv[2]).c_str());
        if (argc == 2 && std::string(argv[1]) == "--owned-desktop-child") return reply_window_case();
        require(argc == 1, "unexpected reply test arguments");
        for (int round = 0; round < 2; ++round)
            require(tf_shell::run_ime_desktop_child(12000) == 0, "owned candidate closing reply failed");
        std::cout << "{\"closing_reply\":\"passed\",\"owned_job_zero\":true,\"desktop_deleted\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
