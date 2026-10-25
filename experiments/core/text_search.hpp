// Original bounded literal search over the shared terminal text projection.
// Each work unit consumes one UTF-8 byte or loads/checks one bounded physical
// row; one rewind may also examine at most query_byte_limit preceding bytes'
// row metadata and prune at most result_hard_limit results. No entire text
// document is copied. Returned row values are bounded by Screen's 500-cell and
// 16-combining-mark limits; metadata is bounded by retained history + live rows.
#pragma once
#include "text_document.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace termforge {
enum class SearchStatus { idle, running, complete, capped, cancelled, invalidated, closed };
enum class SearchDirection { previous, next };
struct SearchOptions { bool case_sensitive = true; size_t result_limit = 1024; };
struct SearchStep {
    SearchStatus status = SearchStatus::idle;
    size_t work_used = 0, bytes_scanned = 0, rows_loaded = 0;
    bool accepted = false;
};
struct SearchMatch { TextRange range; };
struct SearchNavigation { SearchMatch match; size_t index = 0; bool wrapped = false; };
// Project policy: exact UTF-8 literal, optional ASCII-only case folding, no
// Unicode normalization, non-overlapping matches. Soft wraps join; other row
// boundaries are LF. Hits inside a combining cell cover the entire cell.
// notify_change() is required after mutations. It retains stable history
// progress; readers MUST live-validate raw snapshots with match(doc,index).
class TextSearch {
    struct ScannedRow { DocumentLineMetadata meta; size_t bytes = 0, prefix_before = 0; uint64_t successor_id = 0; };
    struct Origin { TextPosition begin, end; };
    uint64_t generation_ = 0, change_serial_ = 0, audit_serial_ = 0;
    SearchStatus status_ = SearchStatus::idle;
    SearchOptions options_;
    std::string query_;
    std::vector<size_t> prefix_;
    std::vector<Origin> origins_;
    size_t origin_head_ = 0, matched_ = 0;
    std::vector<SearchMatch> matches_;
    std::deque<ScannedRow> rows_;
    size_t audit_cursor_ = 0;
    bool audit_requested_ = false, loaded_ = false, boundary_done_ = false;
    bool audit_turn_ = true;
    bool at_end_ = false, cap_reached_ = false;
    uint64_t next_id_ = 0, emit_from_id_ = 0;
    DocumentLine current_;
    DocumentLineMetadata current_meta_;
    uint64_t current_successor_id_ = 0;
    size_t byte_ = 0, span_ = 0, current_prefix_before_ = 0;

