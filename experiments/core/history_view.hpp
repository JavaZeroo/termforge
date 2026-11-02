// Independent physical-row history viewport. No HWND, parser or transport state.
#pragma once
#include "view_mapping.hpp"

namespace termforge {
struct ViewportState {
    size_t top = 0, total = 0, page = 0, maximum_top = 0;
    uint64_t anchor = 0;
    bool following = true;
};
struct ViewSnapshot {
    std::vector<Line> lines;
    // Coordinates and cells are published together. A consumer validates this
    // value-only mapping against the current document before using a hit/range.
    ViewMapping mapping;
    ViewportState viewport;
    int cursor_row = -1, cursor_column = 0;
};
// A rank hint for one source traversal, never a durable document position.
// Initial {row,column} requests are unbound; returned cursors bind their rank
// to the current source identity/version, epoch and owner order revision.
struct ProjectionCursor {
    size_t row = 0, column = 0;
    TextPosition source;
    uint64_t order_revision = 0;
    bool bound = false;
};
enum class ProjectionStop { page_full, source_end, source_limit };
struct ProjectionPage {
    ViewSnapshot snapshot;
    ProjectionCursor next;
    ProjectionStop stop = ProjectionStop::source_limit;
    // Rows whose Cell values were examined, including a rolled-back partial
    // display row. Constant-size identity/link checks do not scan row contents.
    size_t source_rows_read = 0;
};
class HistoryViewport {
    uint64_t anchor_ = 0, observed_epoch_ = 0;
    uint64_t layout_revision_ = 0;
    bool following_ = true;
    void sync_epoch(const Screen& screen) {
        if (observed_epoch_ == screen.buffer_epoch()) return;
        observed_epoch_ = screen.buffer_epoch();
        anchor_ = 0;
        following_ = true;
    }
public:
    // A viewport is owned by exactly one session. Never share it across Screens.
    // A live anchor uses its exact rank; an evicted anchor keeps the existing
    // ordered-history fallback (first retained row at or after the id).
    size_t anchor_top(const Screen& screen) const {
        if (const auto index = screen.locate(anchor_)) return (std::min)(*index, screen.history_lines().size());
        return (std::min)(screen.lower_bound_line(anchor_), screen.history_lines().size());
    }
    ViewportState reconcile(const Screen& screen) {
        sync_epoch(screen);
        const size_t maximum = screen.history_lines().size();
        const size_t top = following_ ? maximum : anchor_top(screen);
        anchor_ = screen.retained_line(top).id;
        return {top, screen.retained_size(), static_cast<size_t>(screen.height()), maximum, anchor_, following_};
    }
    ViewportState scroll_to(const Screen& screen, size_t top) {
        sync_epoch(screen);
        top = (std::min)(top, screen.history_lines().size());
        following_ = top == screen.history_lines().size();
        anchor_ = screen.retained_line(top).id;
        return reconcile(screen);
    }
    ViewportState scroll_by(const Screen& screen, int64_t rows) {
        const auto state = reconcile(screen);
        size_t top;
        if (rows < 0) {
            // Avoid signed overflow for INT64_MIN and never wrap size_t.
            const auto distance = static_cast<uint64_t>(-(rows + 1)) + 1;
            top = distance > state.top ? 0 : state.top - static_cast<size_t>(distance);
        } else {
            const auto distance = static_cast<uint64_t>(rows);
            top = distance > state.maximum_top - state.top ? state.maximum_top : state.top + static_cast<size_t>(distance);
        }
        return scroll_to(screen, top);
    }
    ViewportState bottom(const Screen& screen) { return scroll_to(screen, screen.history_lines().size()); }
    ViewportState reveal(const Screen& screen, uint64_t identity) {
        if (const auto index = screen.locate(identity)) return scroll_to(screen, *index);
        return scroll_to(screen, screen.lower_bound_line(identity));
    }
    bool following() const { return following_; }
    uint64_t anchor() const { return anchor_; }
    uint64_t layout_revision() const { return layout_revision_; }
    // Explicit, read-only ASCII projection. At most 200 source fragments of at
    // most 500 cells are examined; no prefix/history expansion is performed.
    // The result owns display cells (row ids stay zero) and maps them to source
    // identities. It does not infer a display caret from the protocol cursor.
    // Legacy viewport totals describe physical rows, so projected pages leave
    // snapshot.viewport empty and use next/stop for bounded forward traversal.
    ProjectionPage project(const Screen& screen, size_t columns, size_t page_rows,
                           ProjectionCursor start, size_t source_end) {
        const auto total = screen.retained_size();
        if (columns < 1 || columns > 500 || page_rows < 1 || page_rows > 200 ||
            start.row > source_end || source_end > total || source_end - start.row > 200 ||
            (start.row == total ? start.column != 0 : start.column > screen.retained_line(start.row).size()))
            throw std::invalid_argument("invalid bounded projection request");
        if (layout_revision_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("display layout revision exhausted");
        const auto epoch = screen.buffer_epoch(), order = screen.order_revision();
        const auto bind = [&](size_t row, size_t column) {
            // A document-end sentinel binds the last retained source endpoint;
            // all other cursors bind the exact next source rank and column.
            const auto& line = screen.retained_line(row == total ? total - 1 : row);
            const auto offset = row == total ? line.size() : column;
            return ProjectionCursor{row, column, {line.id, line.version, offset, epoch}, order, true};
        };
        if (start.bound) {
            const auto current = bind(start.row, start.column);
            if (start.order_revision != order || start.source != current.source)
                throw std::invalid_argument("stale projection source cursor");
        }

        ProjectionPage result;
        result.snapshot.lines.reserve(page_rows);
        std::vector<ViewSourceSpan> sources, pending_sources;
        Line pending;
        size_t used = 0;
        ProjectionCursor pending_start;
        bool pending_active = false;
        const auto begin_row = [&](size_t row, size_t column) {
            if (pending_active) return;
            pending = Line(columns, Cell{});
            pending_sources.clear(); used = 0;
            pending_start = bind(row, column);
            pending_active = true;
        };
        const auto publish_row = [&] {
            result.snapshot.lines.push_back(std::move(pending));
            sources.insert(sources.end(), pending_sources.begin(), pending_sources.end());
            pending_sources.clear(); used = 0; pending_active = false;
        };
        const auto finish = [&](ProjectionCursor next, ProjectionStop stop) {
            result.next = bind(next.row, next.column);
            result.stop = stop;
            result.snapshot.mapping = ViewMapping(columns, std::move(sources), layout_revision_ + 1);
            ++layout_revision_;
            return std::move(result);
        };

        size_t row = start.row, column = start.column;
        bool logical_content = start.column != 0;
        ProjectionCursor last_content_end = start;
        for (; row < source_end; ++row, column = 0) {
            const auto& line = screen.retained_line(row);
            ++result.source_rows_read;
            if (line.size() > 500) throw std::invalid_argument("projection source fragment exceeds cell bound");
            if (line.content_columns > line.size())
                throw std::invalid_argument("projection content extent exceeds source fragment");
            size_t extent = line.content_columns;
            for (size_t index = 0; index < line.size(); ++index) {
                const auto& cell = line[index];
                if (cell.width != 1 || cell.text.size() != 1 ||
                    static_cast<unsigned char>(cell.text[0]) < 0x20 ||
                    static_cast<unsigned char>(cell.text[0]) > 0x7e || cell.marks ||
                    cell.clipped || cell.grapheme_fragment || cell.leading_spacer)
                    throw std::invalid_argument("explicit display projection requires single-cell ASCII sources");
                // Explicit spaces and styled erase blanks are owned content.
                // Only untouched, plain trailing padding may be synthesized.
                if (cell.written || !cell.style.plain() || cell.text != " ") extent = (std::max)(extent, index + 1);
            }
            if (column > extent)
                throw std::invalid_argument("projection starts beyond the source content extent");
            const bool joins = row + 1 < total && continues_into(line, screen.retained_line(row + 1));
            while (column < extent) {
                begin_row(row, column);
                const auto count = (std::min)(columns - used, extent - column);
                for (size_t index = 0; index < count; ++index) pending[used + index] = line[column + index];
                pending_sources.push_back({result.snapshot.lines.size(), used, used + count,
                    {{line.id, line.version, column, epoch}, {line.id, line.version, column + count, epoch}}});
                used += count; column += count;
                logical_content = true; last_content_end = {row, column};
                if (used == columns) {
                    publish_row();
                    if (result.snapshot.lines.size() == page_rows) {
                        // Keep the soft fragment's end when a page stops there:
                        // a later empty successor must not invent a hard row.
                        const auto next = column < extent || joins ? ProjectionCursor{row, column} : ProjectionCursor{row + 1, 0};
                        return finish(next, next.row == total ? ProjectionStop::source_end : ProjectionStop::page_full);
                    }
                }
            }
            if (!joins) {
                if (!extent && !logical_content) {
                    begin_row(row, 0);
                    // An empty hard source row has a real zero-length anchor;
                    // its visual padding does not receive invented identities.
                    pending_sources.push_back({result.snapshot.lines.size(), used, used,
                        {{line.id, line.version, 0, epoch}, {line.id, line.version, 0, epoch}}});
                }
                if (pending_active) {
                    publish_row();
                    if (result.snapshot.lines.size() == page_rows)
                        return finish({row + 1, 0}, row + 1 == total ? ProjectionStop::source_end : ProjectionStop::page_full);
                }
                logical_content = false;
            }
        }
        if (source_end == total) {
            if (pending_active) publish_row();
            return finish({total, 0}, ProjectionStop::source_end);
        }
        // A source-window boundary is not a hard line ending. A partial display
        // row must be retried from its original source start in a wider window;
        // the caller can observe that next did not advance and work was spent.
        const auto next = pending_active ? pending_start : logical_content ? last_content_end : ProjectionCursor{row, column};
        return finish(next, ProjectionStop::source_limit);
    }
    ViewSnapshot snapshot(const Screen& screen) {
        ViewSnapshot result;
        result.viewport = reconcile(screen);
        result.lines.reserve(result.viewport.page);
        std::vector<ViewSourceSpan> sources;
        sources.reserve(result.viewport.page);
        for (size_t i = 0; i < result.viewport.page; ++i) {
            const auto& line = screen.retained_line(result.viewport.top + i);
            result.lines.push_back(line);
            // This viewport preserves physical rows, including their existing
            // blank cells. It introduces no reflow or source-less padding.
            sources.push_back({i, 0, line.size(),
                {{line.id, line.version, 0, screen.buffer_epoch()},
                 {line.id, line.version, line.size(), screen.buffer_epoch()}}});
        }
        result.mapping = ViewMapping(static_cast<size_t>(screen.width()), std::move(sources));
        // A paused historical view must not draw a live caret over unrelated text.
        if (result.viewport.following) {
            result.cursor_row = screen.row;
            result.cursor_column = screen.column;
        }
        return result;
    }
};
} // namespace termforge
