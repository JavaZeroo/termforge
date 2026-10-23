// Fixed helper endpoint. Its process/Job can be killed if another application
// blocks GetClipboardData with delayed rendering; it never owns the UI thread.
#include "clipboard.hpp"
#include "clipboard_worker.hpp"
#include <array>

static void emit(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, static_cast<DWORD>((std::min)(size, size_t{65536})), &written, nullptr) || !written)
            throw std::runtime_error("clipboard helper output closed");
        bytes += written; size -= written;
    }
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring(argv[1]) != L"--read") return 2;
    DWORD error = ERROR_SUCCESS; std::u16string text;
    try {
        text = tf_shell::read_unicode_clipboard(nullptr);
        for (size_t i = 0; i < text.size(); ++i) {
            const char16_t c = text[i];
            if (c >= 0xd800 && c <= 0xdbff) {
                if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) { error = ERROR_NO_UNICODE_TRANSLATION; break; }
            } else if (c >= 0xdc00 && c <= 0xdfff) { error = ERROR_NO_UNICODE_TRANSLATION; break; }
        }
    } catch (const tf_shell::ClipboardError& failure) { error = failure.code ? failure.code : ERROR_INVALID_DATA; }
      catch (const std::length_error&) { error = ERROR_BUFFER_OVERFLOW; }
      catch (...) { error = ERROR_INVALID_DATA; }
    if (error) text.clear();
    static_assert(sizeof(char16_t) == 2 && sizeof(uint32_t) == 4, "Windows clipboard wire sizes");
    const uint32_t header[] = {tf_shell::clipboard_wire_magic, tf_shell::clipboard_wire_version, error, static_cast<uint32_t>(text.size())};
    try { emit(header, sizeof(header)); if (!text.empty()) emit(text.data(), text.size() * 2); }
    catch (...) { return 3; }
    return 0;
}
