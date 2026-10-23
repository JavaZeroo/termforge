// Original bounded paste preparation and transfer. MIT; see repository LICENSE.
#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tf_shell {
// Application policy, not a claim about WindTerm defaults. No bracketed-paste
// wrapper is added: that requires terminal mode negotiation in a later slice.
enum class Newlines { carriage_return, line_feed, preserve };
enum class InvalidUtf16 { reject, replace };
struct PastePolicy {
    static constexpr size_t hard_utf16_units = 1024 * 1024;
    static constexpr size_t hard_utf8_bytes = 4 * 1024 * 1024;
    size_t max_utf16_units = hard_utf16_units;
    size_t max_utf8_bytes = hard_utf8_bytes;
    Newlines newlines = Newlines::carriage_return;
    InvalidUtf16 invalid_utf16 = InvalidUtf16::reject;
    bool reject_controls = true; // TAB and CR/LF are allowed; C0/C1/DEL are not.
    bool confirm_multiline = true; // Any newline, including a final one, executes input.
};
struct PreparedPaste {
    std::string bytes;
    size_t source_units = 0, newline_count = 0, replacements = 0;
    bool confirmation_required = false;
};
inline void validate_policy(const PastePolicy& p) {
    if (p.max_utf16_units > PastePolicy::hard_utf16_units ||
        p.max_utf8_bytes > PastePolicy::hard_utf8_bytes)
        throw std::invalid_argument("paste policy exceeds hard memory limit");
}
inline PreparedPaste prepare_paste(std::u16string_view source, const PastePolicy& p = {}) {
    validate_policy(p);
    if (source.size() > p.max_utf16_units) throw std::length_error("clipboard text exceeds UTF-16 limit");
    PreparedPaste result; result.source_units = source.size();
    // Reserve the bounded worst case once, avoiding repeated reallocations. A
    // clipboard read plus preparation owns at most 2 MiB + 4 MiB of text.
    result.bytes.reserve((std::min)(p.max_utf8_bytes, source.size() * 3));
    auto append = [&](uint32_t cp) {
        char bytes[4]; size_t count = 0;
        if (cp < 0x80) bytes[count++] = static_cast<char>(cp);
        else if (cp < 0x800) {
            bytes[count++] = static_cast<char>(0xc0 | (cp >> 6));
            bytes[count++] = static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            bytes[count++] = static_cast<char>(0xe0 | (cp >> 12));
            bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            bytes[count++] = static_cast<char>(0x80 | (cp & 63));
        } else {
            bytes[count++] = static_cast<char>(0xf0 | (cp >> 18));
            bytes[count++] = static_cast<char>(0x80 | ((cp >> 12) & 63));
            bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            bytes[count++] = static_cast<char>(0x80 | (cp & 63));
        }
        if (count > p.max_utf8_bytes - result.bytes.size())
            throw std::length_error("paste exceeds UTF-8 limit");
        result.bytes.append(bytes, count);
    };
    for (size_t i = 0; i < source.size(); ++i) {
        uint32_t cp = source[i];
        if (cp == '\r' || cp == '\n') {
            ++result.newline_count;
            const bool pair = cp == '\r' && i + 1 < source.size() && source[i + 1] == '\n';
            if (p.newlines == Newlines::preserve) {
                append(cp); if (pair) { append('\n'); ++i; }
            } else {
                append(p.newlines == Newlines::carriage_return ? '\r' : '\n');
                if (pair) ++i;
            }
            continue;
        }
        if (cp >= 0xd800 && cp <= 0xdfff) {
            if (cp <= 0xdbff && i + 1 < source.size() && source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (source[++i] - 0xdc00);
            } else {
                if (p.invalid_utf16 == InvalidUtf16::reject) throw std::invalid_argument("clipboard has invalid UTF-16");
                cp = 0xfffd; ++result.replacements;
            }
        }
        // Embedded NUL cannot be represented by CF_UNICODETEXT. Reject it even
        // in permissive-control mode instead of silently truncating a source.
        if (!cp || (p.reject_controls && ((cp < 32 && cp != '\t') || (cp >= 0x7f && cp <= 0x9f))))
            throw std::invalid_argument("clipboard contains unsupported control character");
        append(cp);
    }
    result.confirmation_required = p.confirm_multiline && result.newline_count != 0;
    return result;
}

