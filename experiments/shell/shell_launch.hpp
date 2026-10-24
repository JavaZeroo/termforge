#pragma once
#include <string>

namespace tf_shell {
struct ShellLaunch {
    std::wstring id;
    std::wstring executable;
    std::wstring arguments;
    std::wstring working_directory;
    bool wsl = false;
    std::wstring command_prefix;
};

ShellLaunch cmd();
ShellLaunch powershell(bool core);
ShellLaunch wsl(const std::wstring& distribution, const std::wstring& working_directory);
std::wstring wsl_path(const std::wstring& windows_path);
}
