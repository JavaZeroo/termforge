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
int wmain(int argc, wchar_t** argv) {
    try {
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