enum class PasteState { ready, sending, cancelling, completed, cancelled, failed };
inline bool paste_terminal(PasteState state) {
    return state == PasteState::completed || state == PasteState::cancelled || state == PasteState::failed;
}
struct PasteProgress {
    uint64_t target = 0;
    size_t total = 0, accepted = 0, sent = 0, in_flight = 0;
    PasteState state = PasteState::ready;
    std::string error;
    size_t retained_capacity = 0;
};
class PasteTransfer {
    mutable std::mutex mutex_;
    std::string bytes_;
    PasteProgress progress_;
    size_t chunk_written_ = 0;
    bool writer_claimed_ = false;
public:
    static constexpr size_t chunk_limit = 1024;
    PasteTransfer(uint64_t target, PreparedPaste&& prepared, bool confirmed = false) {
        if (prepared.bytes.size() > PastePolicy::hard_utf8_bytes ||
            prepared.bytes.capacity() > PastePolicy::hard_utf8_bytes + 32)
            throw std::length_error("paste exceeds hard memory limit");
        if (prepared.confirmation_required && !confirmed)
            throw std::invalid_argument("multiline paste requires confirmation");
        bytes_ = std::move(prepared.bytes);
        progress_.target = target; progress_.total = bytes_.size();
        progress_.retained_capacity = bytes_.capacity();
        if (bytes_.empty()) progress_.state = PasteState::completed;
    }
    PasteProgress progress() const { std::lock_guard<std::mutex> lock(mutex_); return progress_; }
    bool claim_writer() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (writer_claimed_) return false;
        writer_claimed_ = true; return true;
    }
    // Exactly one writer owns the returned chunk. Chunk boundaries preserve
    // Unicode scalar boundaries, making normal cancellation a decodable prefix.
    std::string take_chunk() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paste_terminal(progress_.state) || progress_.state == PasteState::cancelling) return {};
        if (progress_.in_flight) throw std::logic_error("paste writer already has an in-flight chunk");
        size_t count = (std::min)(chunk_limit, bytes_.size() - progress_.accepted);
        if (progress_.accepted + count < bytes_.size()) {
            while (count && (static_cast<unsigned char>(bytes_[progress_.accepted + count]) & 0xc0) == 0x80) --count;
        }
        if (!count) throw std::logic_error("invalid paste chunk boundary");
        std::string chunk = bytes_.substr(progress_.accepted, count);
        progress_.accepted += count; progress_.in_flight = count;
        chunk_written_ = 0; progress_.state = PasteState::sending;
        return chunk;
    }
    // Called for each WriteFile byte count, not on enqueue or child echo. This
    // is transport submission, not an assertion that a child has consumed it.
    void acknowledge_written(size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count > progress_.in_flight - chunk_written_) throw std::logic_error("invalid paste write acknowledgement");
        chunk_written_ += count; progress_.sent += count;
    }
    void finish_chunk() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!progress_.in_flight || chunk_written_ != progress_.in_flight)
            throw std::logic_error("paste chunk did not finish writing");
        progress_.in_flight = 0; chunk_written_ = 0;
        if (progress_.state == PasteState::cancelling) progress_.state = PasteState::cancelled;
        else if (progress_.sent == progress_.total) progress_.state = PasteState::completed;
        if (paste_terminal(progress_.state)) { std::string{}.swap(bytes_); progress_.retained_capacity = bytes_.capacity(); }
    }
    void cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!paste_terminal(progress_.state))
            progress_.state = progress_.in_flight ? PasteState::cancelling : PasteState::cancelled;
        if (progress_.state == PasteState::cancelled) { std::string{}.swap(bytes_); progress_.retained_capacity = bytes_.capacity(); }
    }
    // Writer-only, after the failed/cancelled OS write has returned. UI callers
    // use cancel(), which never invalidates an outstanding acknowledgement.
    void fail(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paste_terminal(progress_.state)) return;
        progress_.state = PasteState::failed; progress_.error = error;
        progress_.in_flight = 0; chunk_written_ = 0;
        std::string{}.swap(bytes_); progress_.retained_capacity = bytes_.capacity();
    }
};
} // namespace tf_shell
