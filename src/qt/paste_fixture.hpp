// Test-only child that records received input bytes unchanged. It never
// interprets or executes pasted text.
#pragma once
#include "../../experiments/conpty/session.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace termforge::qt::test {

inline int paste_child(const std::wstring& path) {
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("paste child requires its owned Session Job");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(paste child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        tf_conpty::fail("SetConsoleMode(paste child)");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) tf_conpty::fail("paste child input file");
    const char ready[] = "PASTE_READY\r\n";
    DWORD written = 0;
    if (!WriteFile(output.get(), ready, sizeof(ready) - 1, &written, nullptr) ||
        written != sizeof(ready) - 1)
        tf_conpty::fail("WriteFile(paste child prompt)");
    for (;;) {
        char byte = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &byte, 1, &count, nullptr) || count != 1)
            tf_conpty::fail("ReadFile(paste child input)");
        if (static_cast<unsigned char>(byte) == 0x03) {
            file.flush();
            return 37;
        }
        file.put(byte);
        file.flush();
    }
}

} // namespace termforge::qt::test
