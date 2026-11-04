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
    uint64_t layout_revision_ = 0;

    // Identities do not order the document: retained sequence does. Document
    // order therefore comes from TextDocument::compare, which stays correct for
    // reordered rows and rejects positions captured at an older buffer epoch.
    static bool before(const TextDocument& document, TextPosition a, TextPosition b) {
        const auto order = document.compare(a, b);
        return order.has_value() && *order < 0;
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
    ViewMapping(size_t columns, std::vector<ViewSourceSpan> sources, uint64_t layout_revision = 0)
        : columns_(columns), sources_(std::move(sources)), layout_revision_(layout_revision) {}

    size_t columns() const { return columns_; }
    const std::vector<ViewSourceSpan>& sources() const { return sources_; }
    uint64_t layout_revision() const { return layout_revision_; }

    std::optional<TextPosition> hit(const TextDocument& document, size_t row, size_t column,
                                    ViewAffinity affinity, uint64_t current_layout_revision = 0) const {
        if (layout_revision_ != current_layout_revision) return std::nullopt;
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

    std::vector<ViewCellSpan> project(const TextDocument& document, TextRange range,
                                    uint64_t current_layout_revision = 0) const {
        if (layout_revision_ != current_layout_revision) return {};
        const auto normalized = document.normalize(range);
        if (!normalized || normalized->begin == normalized->end) return {};
        std::vector<ViewCellSpan> result;
        for (const auto& span : sources_) {
            if (!valid(document, span)) return {};
            if (!before(document, span.source.begin, normalized->end) ||
                !before(document, normalized->begin, span.source.end))
                continue;
            auto first = span.column_begin, last = span.column_end;
            if (span.source.end.column - span.source.begin.column == last - first) {
                // One-to-one physical cells preserve the selected intersection,
                // including source/display spans with different starting columns.
                // normalize() has already expanded wide-glyph endpoints.
                if (normalized->begin.line_id == span.source.begin.line_id)
                    first += (std::max)(normalized->begin.column, span.source.begin.column) -
                        span.source.begin.column;
                if (normalized->end.line_id == span.source.end.line_id)
                    last = span.column_begin +
                        (std::min)(normalized->end.column, span.source.end.column) - span.source.begin.column;
            }
            // Unequal-width spans are indivisible display atoms: any overlap
            // projects the whole atom, matching hit()'s leading/trailing policy.
            if (first == last) continue;
            if (!result.empty() && result.back().row == span.row &&
                result.back().column_end == first) result.back().column_end = last;
            else result.push_back({span.row, first, last});
        }
        return result;
    }
};
} // namespace termforge
