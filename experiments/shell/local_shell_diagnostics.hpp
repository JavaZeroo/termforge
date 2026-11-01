// Synthetic ui-shells diagnostics. Raw output is private and independently bounded.
#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

namespace tf_shell_test {
inline void local_shell_progress(const std::filesystem::path& directory, std::string_view role,
                                 std::string_view event, int stage,
                                 std::optional<unsigned long> exit_code = std::nullopt,
                                 bool cleanup_complete = false, unsigned long active_children = 0,
                                 std::string_view output = {}) {
    require(role=="cmd" || role=="powershell" || role=="pwsh" || role=="wsl", "invalid local shell diagnostic role");
    require(event=="started" || event=="cwd-verified" || event=="size-helper-held" || event=="sizes-ready" || event=="exited", "invalid local shell diagnostic event");
    require(output.size()<=8192, "local shell diagnostic output exceeds its bound");
    std::ofstream progress(directory/"shells-progress.jsonl",std::ios::binary|std::ios::app);
    progress<<"{\"schema\":\"termforge-local-shell-progress-v1\",\"role\":\""<<role
            <<"\",\"event\":\""<<event<<"\",\"stage\":"<<stage
            <<",\"availability_preflight_performed\":false,\"exit_code\":";
    if(exit_code) progress<<*exit_code; else progress<<"null";
    progress<<",\"cleanup_complete\":"<<(cleanup_complete?"true":"false")
            <<",\"active_children_after\":"<<active_children<<",\"observed_output_tail_bytes\":"<<output.size()
            <<",\"presented_done\":null}\n";
    progress.close(); require(bool(progress), "local shell diagnostic progress write failed");
    if(event=="exited") {
        const auto private_directory=directory/"_private"/"local-shells";
        std::filesystem::create_directories(private_directory);
        std::ofstream raw(private_directory/(std::string(role)+".output.bin"),std::ios::binary|std::ios::trunc);
        raw.write(output.data(),static_cast<std::streamsize>(output.size()));
        raw.close(); require(bool(raw), "local shell private output write failed");
    }
}
inline void local_shell_size_readiness(const std::filesystem::path& directory, std::string_view role,
                                       unsigned long columns, unsigned long rows, bool settled,
                                       bool marker_present, bool transport_completed,
                                       unsigned long observed_bytes) {
    require(role=="cmd" || role=="powershell" || role=="pwsh" || role=="wsl", "invalid local shell diagnostic role");
    std::ofstream progress(directory/"shells-size-readiness.jsonl",std::ios::binary|std::ios::app);
    progress<<"{\"schema\":\"termforge-local-shell-size-readiness-v1\",\"role\":\""<<role
            <<"\",\"columns\":"<<columns<<",\"rows\":"<<rows
            <<",\"settled\":"<<(settled?"true":"false")
            <<",\"marker_present\":"<<(marker_present?"true":"false")
            <<",\"transport_completed\":"<<(transport_completed?"true":"false")
            <<",\"observed_output_tail_bytes\":"<<observed_bytes<<"}\n";
    progress.close(); require(bool(progress), "local shell size readiness write failed");
}
// Diagnostic-only copy of the transport oracle's line rules. It never changes a
// pass or fail: the shells scenario calls it on its timeout path so a stage five
// failure can report which transport condition was unmet.
inline void local_shell_transport_probe(const std::filesystem::path& directory, std::string_view role,
                                        std::string_view raw, std::string_view marker) {
    bool output_seen = false, prompt_seen = false, literal = true, malformed = false;
    std::string line;
    const auto prompt = [](std::string_view text) { return text == "TF>" || text == "TF> "; };
    const auto end_line = [&] {
        if (literal && line == marker) output_seen = true;
        else if (literal && output_seen && prompt(line)) prompt_seen = true;
        line.clear();
        literal = true;
    };
    for (std::size_t i = 0; i < raw.size();) {
        const auto byte = static_cast<unsigned char>(raw[i++]);
        if (byte == 0x1b) {
            if (i == raw.size()) { malformed = true; break; }
            const char kind = raw[i++];
            if (kind == ']') {
                bool terminated = false;
                while (i < raw.size()) {
                    const char next = raw[i++];
                    if (next == '\a') { terminated = true; break; }
                    if (next == '\x1b') {
                        if (i == raw.size() || raw[i++] != '\\') { malformed = true; break; }
                        terminated = true;
                        break;
                    }
                }
                if (!terminated) { malformed = true; break; }
            } else if (kind == '[') {
                while (i < raw.size() && ((raw[i] >= '0' && raw[i] <= '9') || raw[i] == ';' || raw[i] == '?')) ++i;
                if (i == raw.size()) { malformed = true; break; }
                ++i;
            } else { malformed = true; break; }
        } else if (byte == '\r') {
        } else if (byte == '\n') {
            end_line();
        } else if (byte == '\b') {
            if (output_seen) { malformed = true; break; }
            literal = false;
        } else {
            if (byte < 0x20 || byte == 0x7f) { malformed = true; break; }
            line.push_back(static_cast<char>(byte));
        }
    }
    std::string escaped;
    for (const char ch : line) {
        if (escaped.size() > 160) { escaped += "..."; break; }
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20 && byte < 0x7f && ch != '"' && ch != '\\') escaped.push_back(ch);
        else {
            char buffer[8]{};
            std::snprintf(buffer, sizeof(buffer), "\\x%02x", byte);
            escaped += buffer;
        }
    }
    // The window itself is kept as hex so a mismatch can be re-derived offline
    // without re-running the scenario (#80).
    std::string window_hex;
    for (const char ch : raw) {
        char buffer[4]{};
        std::snprintf(buffer, sizeof(buffer), "%02x", static_cast<unsigned char>(ch));
        window_hex += buffer;
        if (window_hex.size() > 2048) { window_hex += "..."; break; }
    }
    std::ofstream progress(directory/"shells-transport-probe.jsonl", std::ios::binary | std::ios::app);
    progress << "{\"schema\":\"termforge-local-shell-transport-probe-v1\",\"role\":\"" << role
             << "\",\"marker\":\"" << marker
             << "\",\"window_bytes\":" << raw.size()
             << ",\"window_hex\":\"" << window_hex
             << "\",\"output_seen\":" << (output_seen ? "true" : "false")
             << ",\"prompt_seen\":" << (prompt_seen ? "true" : "false")
             << ",\"malformed\":" << (malformed ? "true" : "false")
             << ",\"last_line\":\"" << escaped << "\"}\n";
    progress.close();
}

} // namespace tf_shell_test
