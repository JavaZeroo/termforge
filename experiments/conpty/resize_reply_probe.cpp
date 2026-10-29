// Bounded diagnostic for the system ConPTY resize/reply ordering question in
// issue #145. It does not replace the lifecycle probe's oracle: the child is
// still the only source of truth for its own console size. Original, MIT.
#include "session.hpp"
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using tf_conpty::Session;
using tf_conpty::fail;

namespace {
std::wstring executable_path() {
    std::wstring result(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    if (!size || size == result.size()) fail("GetModuleFileName");
    result.resize(size); return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) fail("WideCharToMultiByte(size)");
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            result.data(), size, nullptr, nullptr) != size) fail("WideCharToMultiByte");
    return result;
}
void write_child(const std::string& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data() + offset,
                       static_cast<DWORD>(bytes.size() - offset), &written, nullptr) || !written) fail("child WriteFile");
        offset += written;
    }
}
// The client reports both console dimensions so a stale buffer can be told
// apart from a stale window. Neither value is asserted here.
struct ChildSize { int buffer_columns = 0, buffer_rows = 0, window_columns = 0, window_rows = 0; };

std::string describe(const ChildSize& size) {
    std::ostringstream out;
    out << size.buffer_columns << 'x' << size.buffer_rows << ':' << size.window_columns << 'x' << size.window_rows;
    return out.str();
}
bool parse_unsigned(const std::string& text, size_t& offset, int& value) {
    const size_t begin = offset;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
    if (begin == offset || offset - begin > 5) return false;
    value = std::stoi(text.substr(begin, offset - begin));
    return true;
}
bool parse_size(const std::string& raw, ChildSize& value) {
    size_t offset = 0;
    int bc = 0, br = 0, wc = 0, wr = 0;
    if (!parse_unsigned(raw, offset, bc) || offset >= raw.size() || raw[offset++] != 'x') return false;
    if (!parse_unsigned(raw, offset, br) || offset >= raw.size() || raw[offset++] != ':') return false;
    if (!parse_unsigned(raw, offset, wc) || offset >= raw.size() || raw[offset++] != 'x') return false;
    if (!parse_unsigned(raw, offset, wr) || offset != raw.size()) return false;
    value = {bc, br, wc, wr};
    return true;
}
void require(bool condition, const char* detail) {
    if (!condition) throw std::runtime_error(detail);
}
long long micros(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
}
std::string quoted(const std::string& text) {
    std::ostringstream result; result << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') result << '\\' << static_cast<char>(c);
        else if (c < 32) result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                << static_cast<unsigned>(c) << std::dec;
        else result << static_cast<char>(c);
    }
    result << '"'; return result.str();
}

int child() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8)) fail("SetConsoleCP");
    if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                DISABLE_NEWLINE_AUTO_RETURN)) fail("SetConsoleMode");
    write_child("TFREADY\r\n");
    std::wstring pending;
    wchar_t buffer[128]; DWORD count = 0;
    while (ReadConsoleW(input, buffer, 128, &count, nullptr) && count) {
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t c = buffer[i];
            if (c != L'\r' && c != L'\n') {
                if (pending.size() >= 256) return 12;
                pending += c; continue;
            }
            if (pending.empty()) continue;
            const std::string command = utf8(pending);
            pending.clear();
            if (command.rfind("SIZE:", 0) == 0) {
                CONSOLE_SCREEN_BUFFER_INFO info{};
                if (!GetConsoleScreenBufferInfo(output, &info)) fail("GetConsoleScreenBufferInfo");
                const ChildSize size{info.dwSize.X, info.dwSize.Y,
                                     info.srWindow.Right - info.srWindow.Left + 1,
                                     info.srWindow.Bottom - info.srWindow.Top + 1};
                write_child("TFSIZE:" + command.substr(5) + ":" + describe(size) + ":END\r\n");
            } else if (command == "EXIT") {
                write_child("TFEXIT:37\r\n");
                return 37;
            } else {
                return 13;
            }
        }
    }
    return 0;
}

