// Test-only real child for Qt selection/source-migration coverage. It writes a
// fixed synthetic history with one soft-wrapped line and accepts small commands
// for unrelated appends, eviction, ED3 and RIS.
#pragma once
#include "../../experiments/conpty/session.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace termforge::qt::test {

inline const std::string kSelectionSoftLine =
    "SEL_SOFT_" + std::string(70, 'A') + "MIDDLE" + std::string(70, 'B') + "_END";
inline const std::string kSelectionHardLine = "SEL_HARD_LINE";

inline void selection_write(HANDLE output, std::string_view bytes) {
    DWORD written = 0;
    if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size())
        tf_conpty::fail("WriteFile(selection child output)");
}

inline void selection_numbered(HANDLE output, int first, int last) {
    std::string batch;
    char line[64];
    for (int index = first; index <= last; ++index) {
        std::snprintf(line, sizeof(line), "SEL_N_%03d\r\n", index);
        batch += line;
    }
    selection_write(output, batch);
}

inline int selection_child() {
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("selection child requires its owned Session Job");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(selection child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN))
        tf_conpty::fail("SetConsoleMode(selection child)");
    selection_numbered(output.get(), 1, 120);
    selection_write(output.get(), "SEL_HEAD\r\n" + kSelectionSoftLine + "\r\n" +
        kSelectionHardLine + "\r\nSEL_TAIL\r\n");
    selection_numbered(output.get(), 126, 165);
    selection_write(output.get(), "SEL>");
    for (;;) {
        char key = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &key, 1, &count, nullptr) || count != 1)
            tf_conpty::fail("ReadFile(selection child input)");
        if (key == 'q') return 37;
        if (key == 'o') {
            selection_numbered(output.get(), 121, 125);
            selection_write(output.get(), "SEL_APPEND\r\nSEL>");
            continue;
        }
        if (key == 'a') {
            selection_numbered(output.get(), 126, 260);
            selection_write(output.get(), "SEL_EVICT\r\nSEL>");
            continue;
        }
        if (key == '3') {
            selection_write(output.get(), "\x1b[3JSEL_ED3\r\nSEL>");
            continue;
        }
        if (key == 'r') {
            selection_write(output.get(), "\x1b" "cSEL_RIS\r\nSEL>");
            continue;
        }
    }
}

} // namespace termforge::qt::test
