// Bounded CF_UNICODETEXT allocation parsing, independent of the system API.
#pragma once
#include "paste.hpp"

namespace tf_shell {
inline size_t clipboard_text_scan_limit(size_t allocation_bytes, size_t max_units) {
    if (max_units > PastePolicy::hard_utf16_units)
        throw std::length_error("clipboard limit exceeds hard bound");
    // GlobalAlloc may round a request. Limit both the allocation before locking
    // it and the later character scan; allocator slack never expands the text cap.
    constexpr size_t allocation_slack = 32;
    if (!allocation_bytes || allocation_bytes > (max_units + 1) * sizeof(char16_t) + allocation_slack)
        throw std::length_error("clipboard allocation exceeds limit");
    return (std::min)(allocation_bytes / sizeof(char16_t), max_units + 1);
}
inline std::u16string copy_bounded_clipboard_text(const char16_t* text, size_t allocation_bytes,
                                                 size_t max_units = PastePolicy::hard_utf16_units) {
    const size_t scan_limit = clipboard_text_scan_limit(allocation_bytes, max_units);
    if (!text) throw std::invalid_argument("clipboard text is null");
    size_t length = 0;
    while (length < scan_limit && text[length]) ++length;
    if (length == scan_limit) throw std::length_error("clipboard text lacks bounded terminator");
    return std::u16string(text, length);
}
} // namespace tf_shell
