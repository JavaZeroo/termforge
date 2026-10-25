#pragma once
#include <optional>
#include <string>

namespace tf_shell {
struct LaunchDefinition {
    std::wstring kind = L"cmd", distribution, working_directory;
};

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
ShellLaunch make_launch(const LaunchDefinition& definition);
std::optional<LaunchDefinition> known_launch(const ShellLaunch& launch);
}