// The reply carries its own tag so a late reply from an earlier round can
// never satisfy a later request.
bool wait_reply(Session& session, const std::string& tag, std::string& raw, unsigned timeout_ms) {
    const std::string prefix = "TFSIZE:" + tag + ":";
    if (!session.wait_text(prefix, timeout_ms)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const std::string output = session.output();
        const auto begin = output.find(prefix);
        if (begin != std::string::npos) {
            const auto end = output.find(":END", begin);
            if (end != std::string::npos) { raw = output.substr(begin, end + 4 - begin); return true; }
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        Sleep(1);
    }
}
bool reply_size(const std::string& raw, const std::string& tag, ChildSize& value) {
    const std::string prefix = "TFSIZE:" + tag + ":";
    if (raw.rfind(prefix, 0) != 0) return false;
    const std::string body = raw.substr(prefix.size());
    if (body.size() < 4 || body.compare(body.size() - 4, 4, ":END") != 0) return false;
    return parse_size(body.substr(0, body.size() - 4), value);
}

int host(unsigned rounds, bool barrier) {
    Session session;
    int exit_status = 0;
    try {
        session.start(executable_path());
        require(session.wait_text("TFREADY"), "ready marker timeout");
        session.send("SIZE:init\r");
        std::string raw;
        require(wait_reply(session, "init", raw, 5000), "initial size reply timeout");
        ChildSize initial{};
        require(reply_size(raw, "init", initial), "initial size reply malformed");
        std::cout << "{\"ready\":true,\"initial\":" << quoted(describe(initial))
                  << ",\"backend\":" << quoted(session.backend_identity()) << "}" << std::endl;
        for (unsigned index = 1; index <= rounds; ++index) {
            // Alternate so each round has to apply a real size change.
            const bool wide = (index % 2) == 1;
            const short columns = wide ? 120 : 80, rows = wide ? 40 : 24;
            const auto resize_begin = std::chrono::steady_clock::now();
            session.resize(columns, rows);
            const auto resize_return = std::chrono::steady_clock::now();
            std::ostringstream resize_vt;
            resize_vt << "\x1b[8;" << rows << ';' << columns << 't';
            const bool barrier_vt_seen = barrier && session.wait_text(resize_vt.str(), 500);
            const std::string tag = "r" + std::to_string(index);
            session.send("SIZE:" + tag + "\r");
            const auto send_done = std::chrono::steady_clock::now();
            const bool replied = wait_reply(session, tag, raw, 5000);
            const auto reply_done = std::chrono::steady_clock::now();
            ChildSize reply{};
            const bool parsed = replied && reply_size(raw, tag, reply);
            const std::string output = session.output();
            std::cout << "{\"round\":" << index
                      << ",\"request\":" << quoted(std::to_string(columns) + "x" + std::to_string(rows))
                      << ",\"replied\":" << (replied ? "true" : "false")
                      << ",\"parsed\":" << (parsed ? "true" : "false")
                      << ",\"reply\":" << quoted(parsed ? describe(reply) : std::string{})
                      << ",\"buffer_matches_request\":" << (parsed && reply.buffer_columns == columns && reply.buffer_rows == rows ? "true" : "false")
                      << ",\"window_matches_request\":" << (parsed && reply.window_columns == columns && reply.window_rows == rows ? "true" : "false")
                      << ",\"resize_vt_before_reply\":" << (output.find(resize_vt.str()) != std::string::npos ? "true" : "false")
                      << ",\"barrier\":" << (barrier ? "true" : "false")
                      << ",\"barrier_vt_seen\":" << (barrier_vt_seen ? "true" : "false")
                      << ",\"resize_to_send_us\":" << micros(resize_begin, send_done)
                      << ",\"resize_to_reply_us\":" << micros(resize_begin, reply_done)
                      << "}" << std::endl;
            if (!parsed) { exit_status = 1; break; }
        }
        session.send("EXIT\r");
        const bool exited = session.wait_exit(5000);
        if (!exited || session.exit_code != 37) {
            std::cerr << "normal exit mismatch\n";
            exit_status = 1;
        }
        std::cout << "{\"complete\":" << (exit_status == 0 ? "true" : "false") << "}" << std::endl;
    } catch (const std::exception& error) {
        session.close();
        std::cerr << error.what() << '\n';
        return 1;
    }
    session.close();
    return exit_status;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--child") return child();
        if (argc != 4 || std::string(argv[1]) != "--host") return 2;
        const unsigned rounds = static_cast<unsigned>(std::stoul(argv[2]));
        const std::string mode = argv[3];
        if (!rounds || rounds > 1000 || (mode != "raw" && mode != "barrier")) return 2;
        // The supervisor assigns this host to its job before sending this gate.
        std::string gate;
        if (!std::getline(std::cin, gate) || gate != "GO") return 2;
        return host(rounds, mode == "barrier");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
