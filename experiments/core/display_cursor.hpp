// Source-to-display caret mapping for one projected page.
//
// The protocol cursor is a retained source position (fragment row and column).
// A display page reflows complete source atoms at another width, so the caret
// cell is a different coordinate. This header owns only that conversion; it
// never moves the protocol cursor and never invents source content.
#pragma once
#include "history_view.hpp"
#include <optional>

namespace termforge {

struct DisplayCursor {
    // The page covered the cursor's source row and produced a display cell.
    bool inside = false;
    // Display cell: zero-based row within the page and column within its width.
    size_t row = 0, column = 0;
};

inline std::optional<DisplayCursor> display_cursor(const Screen& screen, const ViewSnapshot& page) {
    if (screen.row < 0 || screen.row >= screen.height()) return std::nullopt;
    const size_t retained = screen.history_lines().size() + static_cast<size_t>(screen.row);
    if (retained >= screen.retained_size()) return std::nullopt;
    const auto& line = screen.retained_line(retained);
    const auto& spans = page.mapping.sources();
    const size_t columns = page.mapping.columns();
    if (!columns) return std::nullopt;
    size_t column = static_cast<size_t>((std::max)(screen.column, 0));
    column = (std::min)(column, line.size());
    const ViewSourceSpan* first = nullptr;
    const ViewSourceSpan* last = nullptr;
    for (const auto& span : spans) {
        if (span.source.begin.line_id != line.id || span.source.begin.version != line.version) continue;
        if (!first) first = &span;
        last = &span;
    }
    if (!first) return std::nullopt;
    for (const auto& span : spans) {
        if (span.source.begin.line_id != line.id || span.source.begin.version != line.version) continue;
        if (span.source.begin.column > column) continue;
        if (column < span.source.end.column) {
            DisplayCursor result;
            result.inside = true;
            result.row = span.row;
            const auto source_width = span.source.end.column - span.source.begin.column;
            const auto display_width = span.column_end - span.column_begin;
            // A wide or otherwise indivisible display atom keeps its leading
            // edge; one-to-one cells preserve the column offset.
            result.column = display_width == source_width
                ? span.column_begin + (column - span.source.begin.column)
                : span.column_begin;
            return result;
        }
        if (column == span.source.end.column) {
            // The caret sits immediately after this atom: inside the span when
            // another span continues on the same display row, otherwise at its
            // trailing edge. A trailing edge at the display row width continues
            // on the next display row without claiming a source identity there.
            DisplayCursor result;
            result.inside = true;
            result.row = span.row;
            result.column = span.column_end;
            if (result.column >= columns) {
                result.row += result.column / columns;
                result.column %= columns;
            }
            return result;
        }
    }
    // Cursor in untouched trailing padding: the display keeps the same cell
    // offsets after the last published atom on that fragment's final row.
    DisplayCursor result;
    result.inside = true;
    result.row = last->row;
    result.column = last->column_end + (column - last->source.end.column);
    if (result.column >= columns) {
        // Synthetic padding ends at the display row edge; the caret continues
        // on the next display row without claiming a source identity there.
        result.row += result.column / columns;
        result.column %= columns;
    }
    return result;
}

} // namespace termforge
