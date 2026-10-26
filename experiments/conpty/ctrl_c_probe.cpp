// Original, bounded cmd/ConPTY interrupt diagnostic. MIT; see LICENSE.
#include "session.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
using Clock = std::chrono::steady_clock;
constexpr unsigned loop_count = 1000000;
constexpr unsigned marker_timeout_ms = 5000;

// A marker must occupy its own completed output line. Matching an arbitrary
// substring would accept the line editor's echo of "echo TF_AFTER_FLOOD".
std::string plain_text(const std::string& raw) {
    std::string text;
    for (size_t at = 0; at < raw.size();) {
        const auto value = static_cast<unsigned char>(raw[at++]);
        if (value == 27 && at < raw.size()) {
            const char kind = raw[at++];
            if (kind == '[') {
                while (at < raw.size()) {
                    const auto c = static_cast<unsigned char>(raw[at++]);
                    if (c >= 0x40 && c <= 0x7e) break;
                }
            } else if (kind == ']') {
                while (at < raw.size()) {
                    if (raw[at++] == '\a') break;
                    if (at >= 2 && raw[at - 2] == 27 && raw[at - 1] == '\\') break;
                }
            }
        } else if (value != '\r' && value != '\a') text.push_back(static_cast<char>(value));
    }
    return text;
}

unsigned marker_lines(const std::string& raw, const std::string& expected) {
    const auto text = plain_text(raw);
    unsigned count = 0;
    for (size_t at = 0; at < text.size();) {
        const auto end = text.find('\n', at);
        if (end == std::string::npos) break;
        if (text.compare(at, end - at, expected) == 0) ++count;
        at = end + 1;
    }
    return count;
}

unsigned highest_busy(const std::string& raw) {
    const auto text = plain_text(raw);
    unsigned highest = 0;
    for (size_t at = text.find("TF_BUSY_"); at != std::string::npos; at = text.find("TF_BUSY_", at + 8)) {
        size_t end = at + 8;
        unsigned number = 0;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
            if (number > loop_count) break;
            number = number * 10 + static_cast<unsigned>(text[end++] - '0');
        }
        if (end > at + 8 && end < text.size() && text[end] == '\n') highest = (std::max)(highest, number);
    }
    return highest;
}

bool wait_marker(tf_conpty::Session& session, const std::string& marker) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(marker_timeout_ms);
    do {
        if (marker_lines(session.output(), marker) == 1) return true;
        Sleep(10);
    } while (Clock::now() < deadline);
    return false;
}

long long elapsed_ms(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}
void require(bool value) { if (!value) throw std::runtime_error("diagnostic stage failed"); }
} // namespace

