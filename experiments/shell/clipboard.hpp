// CF_UNICODETEXT access. Clipboard ownership follows the calling window station.
#pragma once
#include "paste.hpp"
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
    explicit ClipboardAccess(HWND owner) {
        if (!OpenClipboard(owner)) throw ClipboardError("OpenClipboard", GetLastError());
    }
    ~ClipboardAccess() { CloseClipboard(); }
    ClipboardAccess(const ClipboardAccess&) = delete;
};
inline std::u16string read_unicode_clipboard(HWND owner, size_t max_units = PastePolicy::hard_utf16_units) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows UTF-16 expected");
    if (max_units > PastePolicy::hard_utf16_units) throw std::length_error("clipboard limit exceeds hard bound");
    ClipboardAccess clipboard(owner);
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) throw ClipboardError("Unicode text unavailable", ERROR_NOT_FOUND);
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (!memory) throw ClipboardError("GetClipboardData", GetLastError());
    const SIZE_T allocation = GlobalSize(memory);
    // Check the system allocation BEFORE locking, scanning, or copying. Small
    // allocator rounding is permitted, but can never make the copy unbounded.
    constexpr size_t allocation_slack = 32;
    if (!allocation || allocation > (max_units + 1) * sizeof(char16_t) + allocation_slack)
        throw std::length_error("clipboard allocation exceeds limit");
    const auto* text = static_cast<const char16_t*>(GlobalLock(memory));
    if (!text) throw ClipboardError("GlobalLock", GetLastError());
    struct Unlock { HANDLE memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
    const size_t scan_limit = (std::min)(static_cast<size_t>(allocation / sizeof(char16_t)), max_units + 1);
    size_t length = 0;
    while (length < scan_limit && text[length]) ++length;
    if (length == scan_limit) throw std::length_error("clipboard text lacks bounded terminator");
    return std::u16string(text, length);
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
    ClipboardAccess clipboard(owner);
    if (!EmptyClipboard()) throw ClipboardError("EmptyClipboard", GetLastError());
    if (!SetClipboardData(CF_UNICODETEXT, memory)) throw ClipboardError("SetClipboardData", GetLastError());
    allocation.value = nullptr; // The window station owns the allocation now.
}
} // namespace tf_shell
