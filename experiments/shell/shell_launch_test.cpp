#include "shell_launch.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const auto same_launch = [](const tf_shell::ShellLaunch& a, const tf_shell::ShellLaunch& b) {
            return a.id == b.id && a.executable == b.executable && a.arguments == b.arguments &&
                a.command_prefix == b.command_prefix && a.working_directory == b.working_directory && a.wsl == b.wsl;
        };
        const auto expect_round_trip = [&](const tf_shell::ShellLaunch& expected, const std::wstring& kind,
                                          const std::wstring& distribution, const std::wstring& directory) {
            const auto definition = tf_shell::known_launch(expected);
            require(definition && definition->kind == kind && definition->distribution == distribution &&
                    definition->working_directory == directory, "known launch was not recovered exactly");
            require(same_launch(tf_shell::make_launch(*definition), expected), "launch factory round trip changed");
        };
        const auto cmd = tf_shell::cmd();
        require(cmd.id == L"cmd" && cmd.arguments.find(L"prompt TF$G") != std::wstring::npos,
                "default cmd launch changed");
        expect_round_trip(cmd, L"cmd", {}, {});
        const auto default_definition = tf_shell::known_launch(tf_shell::ShellLaunch{});
        require(default_definition && default_definition->kind == L"cmd" &&
                same_launch(tf_shell::make_launch(*default_definition), cmd), "empty launch did not select canonical cmd");
        auto custom_directory = cmd;
        custom_directory.working_directory = L"D:\\work dir\\project";
        expect_round_trip(custom_directory, L"cmd", {}, custom_directory.working_directory);
        const auto ps51 = tf_shell::powershell(false);
        const auto ps7 = tf_shell::powershell(true);
        require(ps51.executable.find(L"WindowsPowerShell") != std::wstring::npos &&
                ps7.executable.find(L"PowerShell\\7\\pwsh.exe") != std::wstring::npos,
                "PowerShell executable selection failed");
        require(ps51.arguments.find(L"-NoProfile") != std::wstring::npos &&
                ps51.arguments.find(L"function prompt { 'TF> ' }") != std::wstring::npos &&
                ps51.command_prefix == L"& " && ps7.command_prefix == L"& ",
                "PowerShell isolated prompt or native command contract missing");
        expect_round_trip(ps51, L"powershell", {}, {});
        expect_round_trip(ps7, L"pwsh", {}, {});
        const auto wsl = tf_shell::wsl(L"Ubuntu", L"D:\\work");
        require(wsl.arguments.find(L"--cd \"D:\\work\"") == 0 &&
                wsl.arguments.find(L"-d Ubuntu") != std::wstring::npos &&
                wsl.working_directory == L"D:\\work" && wsl.wsl, "WSL distribution or working directory contract failed");
        expect_round_trip(wsl, L"wsl", L"Ubuntu", L"D:\\work");
        expect_round_trip(tf_shell::wsl(L"Ubuntu-22.04", L"D:\\work dir"), L"wsl", L"Ubuntu-22.04", L"D:\\work dir");
        bool quoted_directory = false;
        try { tf_shell::wsl(L"Ubuntu", L"D:\\work\""); } catch (const std::runtime_error&) { quoted_directory = true; }
        require(quoted_directory, "quoted WSL working directory was accepted");
        require(tf_shell::wsl_path(L"D:\\work\\shell_size_client.exe") == L"/mnt/d/work/shell_size_client.exe",
                "WSL helper path conversion failed");
        bool bad_path = false;
        try { tf_shell::wsl_path(L"relative\\path"); } catch (const std::runtime_error&) { bad_path = true; }
        require(bad_path, "relative WSL helper path was accepted");
        for (const auto& invalid : {std::wstring(), std::wstring(L"Ubuntu;whoami"), std::wstring(L"Ubuntu WSL")}) {
            bool rejected = false;
            try { tf_shell::wsl(invalid, L"D:\\work"); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "unsafe WSL distribution was accepted");
        }
        for (const auto& definition : {tf_shell::LaunchDefinition{L"unknown"},
                                      tf_shell::LaunchDefinition{L"wsl", {}, L"D:\\work"},
                                      tf_shell::LaunchDefinition{L"wsl", L"Ubuntu", {}}}) {
            bool rejected = false;
            try { tf_shell::make_launch(definition); } catch (const std::exception&) { rejected = true; }
            require(rejected, "invalid launch definition was accepted");
        }
        auto custom = cmd;
        custom.arguments += L"&whoami";
        require(!tf_shell::known_launch(custom), "custom command arguments were accepted");
        custom = cmd; custom.command_prefix = L"custom ";
        require(!tf_shell::known_launch(custom), "custom command prefix was accepted");
        custom = cmd; custom.executable += L".custom";
        require(!tf_shell::known_launch(custom), "custom executable was accepted");
        custom = cmd; custom.id = L"wsl-custom"; custom.wsl = true;
        require(!tf_shell::known_launch(custom), "custom launch was treated as WSL");
        require(!tf_shell::known_launch({{}, {}, L"/k whoami"}), "partial command-only launch was accepted");
        custom = wsl; custom.arguments = L"-e whoami";
        require(!tf_shell::known_launch(custom), "custom WSL arguments were accepted");
        require(!tf_shell::known_launch(tf_shell::ShellLaunch{L"wsl-", wsl.executable, wsl.arguments,
            wsl.working_directory, true}), "missing WSL distribution was accepted");
        std::cout << "local shell launch contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
