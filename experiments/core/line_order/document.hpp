// Experimental counterpart of TextDocument: reuse real Screen/Line/projection
// and DTOs, but route identity/order queries through the competing adapters.
#pragma once
#include "adapters.hpp"
#include "text_document.hpp"

namespace termforge::line_order {
template<class Adapter> class Document {
    const Source& source_;
    Adapter& adapter_;
public:
    Document(const Source& source, Adapter& adapter) : source_(source), adapter_(adapter) {}
    uint64_t epoch() const { return source_.epoch; }
    uint64_t order_revision() const { return source_.order_revision; }
    size_t size() const { return source_.screen.retained_size(); }
    std::optional<size_t> index_of(uint64_t id) const { return adapter_.locate(id); }
    DocumentLineMetadata meta(size_t index) const {
        if (!source_.current()) throw std::logic_error("experimental document frame is stale");
        const auto& value = source_.screen.retained_line(index);
        const bool joins = index + 1 < size() && continues_into(value, source_.screen.retained_line(index + 1));
        return {value.id, value.version, value.size(), joins, value.ending};
    }
    DocumentLine line(size_t index) const {
        const auto value = meta(index);
        return {project_line(source_.screen.retained_line(index)), value.columns, value.joins_next, value.ending};
    }
    bool valid(TextPosition value) const {
        if (!source_.current() || value.epoch != epoch()) return false;
        const auto index = index_of(value.line_id);
        if (!index) return false;
        const auto current = meta(*index);
        return current.version == value.version && value.column <= current.columns;
    }
    std::optional<TextPosition> position(size_t index, size_t column) const {
        if (!source_.current() || index >= size()) return {};
        const auto value = meta(index);
        if (column > value.columns) return {};
        return TextPosition{value.id, value.version, column, epoch()};
    }
    // An absent result is invalid/stale; it does not choose a UI fallback.
    std::optional<int> compare(TextPosition a, TextPosition b) const {
        if (!valid(a) || !valid(b)) return {};
        const auto first = *index_of(a.line_id), last = *index_of(b.line_id);
        if (first != last) return first < last ? -1 : 1;
        return a.column == b.column ? 0 : (a.column < b.column ? -1 : 1);
    }
    std::optional<TextRange> normalize(TextRange range) const {
        const auto order = compare(range.begin, range.end);
        if (!order) return {};
        if (*order > 0) std::swap(range.begin, range.end);
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
} // namespace termforge::line_order