    static bool same(const DocumentLineMetadata& a, const DocumentLineMetadata& b) {
        return a.id == b.id && a.version == b.version && a.columns == b.columns &&
               a.joins_next == b.joins_next && a.ending == b.ending;
    }
    static bool before(const TextPosition& a, const TextPosition& b) {
        return a.line_id < b.line_id || (a.line_id == b.line_id && a.column < b.column);
    }
    static bool equal(const TextPosition& a, const TextPosition& b) {
        return a.line_id == b.line_id && a.version == b.version && a.column == b.column;
    }
    static bool valid_utf8(const std::string& value) {
        uint32_t scalar = 0, minimum = 0; unsigned remaining = 0;
        for (const auto raw : value) {
            const auto c = static_cast<unsigned char>(raw);
            if (!remaining) {
                if (c < 0x80) continue;
                if (c >= 0xc2 && c <= 0xdf) { scalar = c & 0x1f; minimum = 0x80; remaining = 1; }
                else if (c >= 0xe0 && c <= 0xef) { scalar = c & 0xf; minimum = 0x800; remaining = 2; }
                else if (c >= 0xf0 && c <= 0xf4) { scalar = c & 7; minimum = 0x10000; remaining = 3; }
                else return false;
            } else {
                if ((c & 0xc0) != 0x80) return false;
                scalar = (scalar << 6) | (c & 0x3f);
                if (!--remaining && (scalar < minimum || scalar > 0x10ffff ||
                                     (scalar >= 0xd800 && scalar <= 0xdfff))) return false;
            }
        }
        return remaining == 0;
    }
    unsigned char folded(unsigned char c) const {
        return !options_.case_sensitive && c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    }
    void advance_generation() {
        if (generation_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("search generation exhausted");
        ++generation_;
    }
    void reset_scan() {
        rows_.clear(); matches_.clear(); origins_.clear(); matched_ = origin_head_ = 0;
        current_ = {}; current_meta_ = {}; byte_ = span_ = 0;
        audit_cursor_ = 0; audit_requested_ = loaded_ = boundary_done_ = at_end_ = cap_reached_ = false;
        next_id_ = emit_from_id_ = 0;
    }
    bool row_valid(const TextDocument& doc, const DocumentLineMetadata& expected, uint64_t successor_id) const {
        const auto index = doc.index_of(expected.id);
        if (!index) return false;
        const auto current = doc.meta(*index);
        if (!expected.joins_next)
            return current.id == expected.id && current.version == expected.version &&
                current.joins_next == expected.joins_next && current.ending == expected.ending &&
                (successor_id != 0 || *index + 1 == doc.size());
        return same(current, expected) && *index + 1 < doc.size() && doc.meta(*index + 1).id == successor_id;
    }
    // Re-read enough preceding text to reconstruct matches crossing the changed
    // row. Keep metadata, not a full text copy or query-sized per-row snapshots.
    void rewind(size_t changed, uint64_t changed_id) {
        size_t first = changed, bytes = 0;
        while (first && bytes < query_.size() - 1) bytes += rows_[--first].bytes;
        next_id_ = first < rows_.size() ? rows_[first].meta.id : changed_id;
        const auto prefix_before = first < rows_.size() ? rows_[first].prefix_before : current_prefix_before_;
        emit_from_id_ = changed_id;
        matches_.erase(std::remove_if(matches_.begin(), matches_.end(), [changed_id](const SearchMatch& hit) {
            return hit.range.end.line_id >= changed_id;
        }), matches_.end());
        rows_.resize(first); audit_cursor_ = (std::min)(audit_cursor_, first);
        current_ = {}; loaded_ = boundary_done_ = at_end_ = cap_reached_ = false;
        byte_ = span_ = origin_head_ = 0; matched_ = prefix_before; origins_.clear();
    }
    void consume(unsigned char c, Origin origin) {
        if (origins_.size() < query_.size()) origins_.push_back(origin);
        else { origins_[origin_head_] = origin; origin_head_ = (origin_head_ + 1) % query_.size(); }
        c = folded(c);
        while (matched_ && c != folded(static_cast<unsigned char>(query_[matched_]))) matched_ = prefix_[matched_ - 1];
        if (c == folded(static_cast<unsigned char>(query_[matched_]))) ++matched_;
        if (matched_ != query_.size()) return;
        const TextRange range{origins_[origin_head_].begin, origin.end};
        if (origins_.size() == query_.size() && range.end.line_id >= emit_from_id_) {
            // Several matching combining components can map to one cell.
            if (matches_.empty() || !equal(matches_.back().range.begin, range.begin) ||
                                    !equal(matches_.back().range.end, range.end))
                matches_.push_back({range});
            if (matches_.size() == options_.result_limit) cap_reached_ = true;
        }
        matched_ = 0; // Explicit non-overlap policy.
    }
    // New output never resets this persistent, budgeted metadata-audit cursor.
    void audit_one(const TextDocument& doc) {
        if (audit_cursor_ < rows_.size()) {
            const auto expected = rows_[audit_cursor_];
            if (!row_valid(doc, expected.meta, expected.successor_id)) rewind(audit_cursor_, expected.meta.id);
            else ++audit_cursor_;
        } else if (loaded_ && !row_valid(doc, current_meta_, current_successor_id_)) rewind(rows_.size(), current_meta_.id);
        else if (audit_serial_ == change_serial_) audit_requested_ = false;
        else { audit_cursor_ = 0; audit_serial_ = change_serial_; }
    }
    // Eviction removes metadata in O(1) per unit instead of restarting history.
    bool prune_one(const TextDocument& doc) {
        if (rows_.empty() || doc.index_of(rows_.front().meta.id)) return false;
        const auto removed = rows_.front().meta.id; rows_.pop_front();
        if (audit_cursor_) --audit_cursor_;
        matches_.erase(std::remove_if(matches_.begin(), matches_.end(), [removed](const SearchMatch& hit) {
            return hit.range.begin.line_id <= removed;
        }), matches_.end());
        if (matches_.size() < options_.result_limit) cap_reached_ = false;
        // A partial query carried in from evicted text can change the phase of
        // non-overlapping matches (e.g. "aa" across odd-width soft rows). Only
        // that boundary case needs a fresh retained-prefix scan; ordinary log
        // eviction with an empty matching prefix keeps all forward progress.
        if ((!rows_.empty() && rows_.front().prefix_before) ||
            (rows_.empty() && loaded_ && current_meta_.id != removed && current_prefix_before_))
            reset_scan();
        return true;
    }
    bool scan_one(const TextDocument& doc, SearchStep& progress) {
        if (cap_reached_) return false;
        if (!loaded_) {
            std::optional<size_t> index;
            if (next_id_) index = doc.index_of(next_id_);
            else if (!rows_.empty()) {
                const auto last = doc.index_of(rows_.back().meta.id);
                if (last && *last + 1 < doc.size()) index = *last + 1;
            } else if (doc.size()) index = 0;
            if (!index) {
                if (next_id_) { next_id_ = 0; at_end_ = false; return true; }
                at_end_ = true; return false;
            }
            current_ = doc.line(*index); current_meta_ = doc.meta(*index);
            current_successor_id_ = *index + 1 < doc.size() ? doc.meta(*index + 1).id : 0;
            current_prefix_before_ = matched_;
            next_id_ = 0; loaded_ = true; byte_ = span_ = 0; boundary_done_ = false;
            at_end_ = false; ++progress.rows_loaded; return true;
        }
        // Calls are same-thread. step() validates an existing row once at its
        // boundary; freshly loaded rows cannot mutate between these byte units.
        const auto& text = current_.projection.text;
        if (byte_ < text.size()) {
            const auto& spans = current_.projection.spans;
            while (span_ < spans.size() && byte_ >= spans[span_].byte_end) ++span_;
            if (span_ == spans.size()) throw std::logic_error("projected search byte has no cell span");
            const auto& span = spans[span_];
            consume(static_cast<unsigned char>(text[byte_++]),
                    {{current_meta_.id, current_meta_.version, span.column_begin},
                     {current_meta_.id, current_meta_.version, span.column_end}});
            ++progress.bytes_scanned; return true;
        }
        const auto index = doc.index_of(current_meta_.id);
        if (!index) { rewind(rows_.size(), current_meta_.id); return true; }
        const bool has_next = *index + 1 < doc.size();
        if (!boundary_done_ && has_next && !current_.joins_next) {
            const auto next = doc.meta(*index + 1);
            const auto end = current_.projection.spans.empty() ? 0 : current_.projection.spans.back().column_end;
            consume('\n', {{current_meta_.id, current_meta_.version, end}, {next.id, next.version, 0}});
            boundary_done_ = true; ++progress.bytes_scanned; return true;
        }
        rows_.push_back({current_meta_, text.size() + (has_next && !current_.joins_next ? 1u : 0u), current_prefix_before_, current_successor_id_});
        next_id_ = has_next ? doc.meta(*index + 1).id : 0;
        current_ = {}; loaded_ = false; at_end_ = !has_next; return true;
    }
public:
    static constexpr size_t query_byte_limit = 4096, result_hard_limit = 4096;
    uint64_t start(const TextDocument&, std::string query, SearchOptions options = {}) {
        if (query.size() > query_byte_limit || !valid_utf8(query))
            throw std::invalid_argument("search query must be valid UTF-8 within 4096 bytes");
        if (!options.result_limit || options.result_limit > result_hard_limit)
            throw std::invalid_argument("search result limit must be between 1 and 4096");
        advance_generation(); reset_scan(); query_ = std::move(query); options_ = options;
        prefix_.assign(query_.size(), 0);
        for (size_t i = 1, length = 0; i < query_.size(); ++i) {
            const auto c = folded(static_cast<unsigned char>(query_[i]));
            while (length && c != folded(static_cast<unsigned char>(query_[length]))) length = prefix_[length - 1];
            if (c == folded(static_cast<unsigned char>(query_[length]))) ++length;
            prefix_[i] = length;
        }
        status_ = query_.empty() ? SearchStatus::complete : SearchStatus::running;
        return generation_;
    }
    uint64_t notify_change(const TextDocument&) {
        advance_generation();
        if (query_.empty() || status_ == SearchStatus::cancelled || status_ == SearchStatus::closed ||
            status_ == SearchStatus::invalidated || status_ == SearchStatus::idle) return generation_;
        if (change_serial_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("search change serial exhausted");
        ++change_serial_;
        if (!audit_requested_) { audit_requested_ = true; audit_cursor_ = 0; audit_serial_ = change_serial_; }
        at_end_ = false; status_ = SearchStatus::running; return generation_;
    }
    SearchStep step(const TextDocument& doc, uint64_t generation, size_t budget) {
        SearchStep progress{status_, 0, 0, 0, generation == generation_};
        if (!progress.accepted || status_ != SearchStatus::running || !budget) return progress;
        if (loaded_ && !row_valid(doc, current_meta_, current_successor_id_)) {
            rewind(rows_.size(), current_meta_.id); ++progress.work_used;
        }
        while (progress.work_used < budget && prune_one(doc)) ++progress.work_used;
        // Alternating units also guarantee scan progress for a budget of one;
        // repeatedly notifying output cannot consume every turn with auditing.
        while (progress.work_used < budget) {
            if (audit_requested_ && (audit_turn_ || cap_reached_ || at_end_)) {
                audit_one(doc); ++progress.work_used; audit_turn_ = false;
            } else if (scan_one(doc, progress)) { ++progress.work_used; audit_turn_ = true; }
            else if (audit_requested_) { audit_turn_ = true; }
            else break;
        }
        if (!audit_requested_ && !loaded_ && at_end_) status_ = SearchStatus::complete;
        if (cap_reached_ && !audit_requested_) status_ = SearchStatus::capped;
        progress.status = status_; return progress;
    }
    void cancel() { advance_generation(); reset_scan(); query_.clear(); prefix_.clear(); status_ = SearchStatus::cancelled; }
    void close() { cancel(); status_ = SearchStatus::closed; }
    void invalidate() { cancel(); status_ = SearchStatus::invalidated; }
    uint64_t generation() const { return generation_; }
    SearchStatus status() const { return status_; }
    // Current-query snapshots. UI must use match(doc,index), because metadata
    // auditing deliberately runs in batches and raw ranges may have expired.
    const std::vector<SearchMatch>& matches() const { return matches_; }
    size_t retained_rows() const { return rows_.size(); }
    std::optional<SearchMatch> match(const TextDocument& doc, size_t index) const {
        if (index >= matches_.size()) return {};
        const auto hit = matches_[index];
        if (!doc.valid(hit.range.begin) || !doc.valid(hit.range.end)) return {};
        const auto first = std::lower_bound(rows_.begin(), rows_.end(), hit.range.begin.line_id,
            [](const ScannedRow& row, uint64_t id) { return row.meta.id < id; });
        for (auto row = first; row != rows_.end() && row->meta.id <= hit.range.end.line_id; ++row)
            if (!row_valid(doc, row->meta, row->successor_id)) return {};
        if (loaded_ && current_meta_.id >= hit.range.begin.line_id && current_meta_.id <= hit.range.end.line_id &&
            !row_valid(doc, current_meta_, current_successor_id_)) return {};
        return hit;
    }
    std::optional<SearchNavigation> navigate(const TextDocument& doc, SearchDirection direction,
                                             std::optional<TextPosition> anchor = {}, bool allow_wrap = true) const {
        if (anchor && !doc.valid(*anchor)) return {};
        const bool next = direction == SearchDirection::next;
        for (int pass = 0; pass < (allow_wrap ? 2 : 1); ++pass) for (size_t offset = 0; offset < matches_.size(); ++offset) {
            const auto index = next ? offset : matches_.size() - 1 - offset;
            const auto hit = match(doc, index); if (!hit) continue;
            const bool beyond = !anchor || (next ? before(*anchor, hit->range.begin) : before(hit->range.begin, *anchor));
            if ((pass == 0 && beyond) || (pass == 1 && !beyond)) return SearchNavigation{*hit, index, pass == 1};
        }
        return {};
    }
    // Explicit user navigation may inspect live-validated partial results.
    // Absence means "no currently known hit in this direction", never a
    // global document boundary, and must not wrap within a partial prefix.
    std::optional<SearchNavigation> navigate_no_wrap(const TextDocument& doc, SearchDirection direction,
                                                     std::optional<TextPosition> anchor = {}) const {
        return navigate(doc, direction, anchor, false);
    }
};
} // namespace termforge
