// Original bounded UTF-8 terminal experiment; see README for contract.
#pragma once
#include "screen.hpp"
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace termforge {
enum class ParseMode { strict, recovery };
struct RecoveryDiagnostics {
    uint64_t ignored_sequences = 0, ignored_controls = 0;
    uint64_t malformed_utf8 = 0, replaced_scalars = 0;
    uint64_t cancelled_sequences = 0, truncated_sequences = 0;
    uint64_t oversized_sequences = 0, resynchronized_sequences = 0;
    size_t max_sequence_bytes = 0, max_parameters = 0, max_utf8_bytes = 0;
};
class Parser {
    enum class State { ground, escape, escape_ignore, csi, csi_ignore, string_body, string_escape };
    State state = State::ground;
    const ParseMode mode;
    RecoveryDiagnostics stats;
    bool osc = false, private_csi = false;
    std::vector<int> params;
    size_t sequence_bytes = 0;
    uint32_t scalar = 0, minimum = 0;
    int remaining = 0;
    std::string encoded;
    // Bounded replies for implemented queries; drained by the host after each feed.
    std::vector<std::string> pending_replies;
    // Bit 0: DECCKM application cursor keys; bit 1: DECTCEM cursor visible.
    uint32_t mode_bits_ = 1u << 1;
    void dispatch(unsigned char final) {
        if (private_csi) {
            // DECCKM (?1) and DECTCEM (?25) only change terminal state the host
            // reads later; everything else private is still unsupported.
            if (params.size() == 1 && (params[0] == 1 || params[0] == 25) && (final == 'h' || final == 'l')) {
                const uint32_t bit = params[0] == 1 ? 1u << 0 : 1u << 1;
                if (final == 'h') mode_bits_ |= bit; else mode_bits_ &= ~bit;
                return;
            }
            if (params.size() == 1 && (params[0] == 1004 || params[0] == 9001) && (final == 'h' || final == 'l'))
                // Real ConPTY sessions request focus reporting (?1004) and Win32
                // input mode (?9001). Neither changes what this core draws or
                // sends, and the host has nothing to act on yet, so they are
                // accepted without being recorded.
                return;
            if (params.size() != 1 || (params[0] != 1049 && params[0] != 7) || (final != 'h' && final != 'l'))
                throw InputError("unsupported private CSI command");
            if (params[0] == 7) {
                screen.set_autowrap(final == 'h');
                return;
            }
            if (final == 'h') {
                // Re-entry clears only the active alternate frame; the saved
                // primary writing state must survive until a real return.
                if (screen.active_buffer() == ScreenBuffer::alternate) screen.invalidate_buffer_readers();
                screen.activate_buffer(ScreenBuffer::alternate);
                // Colour reset leaves other attributes owned by this frame.
                screen.style.fg = screen.style.bg = -1;
                screen.erase_display(2);
                screen.move(0, 0);
            } else screen.activate_buffer(ScreenBuffer::primary);
            return;
        }
        if (final == 'm') { screen.sgr(params); return; }
        if (params.size() > ((final == 'H' || final == 'f' || final == 'r') ? 2u : 1u))
            throw InputError("too many CSI parameters");
        const int n = params[0] == 0 ? 1 : params[0];
        switch (final) {
        case 'A': screen.move(screen.row - n, screen.column); break;
        case 'B': screen.move(screen.row + n, screen.column); break;
        case 'C': screen.move(screen.row, screen.column + n); break;
        case 'D': screen.move(screen.row, screen.column - n); break;
        case 'G': screen.move(screen.row, n - 1); break;
        case 'd': screen.move(n - 1, screen.column); break;
        case 'H': case 'f': screen.move(n - 1, params.size() < 2 || params[1] == 0 ? 0 : params[1] - 1); break;
        case 'J': screen.erase_display(params[0]); break;
        case 'K': screen.erase_line(params[0]); break;
        case 'L': screen.insert_lines(n); break;
        case 'r':
            screen.set_scroll_region(params[0] == 0 ? 0 : params[0] - 1,
                                     params.size() < 2 || params[1] == 0 ? -1 : params[1] - 1);
            break;
        case 'M': screen.delete_lines(n); break;
        case 't':
            // Window manipulation (XTWINOPS) is advisory: ConPTY emits CSI 1 t
            // around a native resize. Acting on it would fight the host's own
            // geometry, so it is accepted and ignored like other terminals do
            // for operations they do not implement.
            return;
        case 'X': screen.erase_characters(n); break;
        case '@': screen.insert_characters(n); break;
        case 'P': screen.delete_characters(n); break;
        case 'n':
            // Device status report 6 reports the 1-based cursor position. Other
            // reports are not implemented and stay unsupported rather than faked.
            if (params.size() == 1 && params[0] == 6) {
                pending_replies.push_back("\x1b[" + std::to_string(screen.row + 1) + ";" +
                                          std::to_string(screen.column + 1) + "R");
                return;
            }
            throw InputError("unsupported CSI command");
        default: throw InputError("unsupported CSI command");
        }
    }
    void feed_strict(std::string_view bytes) {
        for (unsigned char c : bytes) {
            if (remaining) {
                if ((c & 0xC0) != 0x80) throw std::runtime_error("invalid UTF-8 continuation");
                encoded += static_cast<char>(c); scalar = (scalar << 6) | (c & 0x3F);
                if (--remaining == 0) {
                    if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF))
                        throw std::runtime_error("invalid UTF-8 scalar");
                    screen.put(scalar, encoded);
                }
                continue;
            }
            if (state == State::escape) {
                // RIS resets the parser-owned modes too, so it is handled here
                // rather than inside Screen.
                if (c == 'c') {
                    screen.reset();
                    mode_bits_ = 1u << 1;
                    state = State::ground;
                    continue;
                }
                if (c != '[') throw std::runtime_error("unsupported escape");
                state = State::csi; params.assign(1, 0); sequence_bytes = 0; private_csi = false; continue;
            }
            if (state == State::csi) {
                if (++sequence_bytes > 32) throw std::runtime_error("CSI exceeds limit");
                if (c == '?' && sequence_bytes == 1) { private_csi = true; continue; }
                if (c >= '0' && c <= '9') {
                    if (params.back() > 9999) throw std::runtime_error("CSI parameter exceeds limit");
                    params.back() = params.back() * 10 + c - '0';
                } else if (c == ';') {
                    if (params.size() == 16) throw std::runtime_error("too many CSI parameters");
                    params.push_back(0);
                } else { dispatch(c); state = State::ground; private_csi = false; }
                continue;
            }
            if (c >= 0x80) {
                encoded.assign(1, static_cast<char>(c));
                if (c >= 0xC2 && c <= 0xDF) { remaining = 1; minimum = 0x80; scalar = c & 0x1F; }
                else if (c >= 0xE0 && c <= 0xEF) { remaining = 2; minimum = 0x800; scalar = c & 0x0F; }
                else if (c >= 0xF0 && c <= 0xF4) { remaining = 3; minimum = 0x10000; scalar = c & 0x07; }
                else throw std::runtime_error("invalid UTF-8 leading byte");
            }
            else if (c >= 32 && c <= 126) screen.put(c, std::string(1, static_cast<char>(c)));
            else switch (c) {
            case 27: state = State::escape; break;
            case '\r': screen.move(screen.row, 0); break;
            case '\n': screen.newline(); break;
            case '\b': screen.move(screen.row, screen.column - 1); break;
            case '\t': screen.move(screen.row, (screen.column / 8 + 1) * 8); break;
            default: throw std::runtime_error("unsupported input byte");
            }
        }
    }
    static void count(uint64_t& value) {
        if (value != (std::numeric_limits<uint64_t>::max)()) ++value;
    }
    void reset_sequence() {
        state = State::ground; params.clear(); sequence_bytes = 0; private_csi = false;
    }
    void reset_utf8() { remaining = 0; encoded.clear(); scalar = minimum = 0; }
    void replacement() { screen.put(0xFFFD, "\xEF\xBF\xBD"); }
    void put_recovering(uint32_t cp, const std::string& text) {
        try { screen.put(cp, text); }
        catch (const InputError&) { count(stats.replaced_scalars); replacement(); }
    }
    void utf8_error() { count(stats.malformed_utf8); reset_utf8(); replacement(); }
    bool valid_continuation(unsigned char c) const {
        if ((c & 0xC0) != 0x80) return false;
        if (encoded.size() != 1) return true;
        // Unicode Table 3-7: reject impossible prefixes before consuming the
        // next byte so maximal-subpart replacement preserves valid successors.
        const auto lead = static_cast<unsigned char>(encoded[0]);
        return !((lead == 0xE0 && c < 0xA0) || (lead == 0xED && c > 0x9F) ||
                 (lead == 0xF0 && c < 0x90) || (lead == 0xF4 && c > 0x8F));
    }
    void note_sequence_byte(size_t limit) {
        if (sequence_bytes <= limit) {
            ++sequence_bytes;
            stats.max_sequence_bytes = (std::max)(stats.max_sequence_bytes, sequence_bytes);
            if (sequence_bytes > limit) count(stats.oversized_sequences);
        }
    }
    void ignore_sequence(State ignored) {
        if (state != State::escape_ignore && state != State::csi_ignore) count(stats.ignored_sequences);
        state = ignored; params.clear(); private_csi = false;
    }
    void feed_recovering(unsigned char c) {
        if (remaining) {
            if (valid_continuation(c)) {
                encoded += static_cast<char>(c); scalar = (scalar << 6) | (c & 0x3F);
                stats.max_utf8_bytes = (std::max)(stats.max_utf8_bytes, encoded.size());
                if (--remaining == 0) { put_recovering(scalar, encoded); reset_utf8(); }
                return;
            }
            utf8_error(); // Reprocess this byte; it can start valid text or ESC.
        }
        const bool in_string = state == State::string_body || state == State::string_escape;
        if (state != State::ground && (c == 0x18 || c == 0x1A)) {
            count(stats.cancelled_sequences); reset_sequence(); return;
        }
        if (in_string) {
            note_sequence_byte(4096);
            if ((osc && c == 7) || (state == State::string_escape && c == '\\')) reset_sequence();
            else state = c == 27 ? State::string_escape : State::string_body;
            return; // Never interpret a string body, even ESC followed by CSI.
        }
        if (c == 27) {
            if (state != State::ground) count(stats.resynchronized_sequences);
            reset_sequence(); state = State::escape; return;
        }
        if (state != State::ground) {
            note_sequence_byte(32);
            if (c < 32 || c == 127) { count(stats.ignored_controls); return; }
            if (state == State::escape || state == State::escape_ignore) {
                if (state == State::escape && c == '[') {
                    state = State::csi; sequence_bytes = 0; params.assign(1, 0);
                    stats.max_parameters = (std::max)(stats.max_parameters, params.size()); return;
                }
                if (state == State::escape && c == 'c') {
                    // RIS also works on the recovery path the product uses.
                    screen.reset();
                    mode_bits_ = 1u << 1;
                    reset_sequence();
                    return;
                }
                if (state == State::escape &&
                    (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_')) {
                    count(stats.ignored_sequences); osc = c == ']';
                    state = State::string_body; sequence_bytes = 0; return;
                }
                ignore_sequence(State::escape_ignore);
                if (c >= 0x30 && c <= 0x7E) reset_sequence();
                return;
            }
            if (sequence_bytes > 32) ignore_sequence(State::csi_ignore);
            if (c >= 0x40 && c <= 0x7E) {
                if (state == State::csi) {
                    try { dispatch(c); }
                    catch (const InputError&) { count(stats.ignored_sequences); }
                }
                reset_sequence(); return;
            }
            if (state == State::csi_ignore) return;
            if (c == '?' && sequence_bytes == 1) private_csi = true;
            else if (c >= '0' && c <= '9') {
                if (params.back() > 9999) ignore_sequence(State::csi_ignore);
                else params.back() = params.back() * 10 + c - '0';
            } else if (c == ';') {
                if (params.size() == 16) ignore_sequence(State::csi_ignore);
                else {
                    params.push_back(0);
                    stats.max_parameters = (std::max)(stats.max_parameters, params.size());
                }
            } else ignore_sequence(State::csi_ignore);
            return;
        }
        if (c >= 0x80) {
            if (c >= 0xC2 && c <= 0xDF) { remaining = 1; minimum = 0x80; scalar = c & 0x1F; }
            else if (c >= 0xE0 && c <= 0xEF) { remaining = 2; minimum = 0x800; scalar = c & 0x0F; }
            else if (c >= 0xF0 && c <= 0xF4) { remaining = 3; minimum = 0x10000; scalar = c & 0x07; }
            else { utf8_error(); return; }
            encoded.assign(1, static_cast<char>(c));
            stats.max_utf8_bytes = (std::max)(stats.max_utf8_bytes, encoded.size());
        } else if (c >= 32 && c <= 126) screen.put(c, std::string(1, static_cast<char>(c)));
        else switch (c) {
        case '\r': screen.move(screen.row, 0); break;
        case '\n': screen.newline(); break;
        case '\b': screen.move(screen.row, screen.column - 1); break;
        case '\t': screen.move(screen.row, (screen.column / 8 + 1) * 8); break;
        default: count(stats.ignored_controls); break;
        }
    }
public:
    Screen screen;
    Parser(int rows = 24, int columns = 80, size_t history = 100, ParseMode selected = ParseMode::strict)
        : mode(selected), screen(rows, columns, history) {}
    const RecoveryDiagnostics& diagnostics() const { return stats; }
    std::vector<std::string> take_replies() { return std::exchange(pending_replies, {}); }
    uint32_t mode_bits() const { return mode_bits_; }
    bool application_cursor_keys() const { return (mode_bits_ & (1u << 0)) != 0; }
    bool cursor_visible() const { return (mode_bits_ & (1u << 1)) != 0; }
    size_t buffered_input_bytes() const { return params.size() * sizeof(int) + encoded.size(); }
    void feed(std::string_view bytes) {
        if (mode == ParseMode::strict) feed_strict(bytes);
        else for (unsigned char c : bytes) feed_recovering(c);
    }
    void finish() {
        if (mode == ParseMode::strict) {
            if (state != State::ground || remaining) throw std::runtime_error("incomplete sequence at EOF");
            return;
        }
        if (remaining) { count(stats.truncated_sequences); utf8_error(); }
        if (state != State::ground) { count(stats.truncated_sequences); reset_sequence(); }
    }
};
} // namespace termforge
