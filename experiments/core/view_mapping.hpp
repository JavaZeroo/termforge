// Value-only source coordinates published with one immutable display page.
#pragma once
#include "text_document.hpp"

namespace termforge {
enum class ViewAffinity { leading, trailing };
struct ViewCellSpan {
    size_t row = 0, column_begin = 0, column_end = 0;
};
struct ViewSourceSpan {
    size_t row = 0, column_begin = 0, column_end = 0;
    TextRange source;
};

class ViewMapping {
    size_t columns_ = 0;
    std::vector<ViewSourceSpan> sources_;

    static bool before(TextPosition a, TextPosition b) {
        return a.line_id < b.line_id || (a.line_id == b.line_id && a.column < b.column);
    }
    bool valid(const TextDocument& document, const ViewSourceSpan& span) const {
        return span.column_begin <= span.column_end && span.column_end <= columns_ &&
            span.source.begin.line_id == span.source.end.line_id &&
            span.source.begin.version == span.source.end.version &&
            span.source.begin.column <= span.source.end.column &&
            document.valid(span.source.begin) && document.valid(span.source.end);
    }
public:
    ViewMapping() = default;
    ViewMapping(size_t columns, std::vector<ViewSourceSpan> sources)
        : columns_(columns), sources_(std::move(sources)) {}

    std::optional<TextPosition> hit(const TextDocument& document, size_t row, size_t column,
                                    ViewAffinity affinity) const {
        if (column > columns_) return std::nullopt;
        const ViewSourceSpan* first = nullptr;
        const ViewSourceSpan* previous = nullptr;
        for (const auto& span : sources_) {
            if (span.row != row) continue;
            if (!valid(document, span)) return std::nullopt;
            if (!first) first = &span;
        }
        if (!first) return std::nullopt;
        for (const auto& span : sources_) {
            if (span.row != row) continue;
            if (column <= span.column_begin) {
                if (previous && affinity == ViewAffinity::trailing) return previous->source.end;
                return span.source.begin;
            }
            if (column < span.column_end) {
                auto result = span.source.begin;
                const auto source_width = span.source.end.column - result.column;
                const auto display_width = span.column_end - span.column_begin;
                // Ordinary wide-cell interiors keep their physical boundary.
                // TextDocument normalizes a complete drag range afterward.
                if (source_width == display_width) {
                    result.column += column - span.column_begin;
                    return result;
                }
                return affinity == ViewAffinity::leading ? span.source.begin : span.source.end;
            }
            if (column == span.column_end && affinity == ViewAffinity::trailing) return span.source.end;
            previous = &span;
        }
        // Synthetic trailing padding has no independent source coordinates.
        return previous ? std::optional<TextPosition>{previous->source.end} : std::nullopt;
    }

    std::vector<ViewCellSpan> project(const TextDocument& document, TextRange range) const {
        const auto normalized = document.normalize(range);
        if (!normalized || normalized->begin == normalized->end) return {};
        std::vector<ViewCellSpan> result;
        for (const auto& span : sources_) {
            if (!valid(document, span)) return {};
            if (!before(span.source.begin, normalized->end) || !before(normalized->begin, span.source.end))
                continue;
            if (span.column_begin == span.column_end) continue;
            if (!result.empty() && result.back().row == span.row &&
                result.back().column_end == span.column_begin) result.back().column_end = span.column_end;
            else result.push_back({span.row, span.column_begin, span.column_end});
        }
        return result;
    }
};
} // namespace termforge
