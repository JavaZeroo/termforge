// Original bounded keyboard encoding for the normal VT input mode.
#pragma once
#include <cstdint>
#include <string>

namespace tf_shell {
struct KeyModifiers {
    bool shift = false, control = false, alt = false, alt_graph = false;
};
struct KeyEncoding {
    bool handled = false;
    std::string bytes;
    // The message loop must skip TranslateMessage for these handled keys.
    bool skip_translate = false;
};
class KeyboardEncoder {
    char16_t high_ = 0;
    bool high_alt_ = false;
    static std::string scalar(uint32_t cp) {
        std::string out;
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 63))); }
        else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (cp & 63)));
        }
        return out;
    }
    static std::string encoded(uint32_t cp, bool alt) { return (alt ? "\x1b" : "") + scalar(cp); }
public:
    void reset() { high_ = 0; high_alt_ = false; }
    bool pending_surrogate() const { return high_ != 0; }
    std::string finish() {
        const auto result = high_ ? encoded(0xfffd, high_alt_) : std::string();
        reset(); return result;
    }
    // U+0000 is a real one-byte string. No null-terminated conversion occurs.
    std::string character(char16_t unit, bool alt = false) {
        std::string out;
        if (high_) {
            const auto previous = high_; const bool previous_alt = high_alt_; reset();
            if (unit >= 0xdc00 && unit <= 0xdfff)
                return encoded(0x10000 + ((previous - 0xd800) << 10) + (unit - 0xdc00), previous_alt);
            out = encoded(0xfffd, previous_alt);
        }
        if (unit >= 0xd800 && unit <= 0xdbff) { high_ = unit; high_alt_ = alt; return out; }
        if (unit >= 0xdc00 && unit <= 0xdfff) unit = 0xfffd;
        if (unit == u'\b') unit = 0x7f;
        return out + encoded(unit, alt);
    }
    // Win32 virtual-key values. Printable/control text stays in WM_CHAR;
    // application cursor/keypad modes remain a separate future contract.
    static KeyEncoding key_down(uint32_t key, KeyModifiers mods = {}) {
        if (mods.alt_graph || (mods.control && mods.alt)) return {};
        const int modifier = 1 + (mods.shift ? 1 : 0) + (mods.alt ? 2 : 0) + (mods.control ? 4 : 0);
        if (key == 0x09) { // Plain Tab is translated once, by WM_CHAR.
            if (mods.shift && !mods.control && !mods.alt) return {true, "\x1b[Z", true};
            return {};
        }
        if (key == 0x20 && mods.control && !mods.alt) return {true, std::string(1, '\0'), true};
        char final = 0;
        switch (key) {
        case 0x25: final = 'D'; break;
        case 0x26: final = 'A'; break;
        case 0x27: final = 'C'; break;
        case 0x28: final = 'B'; break;
        case 0x24: final = 'H'; break;
        case 0x23: final = 'F'; break;
        default: break;
        }
        if (final) return {true, modifier == 1 ? std::string("\x1b[") + final :
            std::string("\x1b[1;") + std::to_string(modifier) + final, false};
        int number = 0;
        switch (key) {
        case 0x2d: number = 2; break; // Insert
        case 0x2e: number = 3; break; // Delete
        case 0x21: number = 5; break; // PageUp
        case 0x22: number = 6; break; // PageDown
        default: return {};
        }
        return {true, "\x1b[" + std::to_string(number) +
            (modifier == 1 ? "" : ";" + std::to_string(modifier)) + "~", false};
    }
};
} // namespace tf_shell
