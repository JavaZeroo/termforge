// CF_UNICODETEXT access. Clipboard ownership follows the calling window station.
#pragma once
#include "clipboard_text.hpp"
#include <windows.h>
#include <cstring>

namespace tf_shell {
class ClipboardError : public std::runtime_error {
public:
    DWORD code;
    ClipboardError(const char* operation, DWORD value)
        : std::runtime_error(std::string(operation) + ": " + std::to_string(value)), code(value) {}
};
class ClipboardAccess {
public:
    // A hosted desktop can hold the clipboard for a few milliseconds at a time
    // (Explorer or a neighbouring test), which turns contention into a spurious
    // hard refusal. Reads and writes both retry briefly on ERROR_ACCESS_DENIED.
    // The copy-failure contract still holds: its negative fixture keeps the
    // clipboard owned for the whole attempt window, so the failure and its
    // "OpenClipboard: 5" status are unchanged, only reached a few ms later.
    explicit ClipboardAccess(HWND owner, int attempts, DWORD first_delay_ms) {
        for (int attempt = 0; attempt < attempts; ++attempt) {
            if (OpenClipboard(owner)) return;
            const DWORD error = GetLastError();
            if (error != ERROR_ACCESS_DENIED || attempt + 1 == attempts)
                throw ClipboardError("OpenClipboard", error);
            Sleep(first_delay_ms * static_cast<DWORD>(attempt + 1));
        }
        throw ClipboardError("OpenClipboard", GetLastError());
    }
    explicit ClipboardAccess(HWND owner, bool retry = false) : ClipboardAccess(owner, retry ? 5 : 1, 20) {}
    ~ClipboardAccess() { CloseClipboard(); }
    ClipboardAccess(const ClipboardAccess&) = delete;
};
inline std::u16string read_unicode_clipboard(HWND owner, size_t max_units = PastePolicy::hard_utf16_units) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows UTF-16 expected");
    if (max_units > PastePolicy::hard_utf16_units) throw std::length_error("clipboard limit exceeds hard bound");
    ClipboardAccess clipboard(owner, 5, 20);
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) throw ClipboardError("Unicode text unavailable", ERROR_NOT_FOUND);
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (!memory) throw ClipboardError("GetClipboardData", GetLastError());
    const SIZE_T allocation = GlobalSize(memory);
    // Validate BEFORE locking; the shared parser repeats the bound before scan.
    clipboard_text_scan_limit(static_cast<size_t>(allocation), max_units);
    const auto* text = static_cast<const char16_t*>(GlobalLock(memory));
    if (!text) throw ClipboardError("GlobalLock", GetLastError());
    struct Unlock { HANDLE memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
    return copy_bounded_clipboard_text(text, static_cast<size_t>(allocation), max_units);
}
inline void write_unicode_clipboard(HWND owner, std::u16string_view text) {
    if (!owner) throw std::invalid_argument("clipboard copy requires an owner window");
    if (text.size() > PastePolicy::hard_utf16_units) throw std::length_error("copy exceeds clipboard limit");
    if (text.find(u'\0') != std::u16string_view::npos) throw std::invalid_argument("copy contains embedded NUL");
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    if (!memory) throw ClipboardError("GlobalAlloc", GetLastError());
    struct Allocation { HGLOBAL value; ~Allocation() { if (value) GlobalFree(value); } } allocation{memory};
    auto* destination = static_cast<char16_t*>(GlobalLock(memory));
    if (!destination) throw ClipboardError("GlobalLock", GetLastError());
    if (!text.empty()) std::memcpy(destination, text.data(), text.size() * sizeof(char16_t));
    destination[text.size()] = 0;
    GlobalUnlock(memory);
    // Three short attempts (<=60 ms): enough for micro-contention, still far
    // shorter than the owned hold the copy-failure contract relies on.
    ClipboardAccess clipboard(owner, 3, 10);
    if (!EmptyClipboard()) throw ClipboardError("EmptyClipboard", GetLastError());
    if (!SetClipboardData(CF_UNICODETEXT, memory)) throw ClipboardError("SetClipboardData", GetLastError());
    allocation.value = nullptr; // The window station owns the allocation now.
}
} // namespace tf_shell
