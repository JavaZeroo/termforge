// Synthetic console client: query the OS, not the host's requested dimensions.
#include "../conpty/session.hpp"
#include <string>
int main() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(output, &info)) return 2;
    const auto text = "\r\nTF_SIZE_" + std::to_string(info.dwSize.X) + "x" + std::to_string(info.dwSize.Y) + "\r\n";
    DWORD written = 0;
    return WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size() ? 0 : 3;
}
