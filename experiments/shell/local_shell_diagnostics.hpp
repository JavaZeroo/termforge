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
} // namespace tf_shell_test
