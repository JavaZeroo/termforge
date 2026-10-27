// Independent history display projection. No HWND, parser or transport state.
#pragma once
#include "view_mapping.hpp"

namespace termforge {
struct ViewportState {
    size_t top = 0, total = 0, page = 0, maximum_top = 0;
    uint64_t anchor = 0;
    bool following = true;
};
struct ViewSnapshot {
    // Fragments may share their first source line's id/version. Text actions
    // resolve original document coordinates through the mapping, not these ids.
    std::vector<Line> lines;
    ViewportState viewport;
    ViewMapping mapping;
    int columns = 0;
    int cursor_row = -1, cursor_column = 0;
};
class HistoryViewport {
    struct Piece {
        size_t source_row = 0, source_column = 0, length = 0;
        size_t column = 0, display_length = 0;
        bool clipped = false;
    };
    struct DisplayRow {
        size_t source_row = 0, source_column = 0, used = 0;
        bool physical = true;
        LineEnding ending = LineEnding::none;
        std::vector<Piece> pieces;
    };
    struct Layout {
        std::vector<DisplayRow> rows;
        size_t history = 0;
    };
    uint64_t anchor_ = 0;
    size_t anchor_column_ = 0;
    bool following_ = true;

    static bool default_padding(const Cell& cell) {
        return cell.text == " " && cell.width == 1 && cell.style.plain() && cell.marks == 0 &&
            !cell.written && !cell.clipped && !cell.grapheme_fragment && !cell.leading_spacer;
    }

    static void project_chain(const Screen& screen, size_t first, size_t last, Layout& layout) {
        const auto width = static_cast<size_t>(screen.width());
        const auto start = layout.rows.size();
        DisplayRow current;
        current.physical = false;
        current.source_row = first;
        const auto flush = [&] {
            current.ending = LineEnding::soft_wrap;
            layout.rows.push_back(std::move(current));
            current = DisplayRow{};
            current.physical = false;
        };
        for (size_t index = first; index <= last; ++index) {
            const auto& line = screen.retained_line(index);
            size_t end = line.size();
            while (end && default_padding(line[end - 1])) --end;
            for (size_t column = 0; column < end;) {
                // An old autowrap gap belongs to its former grid geometry,
                // not to the logical text being laid out at the new width.
                if (line[column].leading_spacer) { ++column; continue; }
                const auto length = line[column].width == 2 && column + 1 < end &&
                    line[column + 1].width == 0 ? size_t{2} : size_t{1};
                const auto display_length = (std::min)(length, width);
                if (current.used && current.used + display_length > width) flush();
                if (!current.used) {
                    current.source_row = index;
                    current.source_column = column;
                }
                const bool clipped = display_length != length;
                if (!current.pieces.empty() && !clipped && !current.pieces.back().clipped &&
                    current.pieces.back().source_row == index &&
                    current.pieces.back().source_column + current.pieces.back().length == column) {
                    current.pieces.back().length += length;
                    current.pieces.back().display_length += display_length;
                } else {
                    current.pieces.push_back({index, column, length, current.used, display_length, clipped});
                }
                current.used += display_length;
                column += length;
                if (current.used == width) flush();
            }
        }
        if (current.used || layout.rows.size() == start) {
            if (!current.used) current.source_row = first;
            layout.rows.push_back(std::move(current));
        }
        layout.rows.back().ending = screen.retained_line(last).ending;
    }

    static Layout layout(const Screen& screen) {
        Layout result;
        result.rows.reserve(screen.retained_size());
        for (size_t first = 0; first < screen.history.size();) {
            size_t last = first;
            bool mixed = screen.retained_line(first).size() != static_cast<size_t>(screen.width());
            while (last + 1 < screen.history.size() &&
                   continues_into(screen.retained_line(last), screen.retained_line(last + 1))) {
                ++last;
                mixed |= screen.retained_line(last).size() != static_cast<size_t>(screen.width());
            }
            if (mixed) project_chain(screen, first, last, result);
            else for (size_t index = first; index <= last; ++index) {
                DisplayRow row;
                row.source_row = index;
                result.rows.push_back(std::move(row));
            }
            first = last + 1;
        }
        result.history = result.rows.size();
        // The active grid retains its physical layout and cursor coordinates.
        for (size_t index = screen.history.size(); index < screen.retained_size(); ++index) {
            DisplayRow row;
            row.source_row = index;
            result.rows.push_back(std::move(row));
        }
        return result;
    }

    static size_t locate(const Layout& display, size_t source_row, size_t column) {
        std::optional<size_t> nearest;
        for (size_t index = 0; index < display.rows.size(); ++index) {
            const auto& row = display.rows[index];
            if (row.physical) {
                if (row.source_row == source_row) return index;
                if (row.source_row > source_row) return nearest.value_or(index);
                continue;
            }
            if (row.source_row > source_row) return nearest.value_or(index);
            if (row.source_row == source_row) nearest = index;
            for (const auto& piece : row.pieces) {
                if (piece.source_row != source_row) continue;
                nearest = index;
                if (column < piece.source_column + piece.length) return index;
            }
        }
        return nearest.value_or(display.rows.size() - 1);
    }

    void set_anchor(const Screen& screen, const DisplayRow& row) {
        anchor_ = screen.retained_line(row.source_row).id;
        anchor_column_ = row.source_column;
    }

    ViewportState reconcile(const Screen& screen, const Layout& display) {
        size_t top = display.history;
        if (following_) set_anchor(screen, display.rows[top]);
        else {
            const auto source_row = screen.lower_bound_line(anchor_);
            top = (std::min)(locate(display, source_row, anchor_column_), display.history);
            if (source_row >= screen.retained_size() || screen.retained_line(source_row).id != anchor_)
                set_anchor(screen, display.rows[top]);
            else anchor_column_ = (std::min)(anchor_column_, screen.retained_line(source_row).size());
        }
        return {top, display.rows.size(), static_cast<size_t>(screen.height()), display.history, anchor_, following_};
    }

