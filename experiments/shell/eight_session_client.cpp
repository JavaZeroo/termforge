// One independent console receiver for the formal Qt eight-session matrix.
//
// Each child owns its own identity (PID plus a process-local nonce), receiver file
// and bounded background output. The test creates it only through the public tab
// entry, reads the READY identity the child prints, and verifies the received UTF-8
// against literal expectations. Control protocol (not product behavior):
//   *BG*  starts the single bounded background block; the marker bytes are not
//          recorded in the receiver file.
//   EOT   stops the child naturally after flushing the receiver file.
// Everything else is appended to <root>/<nonce>.bin as received UTF-8.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr size_t maximum_received = 64 * 1024;
constexpr unsigned background_lines = 120;
constexpr char background_marker[] = "*BG*";
constexpr size_t background_marker_size = sizeof(background_marker) - 1;

class Handle {
    HANDLE value_ = nullptr;
public:
    explicit Handle(HANDLE value) : value_(value) {
        if (!value_ || value_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("create/open handle failed: " + std::to_string(GetLastError()));
    }
    ~Handle() { CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
};

std::string utf8(const std::wstring& units) {
    if (units.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, units.data(),
                                          static_cast<int>(units.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("console input is not valid UTF-16");
    std::string encoded(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, units.data(), static_cast<int>(units.size()),
                            encoded.data(), bytes, nullptr, nullptr) != bytes)
        throw std::runtime_error("console input UTF-8 conversion failed");
    return encoded;
}

std::string hex64(uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<size_t>(index)] = digits[value & 0xf];
        value >>= 4;
    }
    return text;
}

bool write_bytes(HANDLE handle, const char* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        if (!WriteFile(handle, data + offset, static_cast<DWORD>(size - offset), &written, nullptr) || !written)
            return false;
        offset += written;
    }
    return true;
}

class Receiver {
    Handle output_;
    Handle file_;
    std::string pending_;
    std::wstring pending_high_;
    unsigned long long saved_ = 0;
    std::atomic<bool> background_started_{false};
    Handle stop_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::thread background_;
    std::atomic<bool> failed_{false};
public:
    Receiver(HANDLE output, const std::wstring& path) : output_(output), file_(CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~Receiver() { stop_background(); }
    void start_background(const std::string& line_prefix) {
        bool expected = false;
        if (!background_started_.compare_exchange_strong(expected, true)) return;
        background_ = std::thread([this, line_prefix] {
            for (unsigned sequence = 1; sequence <= background_lines; ++sequence) {
                if (WaitForSingleObject(stop_.get(), 10) != WAIT_TIMEOUT) return;
                char line[160];
                const int length = std::snprintf(line, sizeof(line), "%s seq=%03u\r\n",
                                                 line_prefix.c_str(), sequence);
                if (length <= 0 || static_cast<size_t>(length) >= sizeof(line) ||
                    !write_bytes(output_.get(), line, static_cast<size_t>(length))) {
                    failed_ = true;
                    return;
                }
            }
        });
    }
    void stop_background() noexcept {
        if (!background_.joinable()) return;
        SetEvent(stop_.get());
        background_.join();
    }
    void write_received(const std::string& bytes) {
        if (bytes.empty()) return;
        if (saved_ + bytes.size() > maximum_received) {
            failed_ = true;
            throw std::runtime_error("received text exceeds the fixed bound");
        }
        if (!write_bytes(file_.get(), bytes.data(), bytes.size()) || !FlushFileBuffers(file_.get()))
            throw std::runtime_error("receiver write failed");
        saved_ += bytes.size();
    }
    // Extract the control marker from the byte stream; hold a possible marker prefix
    // at the tail until the next chunk so a split write cannot record it as text.
    void feed(std::string bytes) {
        pending_ += bytes;
        for (;;) {
            const auto position = pending_.find(background_marker);
            if (position == std::string::npos) break;
            pending_.erase(position, background_marker_size);
            start_background(background_prefix_);
        }
        size_t keep = 0;
        const size_t limit = (std::min)(pending_.size(), background_marker_size - 1);
        for (size_t length = limit; length > 0; --length) {
            if (pending_.compare(pending_.size() - length, length, background_marker, 0, length) == 0) {
                keep = length;
                break;
            }
        }
        const std::string deliver = pending_.substr(0, pending_.size() - keep);
        pending_.erase(0, pending_.size() - keep);
        write_received(deliver);
    }
    void finish() {
        stop_background();
        write_received(pending_);
        pending_.clear();
        if (failed_) throw std::runtime_error("background output failed");
        FlushFileBuffers(file_.get());
    }
    void set_prefix(const std::string& prefix) { background_prefix_ = prefix; }
    bool background_started() const { return background_started_.load(); }
    bool failed() const { return failed_; }
private:
    std::string background_prefix_;
};

int run(const std::wstring& root) {
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("eight-session receiver requires its owned Session Job");
    const DWORD pid = GetCurrentProcessId();
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter)) throw std::runtime_error("QueryPerformanceCounter failed");
    std::string nonce = hex64((static_cast<uint64_t>(counter.QuadPart) << 16) ^
                              (static_cast<uint64_t>(pid) << 32) ^ GetTickCount64());
    nonce = nonce.substr(nonce.size() - 16);
    Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, 0, nullptr));
    Handle output(CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr));
    if (!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                                          ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                          DISABLE_NEWLINE_AUTO_RETURN))
        throw std::runtime_error("console mode/encoding setup failed");
    const std::wstring path = root + L"\\" + std::wstring(nonce.begin(), nonce.end()) + L".bin";
    Receiver receiver(output.get(), path);
    const std::string prefix = "TF8_BG pid=" + std::to_string(pid) + " nonce=" + nonce;
    receiver.set_prefix(prefix);
    {
        const std::string ready = "TF8_READY pid=" + std::to_string(pid) + " nonce=" + nonce + "\r\n";
        if (!write_bytes(output.get(), ready.data(), ready.size()))
            throw std::runtime_error("ready marker write failed");
    }
    std::wstring pending_high;
    for (;;) {
        wchar_t units[256];
        DWORD count = 0;
        if (!ReadConsoleW(input.get(), units, static_cast<DWORD>(std::size(units)), &count, nullptr) || count == 0)
            throw std::runtime_error("console input read failed");
        std::wstring chunk;
        bool finished = false;
        for (DWORD index = 0; index < count; ++index) {
            if (units[index] == 4) {
                if (index + 1 != count) throw std::runtime_error("input after EOT");
                finished = true;
                break;
            }
            chunk += units[index];
        }
        if (!pending_high.empty()) chunk.insert(chunk.begin(), pending_high.front());
        pending_high.clear();
        if (!chunk.empty() && chunk.back() >= 0xd800 && chunk.back() <= 0xdbff) {
            pending_high.push_back(chunk.back());
            chunk.pop_back();
        }
        receiver.feed(utf8(chunk));
        if (receiver.failed()) throw std::runtime_error("bounded background output failed");
        if (finished) {
            if (!pending_high.empty()) receiver.feed(utf8(std::wstring(1, 0xfffd)));
            receiver.finish();
            const std::string done = "TF8_DONE pid=" + std::to_string(pid) + " nonce=" + nonce + "\r\n";
            if (!write_bytes(output.get(), done.data(), done.size()))
                throw std::runtime_error("done marker write failed");
            return 0;
        }
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    try {
        return run(argv[1]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TermForge eight-session receiver: %s\n", error.what());
        return 11;
    }
}