int main() {
    std::cout << std::boolalpha;
    // No PTY client, ConPTY, raw file or input exists before the parent's gate.
    // Windows may give this console-subsystem host its own headless conhost.
    char gate[4]{};
    size_t gate_bytes = 0;
    while (gate_bytes < sizeof(gate)) {
        DWORD count = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate + gate_bytes, 1, &count, nullptr) || count != 1) break;
        if (gate[gate_bytes++] == '\n') break;
    }
    const std::string_view gate_value(gate, gate_bytes);
    if (gate_value != "GO\n" && gate_value != "GO\r\n") {
        std::cout << "{\"schema\":\"termforge-ctrl-c-native-v1\",\"status\":\"guard_rejected\"}\n";
        return 2;
    }
    BOOL contained = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &contained) || !contained) {
        std::cout << "{\"schema\":\"termforge-ctrl-c-native-v1\",\"status\":\"job_required\"}\n";
        return 2;
    }
    tf_conpty::Session session;
    const auto begin = Clock::now();
    std::ofstream input("input.bin", std::ios::binary);
    size_t input_bytes = 0, flood_input_bytes = 0;
    bool idle = false, flood = false, after = false, exited = false;
    long long busy_hold_ms = 0, after_wait_ms = -1;
    const char* phase = "input_artifact";
    const char* failure = "none";
    auto send = [&](const std::string& bytes, bool flood_input = false) {
        size_t confirmed = 0;
        session.send(bytes, [&](size_t count) {
            input.write(bytes.data() + confirmed, static_cast<std::streamsize>(count));
            confirmed += count; input_bytes += count;
            if (flood_input) flood_input_bytes += count;
        });
        input.flush(); require(static_cast<bool>(input));
    };
    try {
        require(static_cast<bool>(input));
        wchar_t directory[MAX_PATH]{};
        phase = "system_directory";
        const auto length = GetSystemDirectoryW(directory, MAX_PATH);
        require(length && length < MAX_PATH);
        phase = "start";
        session.start(std::wstring(directory) + L"\\cmd.exe", L"/d /q /k \"prompt TF$G&cls\"");
        phase = "prompt"; require(session.wait_text("TF>"));
        phase = "idle_input";
        send("@echo off\r"); send(std::string(1, '\x03')); send("echo TF_AFTER_IDLE\r");
        phase = "idle_marker"; idle = wait_marker(session, "TF_AFTER_IDLE"); require(idle);
        phase = "flood_input";
        send("for /L %i in (1,1,1000000) do @echo TF_BUSY_%i\r");
        phase = "flood_output";
        const auto first_busy_deadline = Clock::now() + std::chrono::milliseconds(marker_timeout_ms);
        while (!(flood = highest_busy(session.output()) > 0) && Clock::now() < first_busy_deadline) Sleep(10);
        require(flood);
        const auto first_busy = Clock::now();
        Sleep(1000);
        busy_hold_ms = elapsed_ms(first_busy);
        phase = "interrupt_input";
        const auto interrupt = Clock::now();
        send(std::string(1, '\x03'), true);
        send("echo TF_AFTER_FLOOD\r", true); // no inserted delay or alternate encoding
        phase = "interrupt_marker";
        after = wait_marker(session, "TF_AFTER_FLOOD"); after_wait_ms = elapsed_ms(interrupt);
        require(after && highest_busy(session.output()) < loop_count);
        phase = "exit_input"; send("exit 37\r");
        phase = "exit"; exited = session.wait_exit(5000); require(exited && session.exit_code == 37);
    } catch (...) { failure = phase; }
    // Preserve the pre-cleanup observation separately from close-generated I/O.
    const auto output = session.output();
    const auto highest = highest_busy(output);
    const auto markers = marker_lines(output, "TF_AFTER_FLOOD");
    std::ofstream raw("output.bin", std::ios::binary);
    raw.write(output.data(), static_cast<std::streamsize>(output.size())); raw.close();
    input.close();
    const bool artifacts = static_cast<bool>(raw) && static_cast<bool>(input);
    session.close();
    const bool valid = failure == std::string("none") && artifacts && idle && flood && after && exited &&
        markers == 1 && highest > 0 && highest < loop_count && busy_hold_ms >= 1000 &&
        session.cleanup_complete && !session.capture_overflow &&
        (session.read_error == ERROR_SUCCESS || session.read_error == ERROR_BROKEN_PIPE);
    std::cout << "{\"schema\":\"termforge-ctrl-c-native-v1\",\"status\":\"" << (valid ? "passed" : "failed")
              << "\",\"failure_code\":\"" << failure << "\",\"idle_marker\":" << idle
              << ",\"flood_observed\":" << flood << ",\"after_marker\":" << after
              << ",\"after_marker_lines\":" << markers << ",\"highest_busy\":" << highest
              << ",\"busy_hold_ms\":" << busy_hold_ms << ",\"after_wait_ms\":" << after_wait_ms
              << ",\"input_bytes\":" << input_bytes << ",\"flood_input_bytes\":" << flood_input_bytes
              << ",\"output_bytes\":" << output.size() << ",\"exit_code\":" << session.exit_code
              << ",\"read_error\":" << session.read_error << ",\"capture_overflow\":" << session.capture_overflow
              << ",\"active_children_after\":" << session.active_after
              << ",\"cleanup_complete\":" << session.cleanup_complete
              << ",\"forced_process_cancel\":" << session.forced_process_cancel
              << ",\"forced_reader_cancel\":" << session.forced_reader_cancel
              << ",\"elapsed_ms\":" << elapsed_ms(begin) << "}\n";
    return valid ? 0 : 1;
}