    ViewportState scroll_to(const Screen& screen, const Layout& display, size_t top) {
        top = (std::min)(top, display.history);
        following_ = top == display.history;
        set_anchor(screen, display.rows[top]);
        return reconcile(screen, display);
    }

    static Line materialize(const Screen& screen, const DisplayRow& row) {
        const auto& source = screen.retained_line(row.source_row);
        if (row.physical) return source;
        Line result(static_cast<size_t>(screen.width()));
        result.id = source.id;
        result.version = source.version;
        result.ending = row.ending;
        result.wrap_columns = row.ending == LineEnding::soft_wrap ? row.used : 0;
        for (const auto& piece : row.pieces) {
            const auto& origin = screen.retained_line(piece.source_row);
            if (piece.clipped) {
                result[piece.column] = origin[piece.source_column];
                result[piece.column].width = static_cast<int>(piece.display_length);
                result[piece.column].clipped = true;
            } else std::copy_n(origin.begin() + piece.source_column, piece.length, result.begin() + piece.column);
        }
        return result;
    }

    static void map_piece(const Screen& screen, size_t row, const Piece& piece,
                          std::vector<ViewSourceSpan>& mapping) {
        const auto& source = screen.retained_line(piece.source_row);
        size_t display_column = piece.column;
        const auto end = piece.source_column + piece.length;
        for (size_t column = piece.source_column; column < end;) {
            const auto length = source[column].width == 2 && column + 1 < end &&
                source[column + 1].width == 0 ? size_t{2} : size_t{1};
            const auto width = piece.clipped ? piece.display_length : length;
            mapping.push_back({row, display_column, display_column + width,
                {{source.id, source.version, column}, {source.id, source.version, column + length}}});
            display_column += width;
            column += length;
        }
    }

    static void map_row(const Screen& screen, size_t row, const DisplayRow& display,
                        std::vector<ViewSourceSpan>& mapping) {
        const auto& source = screen.retained_line(display.source_row);
        if (display.physical) {
            map_piece(screen, row, {display.source_row, 0, source.size(), 0, source.size(), false}, mapping);
        } else if (display.pieces.empty()) {
            const TextPosition position{source.id, source.version, display.source_column};
            mapping.push_back({row, 0, 0, {position, position}});
        } else for (const auto& piece : display.pieces) map_piece(screen, row, piece, mapping);
    }

public:
    // A viewport is owned by exactly one session. Never share it across Screens.
    ViewportState reconcile(const Screen& screen) { return reconcile(screen, layout(screen)); }
    ViewportState scroll_to(const Screen& screen, size_t top) { return scroll_to(screen, layout(screen), top); }
    ViewportState scroll_by(const Screen& screen, int64_t rows) {
        const auto display = layout(screen);
        const auto state = reconcile(screen, display);
        size_t top;
        if (rows < 0) {
            // Avoid signed overflow for INT64_MIN and never wrap size_t.
            const auto distance = static_cast<uint64_t>(-(rows + 1)) + 1;
            top = distance > state.top ? 0 : state.top - static_cast<size_t>(distance);
        } else {
            const auto distance = static_cast<uint64_t>(rows);
            top = distance > state.maximum_top - state.top ? state.maximum_top : state.top + static_cast<size_t>(distance);
        }
        return scroll_to(screen, display, top);
    }
    ViewportState bottom(const Screen& screen) {
        const auto display = layout(screen);
        return scroll_to(screen, display, display.history);
    }
    ViewportState reveal(const Screen& screen, uint64_t identity) {
        const auto display = layout(screen);
        return scroll_to(screen, display, locate(display, screen.lower_bound_line(identity), 0));
    }
    std::optional<ViewportState> reveal(const Screen& screen, TextPosition position) {
        if (!TextDocument(screen).valid(position)) return std::nullopt;
        const auto display = layout(screen);
        auto state = scroll_to(screen, display,
            locate(display, screen.lower_bound_line(position.line_id), position.column));
        if (!following_) {
            anchor_ = position.line_id;
            anchor_column_ = position.column;
            state.anchor = anchor_;
        }
        return state;
    }
    bool following() const { return following_; }
    uint64_t anchor() const { return anchor_; }
    ViewSnapshot snapshot(const Screen& screen) {
        const auto display = layout(screen);
        ViewSnapshot result;
        result.columns = screen.width();
        result.viewport = reconcile(screen, display);
        result.lines.reserve(result.viewport.page);
        std::vector<ViewSourceSpan> mapping;
        for (size_t i = 0; i < result.viewport.page; ++i) {
            result.lines.push_back(materialize(screen, display.rows[result.viewport.top + i]));
            map_row(screen, i, display.rows[result.viewport.top + i], mapping);
        }
        result.mapping = ViewMapping(static_cast<size_t>(result.columns), std::move(mapping));
        for (size_t i = 0; i < result.lines.size(); ++i) {
            const auto next = result.viewport.top + i + 1;
            if (!display.rows[result.viewport.top + i].physical &&
                result.lines[i].ending == LineEnding::soft_wrap && next < display.rows.size())
                result.lines[i].next_id = screen.retained_line(display.rows[next].source_row).id;
        }
        // A paused historical view must not draw a live caret over unrelated text.
        if (result.viewport.following) {
            result.cursor_row = screen.row;
            result.cursor_column = screen.column;
        }
        return result;
    }
};
} // namespace termforge
