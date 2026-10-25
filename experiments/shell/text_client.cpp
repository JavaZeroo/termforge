// Original deterministic ConPTY text producer and raw, non-echoing receiver.
// Owned test Job controls its lifetime. It never executes received commands.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include "workflow_fixture.hpp"
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void output(const std::string& value) {
    size_t offset = 0;
    while (offset < value.size()) {
        DWORD written = 0;
        require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value.data() + offset,
            static_cast<DWORD>((std::min)(size_t{16384}, value.size() - offset)), &written, nullptr) && written,
            "text fixture output failed");
        offset += written;
    }
}
static std::string rows(unsigned first, unsigned count, bool matches) {
    std::ostringstream out;
    for (unsigned row = first; row < first + count; ++row) {
        out << (matches ? "TF_FIND_" : "TF_EVICT_") << std::setw(3) << std::setfill('0') << row;
        if (matches) out << " needle \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81";
        out << "\r\n";
    }
    return out.str();
}
class StreamOutput {
    HANDLE stop_ = nullptr;
    std::thread thread_;
    std::atomic<bool> failed_{false};
public:
    static constexpr DWORD deadline_ms = 5000;
    StreamOutput() : stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        require(stop_ != nullptr, "stream fixture stop event failed");
    }
    void stop() noexcept {
        if (!thread_.joinable()) return;
        SetEvent(stop_);
        const auto deadline = GetTickCount64() + 3000;
        while (WaitForSingleObject(thread_.native_handle(), 5) != WAIT_OBJECT_0) {
            CancelSynchronousIo(thread_.native_handle());
            if (GetTickCount64() >= deadline) std::_Exit(11);
        }
        try { thread_.join(); } catch (...) { std::_Exit(11); }
    }
    void start() {
        stop(); require(ResetEvent(stop_) != FALSE, "stream fixture reset failed"); failed_ = false;
        output("TF_STREAM_STARTED\r\n");
        thread_ = std::thread([this] {
            try {
                const auto deadline = GetTickCount64() + deadline_ms;
                for (unsigned tick = 0; GetTickCount64() < deadline; ++tick) {
                    if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) return;
                    std::ostringstream out; out << "\rTF_STREAM_TICK_" << std::setw(8) << std::setfill('0') << tick;
                    output(out.str());
                    if (WaitForSingleObject(stop_, 4) == WAIT_OBJECT_0) return;
                }
                output("\r\x1b[2KTF_STREAM_DEADLINE\r\n");
            } catch (...) { if (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) failed_ = true; }
        });
    }
    void stop_and_mark() {
        stop(); require(!failed_, "stream fixture output worker failed"); output("\r\x1b[2KTF_STREAM_STOPPED\r\n");
    }
    ~StreamOutput() { stop(); if (stop_) CloseHandle(stop_); }
};
int unicode_fixture(const wchar_t* capture) {
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "Unicode fixture requires an owned Job");
    tf_shell_test::WorkflowOptions options;
    options.capture = capture;
    options.generation = 1;
    const std::wstring text =
        L"\x1b[2J\x1b[H"
        L"TF_U_DECOMPOSE e\u0301\r\n"
        L"TF_U_EMOJI \U0001F642\r\n"
        L"TF_U_ZWJ \U0001F469\u200D\U0001F4BB\r\n"
        L"TF_U_KEYCAP 1\uFE0F\u20E3\r\n"
        L"TF_U_DONE\r\n";
    DWORD written = 0;
    require(WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()),
                          &written, nullptr) && written == text.size(), "Unicode console output failed");
    tf_conpty::Handle source(tf_shell_test::workflow_new_file(std::filesystem::path(capture).wstring() + L".source.bin"));
    const std::string bytes =
        "TF_U_DECOMPOSE e\xcc\x81\r\n"
        "TF_U_EMOJI \xf0\x9f\x99\x82\r\n"
        "TF_U_ZWJ \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\r\n"
        "TF_U_KEYCAP 1\xef\xb8\x8f\xe2\x83\xa3\r\n"
        "TF_U_DONE\r\n";
    tf_shell_test::workflow_save(source.get(), bytes);
    require(FlushFileBuffers(source.get()) != FALSE, "Unicode source flush failed");
    workflow_capture_console(options, "unicode");
    return 0;
}
void wait_alternate_console(const std::filesystem::path& capture, const std::wstring& text, DWORD foreground) {
    const auto console = GetStdHandle(STD_OUTPUT_HANDLE);
    const auto deadline = GetTickCount64() + 1000;
    for (;;) {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        require(GetConsoleScreenBufferInfo(console, &info) != FALSE, "alternate console dimensions failed");
        const int columns = info.dwSize.X;
        require(columns > 0 && columns <= 500 && info.srWindow.Top >= 0 &&
                info.srWindow.Bottom >= info.srWindow.Top && info.srWindow.Bottom - info.srWindow.Top < 200,
                "alternate console bounds failed");
        for (SHORT row = info.srWindow.Top; row <= info.srWindow.Bottom; ++row) {
            std::wstring line(static_cast<size_t>(columns), L'\0');
            DWORD count = 0;
            require(ReadConsoleOutputCharacterW(console, line.data(), columns, {0,row}, &count) != FALSE,
                    "alternate console text read failed");
            const auto offset = line.find(text);
            if (offset == std::wstring::npos) continue;
            std::vector<CHAR_INFO> cells(static_cast<size_t>(columns));
            SMALL_RECT region{0,row,static_cast<SHORT>(columns-1),row};
            require(ReadConsoleOutputW(console, cells.data(), {static_cast<SHORT>(columns),1}, {0,0}, &region) != FALSE &&
                    region.Left == 0 && region.Top == row && region.Right == columns-1 && region.Bottom == row,
                    "alternate console cell read failed");
            bool matches = offset + text.size() <= cells.size();
            for (size_t index = 0; matches && index < text.size(); ++index) {
                matches = cells[offset+index].Char.UnicodeChar == text[index] &&
                    static_cast<DWORD>(cells[offset+index].Attributes & 0xf) == foreground;
            }
            if (matches) return;
        }
        if (GetTickCount64() >= deadline) {
            tf_shell_test::WorkflowOptions options;
            options.capture = capture.wstring() + L".timeout";
            options.generation = 1;
            workflow_capture_console(options, "alt-timeout");
            require(false, "alternate native state timeout");
        }
        Sleep(1);
    }
}
int alternate_fixture(const wchar_t* capture, const wchar_t* gate_name) {
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "alternate fixture requires an owned Job");
    tf_shell_test::WorkflowOptions options;
    options.capture = capture;
    options.generation = 1;
    const auto base = options.capture.wstring();
    const auto snapshot = [&](const wchar_t* suffix, const std::string& role) {
        options.capture = base + suffix;
        workflow_capture_console(options, role);
    };
    tf_conpty::Handle gate(CreateEventW(nullptr, TRUE, FALSE, gate_name));
    require(gate.get() != nullptr, "alternate fixture event creation failed");
    const std::string main_text = "\x1b[2J\x1b[H\x1b[32mMAIN_GREEN\x1b[0m\r\n";
    const std::string first = "\x1b[?1049h\x1b[34mALT_ONE\x1b[0m\x1b[?1049l";
    const std::string middle = "MAIN_AFTER\r\n";
    const std::string second = "\x1b[?1049h\x1b[31mALT_TWO\x1b[0m\x1b[?1049l";
    const std::string done = "TF_ALT_DONE\r\n";
    output(main_text); wait_alternate_console(capture, L"MAIN_GREEN", 2); snapshot(L".before", "main-before");
    output("\x1b[?1049h\x1b[34mALT_ONE\x1b[0m");
    wait_alternate_console(capture, L"ALT_ONE", 1); snapshot(L".alt1", "alt-one");
    output("\x1b[?1049l"); wait_alternate_console(capture, L"MAIN_GREEN", 2); snapshot(L".restored1", "main-restored");
    output(middle);
    output("\x1b[?1049h\x1b[31mALT_TWO\x1b[0m");
    wait_alternate_console(capture, L"ALT_TWO", 4); snapshot(L".alt2", "alt-two");
    output("\x1b[?1049l"); output(done);
    wait_alternate_console(capture, L"TF_ALT_DONE", 7); snapshot(L".final", "main-final");
    tf_conpty::Handle source(tf_shell_test::workflow_new_file(base + L".source.bin"));
    tf_shell_test::workflow_save(source.get(), main_text + first + middle + second + done);
    require(FlushFileBuffers(source.get()) != FALSE, "alternate source flush failed");
    require(WaitForSingleObject(gate.get(), 5000) == WAIT_OBJECT_0, "alternate fixture release timeout");
    return 0;
}
int scroll_fixture(const wchar_t* capture, const wchar_t* gate_name) {
    BOOL owned = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "scroll fixture requires an owned Job");
    tf_shell_test::WorkflowOptions options;
    options.capture = capture;
    options.generation = 1;
    const auto base = options.capture.wstring();
    const auto snapshot = [&](const wchar_t* suffix, const std::string& role) {
        options.capture = base + suffix;
        workflow_capture_console(options, role);
    };
    tf_conpty::Handle gate(CreateEventW(nullptr, TRUE, FALSE, gate_name));
    require(gate.get() != nullptr, "scroll fixture event creation failed");
    const std::string initial = "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n";
    const std::string region = "\x1b[2;3r\x1b[3;1H\nREGION_C\nREGION_D";
    const std::string done = "\x1b[r\x1b[5;1HTF_SCROLL_DONE\r\n";
    output(initial); wait_alternate_console(capture, L"BOTTOM_FIXED", 7); snapshot(L".before", "main-before");
    output(region); wait_alternate_console(capture, L"REGION_D", 7); snapshot(L".region", "main-region");
    output(done); wait_alternate_console(capture, L"TF_SCROLL_DONE", 7); snapshot(L".final", "main-final");
    tf_conpty::Handle source(tf_shell_test::workflow_new_file(base + L".source.bin"));
    tf_shell_test::workflow_save(source.get(), initial + region + done);
    require(FlushFileBuffers(source.get()) != FALSE, "scroll source flush failed");
    require(WaitForSingleObject(gate.get(), 5000) == WAIT_OBJECT_0, "scroll fixture release timeout");
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if(argc>1 && std::wstring(argv[1])==L"--workflow-v1")
            return tf_shell_test::run_workflow_fixture(tf_shell_test::workflow_options(argc,argv));
        if(argc==4 && std::wstring(argv[1])==L"--scroll-v1") {
            const auto input = GetStdHandle(STD_INPUT_HANDLE);
            require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
                    SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT), "scroll fixture console mode failed");
            return scroll_fixture(argv[2], argv[3]);
        }
        if(argc==4 && std::wstring(argv[1])==L"--alt-v1") {
            const auto input = GetStdHandle(STD_INPUT_HANDLE);
            require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
                    SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT), "alternate fixture console mode failed");
            return alternate_fixture(argv[2], argv[3]);
        }
        if(argc==3 && std::wstring(argv[1])==L"--unicode-v1") {
            const auto input = GetStdHandle(STD_INPUT_HANDLE);
            require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
                    SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT), "Unicode fixture console mode failed");
            return unicode_fixture(argv[2]);
        }
        require(argc == 2 && *argv[1], "usage: shell_text_client capture.bin");
        const auto input = GetStdHandle(STD_INPUT_HANDLE);
        require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
                SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT), "text fixture raw console mode failed");
        std::ofstream capture(std::filesystem::path(argv[1]), std::ios::binary | std::ios::trunc);
        require(bool(capture), "text fixture capture creation failed");
        output("\x1b[2J\x1b[H" + rows(0, 80, true) + "TF_FIND_READY 080\r\n");
        StreamOutput stream;
        wchar_t pending = 0; size_t captured = 0;
        for (;;) {
            wchar_t units[256]{}; DWORD count = 0;
            require(ReadConsoleW(input, units, 256, &count, nullptr) && count, "text fixture input failed");
            std::wstring text; if (pending) { text += pending; pending = 0; }
            text.append(units, count);
            if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) {
                pending = text.back(); text.pop_back();
            }
            if (!text.empty()) {
                const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                    static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                require(bytes > 0 && captured + bytes <= 1024 * 1024, "text fixture UTF-8 input or capture limit failed");
                std::string encoded(static_cast<size_t>(bytes), '\0');
                require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                    encoded.data(), bytes, nullptr, nullptr) == bytes, "text fixture encoding failed");
                capture.write(encoded.data(), bytes); capture.flush(); require(bool(capture), "text fixture capture write failed");
                captured += bytes;
            }
            for (const auto unit : text) {
                if (unit == L'+') output(rows(80, 20, true) + "TF_FIND_APPEND_READY 100\r\n");
                if (unit == L'#') output(rows(0, 500, false) + "TF_FIND_EVICT_READY 500\r\n");
                if (unit == L'~') stream.start();
                if (unit == L'-') stream.stop_and_mark();
            }
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
