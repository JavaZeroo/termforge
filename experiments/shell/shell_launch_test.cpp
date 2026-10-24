#include "shell_launch.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const auto cmd = tf_shell::cmd();
        require(cmd.id == L"cmd" && cmd.arguments.find(L"prompt TF$G") != std::wstring::npos,
                "default cmd launch changed");
        const auto ps51 = tf_shell::powershell(false);
        const auto ps7 = tf_shell::powershell(true);
        require(ps51.executable.find(L"WindowsPowerShell") != std::wstring::npos &&
                ps7.executable.find(L"PowerShell\\7\\pwsh.exe") != std::wstring::npos,
                "PowerShell executable selection failed");
        require(ps51.arguments.find(L"-NoProfile") != std::wstring::npos &&
                ps51.arguments.find(L"function prompt { 'TF> ' }") != std::wstring::npos &&
                ps51.command_prefix == L"& " && ps7.command_prefix == L"& ",
                "PowerShell isolated prompt or native command contract missing");
        const auto wsl = tf_shell::wsl(L"Ubuntu", L"D:\\work");
        require(wsl.arguments.find(L"--cd \"D:\\work\"") == 0 &&
                wsl.arguments.find(L"-d Ubuntu") != std::wstring::npos &&
                wsl.working_directory == L"D:\\work" && wsl.wsl, "WSL distribution or working directory contract failed");
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
        std::cout << "local shell launch contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
