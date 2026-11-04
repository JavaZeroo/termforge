// Independent console receiver that echoes exactly the UTF-8 it received.
//
// The fixture reads the console input as UTF-16 units (ENABLE_VIRTUAL_TERMINAL_INPUT),
// keeps them until the EOT unit the test sends at the end, converts them to strict
// UTF-8, writes those bytes verbatim to a file (the receive oracle) and then echoes
// the same bytes to the console output so the formal Qt/ConPTY presentation path
// parses and paints them. The only transformation is the console line discipline:
// each received CR becomes CRLF on output, because the echo disabled by VT input
// mode is what would normally terminate a displayed line. Both the received and the
// echoed byte sequences are written to separate files so a test can compare the
// display source independently.
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
constexpr size_t maximum_units = 4096;

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

std::string echo_payload(const std::string& received) {
    std::string payload = "TF_ECHO_BEGIN\r\n";
    for (const char byte : received) {
        if (byte == '\r') payload += "\r\n";
        else payload += byte;
    }
    payload += "\r\nTF_ECHO_END\r\n";
    return payload;
}

void write_file(const std::wstring& path, const std::string& bytes) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("receiver output could not be created");
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) throw std::runtime_error("receiver output could not be written");
}

bool write_console(HANDLE output, const std::string& bytes) {
    DWORD written = 0;
    return WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           written == bytes.size();
}

int run(const std::wstring& receiver_path, const std::wstring& echo_path) {
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("echo receiver requires its owned Session Job");
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
    if (!write_console(output.get(), "TF_ECHO_READY\r\n")) throw std::runtime_error("ready marker write failed");

    std::wstring received;
    bool complete = false;
    while (!complete) {
        wchar_t units[256];
        DWORD count = 0;
        if (!ReadConsoleW(input.get(), units, static_cast<DWORD>(std::size(units)), &count, nullptr) || count == 0)
            throw std::runtime_error("console input read failed");
        for (DWORD index = 0; index < count; ++index) {
            if (units[index] == 4) {
                // EOT is this fixture's only end marker; input after it is invalid.
                if (index + 1 != count) throw std::runtime_error("input after EOT");
                complete = true;
                break;
            }
            received += units[index];
            if (received.size() > maximum_units) throw std::runtime_error("received text exceeds its fixed bound");
        }
    }
    const std::string received_utf8 = utf8(received);
    write_file(receiver_path, received_utf8);
    const std::string echoed = echo_payload(received_utf8);
    write_file(echo_path, echoed);
    if (!write_console(output.get(), echoed)) throw std::runtime_error("echo output write failed");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        return run(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TermForge Unicode echo receiver: %s\n", error.what());
        return 11;
    }
}
