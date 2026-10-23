// Original bounded selection state; no HWND, clipboard or mutable row handles.
#pragma once
#include "text_document.hpp"

namespace termforge {
struct SelectionColumns { size_t begin = 0, end = 0; };
enum class SelectionCopyStatus { ok, empty, invalidated, limit_exceeded };
struct SelectionCopy {
    SelectionCopyStatus status = SelectionCopyStatus::empty;
    std::string text;
};
class TextSelection {
    std::optional<TextRange> range_;
    std::vector<DocumentLine> saved_;
    bool invalidated_ = false;
    static bool same_text(const DocumentLine& a, const DocumentLine& b, bool compare_boundary) {
        if (a.projection.text != b.projection.text || a.projection.spans.size() != b.projection.spans.size() ||
            (compare_boundary && (a.joins_next != b.joins_next || a.ending != b.ending))) return false;
        for (size_t i = 0; i < a.projection.spans.size(); ++i) {
            const auto x = a.projection.spans[i], y = b.projection.spans[i];
            if (x.byte_begin != y.byte_begin || x.byte_end != y.byte_end ||
                x.column_begin != y.column_begin || x.column_end != y.column_end) return false;
        }
        return true;
    }
    // TermForge's bounded first word policy, not an inferred Unicode word-break
    // standard: ASCII letters/digits/_/-, and non-ASCII bases form words;
    // spaces form a run, all other ASCII punctuation is a single glyph.
    static int word_class(const ProjectedLine& row, const TextSpan& span) {
        const auto c = static_cast<unsigned char>(row.text[span.byte_begin]);
        if (c == ' ') return 1;
        return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' ? 0 : 2;
    }
    bool invalidate() { clear(); invalidated_ = true; return false; }
public:
    static constexpr size_t maximum_snapshot_bytes = 8 * 1024 * 1024;
    void clear() { range_.reset(); saved_.clear(); invalidated_ = false; }
    bool empty() const { return !range_; }
    const std::optional<TextRange>& range() const { return range_; }
    bool set(const TextDocument& document, TextRange range) {
        clear();
        const auto normalized = document.normalize(range);
        if (!normalized) return false;
        if (normalized->begin == normalized->end) return true;
        const auto begin = *document.index_of(normalized->begin.line_id);
        const auto end = *document.index_of(normalized->end.line_id);
        std::vector<DocumentLine> saved;
        saved.reserve(end - begin + 1);
        size_t bytes = saved.capacity() * sizeof(DocumentLine);
        if (bytes > maximum_snapshot_bytes) return false;
        for (size_t index = begin; index <= end; ++index) {
            auto line = document.line(index);
            const auto cost = line.projection.text.capacity() +
                              line.projection.spans.capacity() * sizeof(TextSpan);
            if (cost > maximum_snapshot_bytes - bytes) return false;
            bytes += cost; saved.push_back(std::move(line));
        }
        range_ = normalized; saved_ = std::move(saved); return true;
    }
    // Resize can bump Screen's versions without changing any projected text.
    // Refresh only after exact owned-text/span/break comparison. Real writes,
    // cropping and history eviction invalidate the entire range atomically.
    bool reconcile(const TextDocument& document) {
        if (!range_) return !invalidated_;
        const auto begin = document.index_of(range_->begin.line_id);
        if (!begin || saved_.size() > document.size() - *begin) return invalidate();
        for (size_t i = 0; i < saved_.size(); ++i) {
            const auto meta = document.meta(*begin + i);
            auto& saved = saved_[i];
            if (meta.id != saved.projection.id) return invalidate();
            if (meta.version != saved.projection.version || meta.joins_next != saved.joins_next) {
                auto current = document.line(*begin + i);
                // The last row at column zero is an excluded half-open
                // endpoint. Its own following break is outside the copy.
                const bool compare_boundary = i + 1 != saved_.size() || range_->end.column != 0;
                if (!same_text(saved, current, compare_boundary)) return invalidate();
                saved = std::move(current);
            }
        }
        const auto first = document.position(*begin, range_->begin.column);
        const auto last = document.position(*begin + saved_.size() - 1, range_->end.column);
        if (!first || !last) return invalidate();
        range_ = TextRange{*first, *last}; return true;
    }
    bool select_word(const TextDocument& document, size_t index, size_t column) {
        clear();
        if (index >= document.size()) return false;
        auto row = document.line(index);
        size_t glyph = 0;
        while (glyph < row.projection.spans.size() &&
               row.projection.spans[glyph].column_end <= column) ++glyph;
        if (glyph == row.projection.spans.size() || row.projection.spans[glyph].column_begin > column)
            return false;
        const auto kind = word_class(row.projection, row.projection.spans[glyph]);
        size_t first_row = index, last_row = index, first = glyph, last = glyph + 1;
        const auto original = row;
        if (kind != 2) {
            while (true) {
                while (first && word_class(row.projection, row.projection.spans[first - 1]) == kind) --first;
                if (first || !first_row) break;
                auto previous = document.line(first_row - 1);
                if (!previous.joins_next || previous.projection.spans.empty() ||
                    word_class(previous.projection, previous.projection.spans.back()) != kind) break;
                --first_row; row = std::move(previous); first = row.projection.spans.size();
            }
        }
        const auto begin = document.position(first_row, row.projection.spans[first].column_begin);
        row = original;
        if (kind != 2) {
            while (true) {
                while (last < row.projection.spans.size() &&
                       word_class(row.projection, row.projection.spans[last]) == kind) ++last;
                if (last < row.projection.spans.size() || !row.joins_next || last_row + 1 >= document.size()) break;
                auto next = document.line(last_row + 1);
                if (next.projection.spans.empty() ||
                    word_class(next.projection, next.projection.spans.front()) != kind) break;
                ++last_row; row = std::move(next); last = 0;
            }
        }
        const auto end = document.position(last_row, row.projection.spans[last - 1].column_end);
        return begin && end && set(document, {*begin, *end});
    }
    // Mouse triple-click selects one physical row without its line ending.
    bool select_line(const TextDocument& document, size_t index) {
        clear();
        if (index >= document.size()) return false;
        const auto row = document.line(index);
        const auto end = row.projection.spans.empty() ? 0 : row.projection.spans.back().column_end;
        return set(document, {*document.position(index, 0), *document.position(index, end)});
    }
    // Call reconcile once after a screen update, then query only visible rows.
    std::optional<SelectionColumns> columns(const TextDocument& document, size_t index) const {
        if (!range_ || index >= document.size() || !document.valid(range_->begin) ||
            !document.valid(range_->end)) return std::nullopt;
        const auto begin = *document.index_of(range_->begin.line_id);
        const auto end = *document.index_of(range_->end.line_id);
        if (index < begin || index > end) return std::nullopt;
        const auto first = index == begin ? range_->begin.column : 0;
        const auto last = index == end ? range_->end.column : document.meta(index).columns;
        if (first >= last) return std::nullopt;
        return SelectionColumns{first, last};
    }
    SelectionCopy copy(const TextDocument& document, size_t maximum_bytes) {
        if (!reconcile(document)) return {SelectionCopyStatus::invalidated, {}};
        if (!range_) return {};
        const auto first = *document.index_of(range_->begin.line_id);
        std::string text;
        const auto append = [&](const std::string& source, size_t begin, size_t count) {
            if (count > maximum_bytes - text.size()) return false;
            text.append(source, begin, count); return true;
        };
        for (size_t offset = 0; offset < saved_.size(); ++offset) {
            const auto& line = saved_[offset];
            const auto columns = this->columns(document, first + offset);
            if (columns) for (const auto span : line.projection.spans)
                if (span.column_begin >= columns->begin && span.column_end <= columns->end &&
                    !append(line.projection.text, span.byte_begin, span.byte_end - span.byte_begin))
                    return {SelectionCopyStatus::limit_exceeded, {}};
            if (offset + 1 < saved_.size() && !line.joins_next &&
                !append("\r\n", 0, 2)) return {SelectionCopyStatus::limit_exceeded, {}};
        }
        return {text.empty() ? SelectionCopyStatus::empty : SelectionCopyStatus::ok, std::move(text)};
    }
};
} // namespace termforge
