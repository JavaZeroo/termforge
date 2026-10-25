#include "shell_launch.hpp"
#include <windows.h>
#include <cctype>
#include <stdexcept>

namespace tf_shell {
namespace {
std::wstring system_directory() {
    wchar_t folder[MAX_PATH]{};
    const UINT count = GetSystemDirectoryW(folder, MAX_PATH);
    if (!count || count >= MAX_PATH) throw std::runtime_error("system directory failed");
    return folder;
}

std::wstring environment_directory(const wchar_t* name) {
    wchar_t folder[32768]{};
    const DWORD count = GetEnvironmentVariableW(name, folder, 32768);
    if (!count || count >= 32768) throw std::runtime_error("program files directory failed");
    return folder;
}
}

ShellLaunch cmd() {
    const auto executable = system_directory() + L"\\cmd.exe";
    const auto chcp = system_directory() + L"\\chcp.com";
    return {L"cmd", executable, L"/d /s /q /k \"\"" + chcp + L"\" 65001>nul&prompt TF$G&cls\"", {}};
}

ShellLaunch powershell(bool core) {
    const auto executable = core
        ? environment_directory(L"ProgramFiles") + L"\\PowerShell\\7\\pwsh.exe"
        : system_directory() + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    return {core ? L"pwsh" : L"powershell", executable,
        L"-NoLogo -NoProfile -ExecutionPolicy Bypass -NoExit -Command \""
        L"[Console]::InputEncoding=[System.Text.UTF8Encoding]::new($false);"
        L"[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false);"
        L"$OutputEncoding=[System.Text.UTF8Encoding]::new($false);"
        L"function prompt { 'TF> ' }\"", {}, false, L"& "};
}

ShellLaunch wsl(const std::wstring& distribution, const std::wstring& working_directory) {
    if (distribution.empty()) throw std::runtime_error("WSL distribution is required");
    for (const wchar_t c : distribution) {
        const bool alpha_numeric = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
        if (!alpha_numeric && c != L'_' && c != L'-' && c != L'.')
            throw std::runtime_error("WSL distribution contains unsupported characters");
    }
    if (working_directory.empty()) throw std::runtime_error("WSL working directory is required");
    if (working_directory.find(L'"') != std::wstring::npos)
        throw std::runtime_error("WSL working directory cannot contain quotes");
    return {L"wsl-" + distribution, system_directory() + L"\\wsl.exe",
        L"--cd \"" + working_directory + L"\" -d " + distribution +
        L" -e sh -c \"export PS1='TF> '; export LANG=C.UTF-8; exec sh\"", working_directory, true};
}

ShellLaunch make_launch(const LaunchDefinition& definition) {
    ShellLaunch launch;
    if (definition.kind == L"cmd") launch = cmd();
    else if (definition.kind == L"powershell") launch = powershell(false);
    else if (definition.kind == L"pwsh") launch = powershell(true);
    else if (definition.kind == L"wsl") return wsl(definition.distribution, definition.working_directory);
    else throw std::invalid_argument("unsupported shell launch kind");
    if (!definition.working_directory.empty()) launch.working_directory = definition.working_directory;
    return launch;
}

std::optional<LaunchDefinition> known_launch(const ShellLaunch& launch) {
    if (launch.id.empty() && launch.executable.empty() && launch.arguments.empty() &&
        launch.command_prefix.empty() && launch.working_directory.empty() && !launch.wsl)
        return LaunchDefinition{};
    const auto equivalent = [](const ShellLaunch& a, const ShellLaunch& b) {
        return a.id == b.id && a.executable == b.executable && a.arguments == b.arguments &&
            a.command_prefix == b.command_prefix && a.working_directory == b.working_directory && a.wsl == b.wsl;
    };
    for (const auto& kind : {L"cmd", L"powershell", L"pwsh"}) {
        auto expected = make_launch({kind, {}, launch.working_directory});
        if (equivalent(launch, expected)) return LaunchDefinition{kind, {}, launch.working_directory};
    }
    if (!launch.wsl || launch.id.rfind(L"wsl-", 0) != 0) return std::nullopt;
    try {
        const auto distribution = launch.id.substr(4);
        if (!equivalent(launch, wsl(distribution, launch.working_directory))) return std::nullopt;
        return LaunchDefinition{L"wsl", distribution, launch.working_directory};
    } catch (const std::runtime_error&) { return std::nullopt; }
}

std::wstring wsl_path(const std::wstring& windows_path) {
    if (windows_path.size() < 3 || windows_path[1] != L':' || windows_path[2] != L'\\')
        throw std::runtime_error("WSL path requires an absolute Windows drive path");
    std::wstring result = L"/mnt/";
    result += static_cast<wchar_t>(std::tolower(windows_path[0]));
    result += windows_path.substr(2);
    for (auto& c : result) if (c == L'\\') c = L'/';
    return result;
}
}
