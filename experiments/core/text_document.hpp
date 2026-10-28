// Value-only, one-row-at-a-time text access for selection and search.
#pragma once
#include "text_projection.hpp"
#include <optional>

namespace termforge {
struct TextPosition {
    uint64_t line_id = 0, version = 0;
    size_t column = 0;
    uint64_t epoch = 0;
};
inline bool operator==(TextPosition a, TextPosition b) {
    return a.line_id == b.line_id && a.version == b.version && a.column == b.column && a.epoch == b.epoch;
}
inline bool operator!=(TextPosition a, TextPosition b) { return !(a == b); }
struct TextRange { TextPosition begin, end; }; // Half-open physical-cell positions.
struct DocumentLineMetadata {
    uint64_t id = 0, version = 0;
    size_t columns = 0;
    bool joins_next = false;
    LineEnding ending = LineEnding::none;
};
struct DocumentLine {
    ProjectedLine projection;
    size_t columns = 0;
    bool joins_next = false;
    LineEnding ending = LineEnding::none;
};
// A short-lived, same-thread view: Screen must outlive this object. Returned
// values own their text. One read is bounded by Screen's 500-cell row limit.
class TextDocument {
    const Screen& screen_;
public:
    explicit TextDocument(const Screen& screen) : screen_(screen) {}
    uint64_t epoch() const { return screen_.buffer_epoch(); }
    size_t size() const { return screen_.retained_size(); }
    DocumentLineMetadata meta(size_t index) const {
        const auto& value = screen_.retained_line(index);
        const bool joins = index + 1 < size() && continues_into(value, screen_.retained_line(index + 1));
        return {value.id, value.version, value.size(), joins, value.ending};
    }
    DocumentLine line(size_t index) const {
        const auto value = meta(index);
        return {project_line(screen_.retained_line(index)), value.columns, value.joins_next, value.ending};
    }
    std::optional<size_t> index_of(uint64_t identity) const {
        const auto index = screen_.lower_bound_line(identity);
        if (index == size() || screen_.retained_line(index).id != identity) return std::nullopt;
        return index;
    }
    bool valid(TextPosition value) const {
        if (value.epoch != epoch()) return false;
        const auto index = index_of(value.line_id);
        if (!index) return false;
        const auto current = meta(*index);
        return current.version == value.version && value.column <= current.columns;
    }
    std::optional<TextPosition> position(size_t index, size_t column) const {
        if (index >= size()) return std::nullopt;
        const auto value = meta(index);
        if (column > value.columns) return std::nullopt;
        return TextPosition{value.id, value.version, column, epoch()};
    }
    // A range is physical-cell based and half-open. Endpoints inside a width-2
    // glyph expand outward, so no consumer can copy or paint half of a glyph.
    // Equal endpoints remain empty, including an endpoint in a trailing cell.
    std::optional<TextRange> normalize(TextRange range) const {
        if (!valid(range.begin) || !valid(range.end)) return std::nullopt;
        const auto first = *index_of(range.begin.line_id), last = *index_of(range.end.line_id);
        if (first > last || (first == last && range.begin.column > range.end.column))
            std::swap(range.begin, range.end);
        if (range.begin == range.end) return range;
        const auto snap = [&](TextPosition& value, bool end) {
            const auto row = line(*index_of(value.line_id));
            for (const auto& span : row.projection.spans)
                if (span.column_begin < value.column && value.column < span.column_end) {
                    value.column = end ? span.column_end : span.column_begin;
                    break;
                }
        };
        snap(range.begin, false); snap(range.end, true);
        return range;
    }
};
} // namespace termforge
