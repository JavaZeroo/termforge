// Test-only real child for formal Qt history browsing. It writes a bounded
// numbered log, later appends more lines without any host input, and accepts
// single-character control commands through its ConPTY input.
#pragma once
#include "../../experiments/conpty/session.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace termforge::qt::test {

inline void history_child_write(HANDLE output, std::string_view bytes) {
    DWORD written = 0;
    if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size())
        tf_conpty::fail("WriteFile(history child output)");
}

inline void history_child_lines(HANDLE output, int first, int last, const char* suffix) {
    std::string batch;
    char line[64];
    for (int index = first; index <= last; ++index) {
        std::snprintf(line, sizeof(line), "TF_H_%03d\r\n", index);
        batch += line;
    }
    batch += suffix;
    history_child_write(output, batch);
}

inline int history_child() {
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("history child requires its owned Session Job");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(history child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN))
        tf_conpty::fail("SetConsoleMode(history child)");
    history_child_lines(output.get(), 1, 160, "TF_END\r\nTF>");
    // A bounded spontaneous append so a reading anchor can be checked during
    // real child output without host input returning to the bottom.
    Sleep(1500);
    history_child_lines(output.get(), 161, 180, "TF_END2\r\nTF>");
    for (;;) {
        char key = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &key, 1, &count, nullptr) || count != 1)
            tf_conpty::fail("ReadFile(history child input)");
        if (key == 'q') return 37;
        if (key == 'm') {
            history_child_lines(output.get(), 181, 220, "TF_END3\r\nTF>");
            continue;
        }
        if (key == '3') {
            history_child_write(output.get(), "\x1b[3JTF_ED3\r\nTF>");
            continue;
        }
        if (key == 'r') {
            history_child_write(output.get(), "\x1b" "cTF_RIS\r\nTF>");
            continue;
        }
    }
}

} // namespace termforge::qt::test
