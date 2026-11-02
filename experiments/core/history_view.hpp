// Independent physical-row history viewport. No HWND, parser or transport state.
#pragma once
#include "view_mapping.hpp"
#include <memory>
#include <optional>

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
// to the current source identity/version, epoch, owner and order revision.
// It is a source anchor, not a width-bound checkpoint or global display rank.
struct ProjectionCursor {
    size_t row = 0, column = 0;
    TextPosition source;
    uint64_t order_revision = 0;
    bool bound = false;
    const Screen* owner = nullptr;
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
class HistoryViewport;
// A single-owner, move-only preparation. Screen and viewport must outlive it,
// retain their addresses, and not be replaced by assignment while it is active.
// No references into Screen storage survive a step; only bounded values do.
class ProjectionJob {
    friend class HistoryViewport;
    struct State {
        const Screen* owner = nullptr;
        const HistoryViewport* viewport = nullptr;
        uint64_t epoch = 0, order = 0, content = 0, request = 0;
        size_t columns = 0, page_rows = 0, total = 0, row = 0, column = 0;
        size_t used = 0, source_rows_read = 0;
        bool logical_content = false, pending_active = false;
        Line pending;
        std::vector<Line> completed;
        std::vector<ViewSourceSpan> sources, pending_sources;
    };
    std::unique_ptr<State> state_;
public:
    ProjectionJob() = default;
    ProjectionJob(ProjectionJob&&) noexcept = default;
    ProjectionJob& operator=(ProjectionJob&&) noexcept = default;
    ProjectionJob(const ProjectionJob&) = delete;
    ProjectionJob& operator=(const ProjectionJob&) = delete;
};
enum class ProjectionStepStatus { yielded, ready };
struct ProjectionStep {
    ProjectionStepStatus status = ProjectionStepStatus::yielded;
    // Work in this step only, never more than the requested budget (<=200).
    size_t source_rows_read = 0;
    // Absent until the requested page or document end is complete. The page's
    // source_rows_read is cumulative across all steps of this preparation.
    std::optional<ProjectionPage> page;
};
class HistoryViewport {
    uint64_t anchor_ = 0, observed_epoch_ = 0;
    uint64_t layout_revision_ = 0;
    uint64_t projection_request_ = 0;
    bool following_ = true;
    // Projection accepts the core's bounded base-scalar plus combining-mark
    // representation. Imported Cell values need the same structural check;
    // accepting arbitrary text would defeat the per-page work bound.
    static bool complete_atom(const Cell& cell) {
        if ((cell.width != 1 && cell.width != 2) || cell.text.empty() ||
            cell.text.size() > 68 || cell.marks > 16) return false;
        size_t count = 0;
        uint32_t scalar = 0, minimum = 0;
        unsigned remaining = 0;
        for (const unsigned char byte : cell.text) {
            if (!remaining) {
                if (byte < 0x80) { scalar = byte; minimum = 0; }
                else if (byte >= 0xc2 && byte <= 0xdf) { scalar = byte & 0x1f; minimum = 0x80; remaining = 1; }
                else if (byte >= 0xe0 && byte <= 0xef) { scalar = byte & 0xf; minimum = 0x800; remaining = 2; }
                else if (byte >= 0xf0 && byte <= 0xf4) { scalar = byte & 7; minimum = 0x10000; remaining = 3; }
                else return false;
                if (remaining) continue;
            } else {
                if ((byte & 0xc0) != 0x80) return false;
                scalar = (scalar << 6) | (byte & 0x3f);
                if (--remaining) continue;
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
                scalar_width(scalar) != (count ? 0 : cell.width)) return false;
            ++count;
        }
        return !remaining && count == cell.marks + 1;
    }
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
    // A successful begin supersedes earlier preparations, but does not publish
    // or invalidate the displayed page. Failed preparation leaves them intact.
    // Start is the local display origin for this request. A new width may use
    // the same source anchor; this does not preserve a global visual row number
    // or infer preceding wrap alignment. No historical prefix is traversed.
    ProjectionJob begin_projection(const Screen& screen, size_t columns, size_t page_rows,
                                   ProjectionCursor start = {0, 0}) {
        const auto total = screen.retained_size();
        if (columns < 1 || columns > 500 || page_rows < 1 || page_rows > 200 ||
            start.row > total ||
            (start.row == total ? start.column != 0 : start.column > screen.retained_line(start.row).size()))
            throw std::invalid_argument("invalid projection job request");
        if (projection_request_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("projection request revision exhausted");
        const auto& first = screen.retained_line(start.row == total ? total - 1 : start.row);
        const TextPosition source{first.id, first.version,
            start.row == total ? first.size() : start.column, screen.buffer_epoch()};
        if (start.bound && (start.owner != &screen || start.order_revision != screen.order_revision() ||
                            start.source != source))
            throw std::invalid_argument("stale or foreign projection source cursor");
        ProjectionJob job;
        auto state = std::make_unique<ProjectionJob::State>();
        state->completed.reserve(page_rows);
        state->owner = &screen; state->viewport = this;
        state->epoch = screen.buffer_epoch(); state->order = screen.order_revision();
        state->content = screen.content_revision(); state->request = projection_request_ + 1;
        state->columns = columns; state->page_rows = page_rows; state->total = total;
        state->row = start.row; state->column = start.column;
        state->logical_content = start.column != 0;
        job.state_ = std::move(state);
        ++projection_request_;
        return job;
    }
    // Cancellation never invalidates another preparation, including a newer
    // request or a job owned by a different viewport.
    void cancel_projection(ProjectionJob& job) noexcept {
        if (job.state_ && job.state_->viewport == this) job.state_.reset();
    }
    // The source owner is read on its own thread. Between steps, any relevant
    // mutation invalidates the entire unpublished preparation. In particular,
    // content_revision guards earlier consumed rows, not just the next cursor.
    // Every exception cancels an owned job; a foreign viewport cannot alter it.
    ProjectionStep step_projection(const Screen& screen, ProjectionJob& job, size_t source_budget = 200) {
        if (!job.state_ || job.state_->viewport != this)
            throw std::invalid_argument("missing or foreign projection job");
        try {
            auto& state = *job.state_;
            if (source_budget < 1 || source_budget > 200)
                throw std::invalid_argument("invalid projection step budget");
            if (state.owner != &screen || state.epoch != screen.buffer_epoch() ||
                state.order != screen.order_revision() || state.content != screen.content_revision() ||
                state.request != projection_request_ || state.total != screen.retained_size())
                throw std::invalid_argument("stale or foreign projection job source");
            if (layout_revision_ == (std::numeric_limits<uint64_t>::max)())
                throw std::overflow_error("display layout revision exhausted");
            ProjectionStep result;
            const auto bind = [&](size_t row, size_t column) {
                const auto& line = screen.retained_line(row == state.total ? state.total - 1 : row);
                return ProjectionCursor{row, column,
                    {line.id, line.version, row == state.total ? line.size() : column, state.epoch},
                    state.order, true, &screen};
            };
            const auto begin_row = [&] {
                if (state.pending_active) return;
                state.pending = Line(state.columns, Cell{});
                state.pending_sources.clear(); state.used = 0; state.pending_active = true;
            };
            const auto publish_row = [&] {
                state.completed.push_back(std::move(state.pending));
                state.sources.insert(state.sources.end(), state.pending_sources.begin(), state.pending_sources.end());
                state.pending_sources.clear(); state.used = 0; state.pending_active = false;
            };
            const auto finish = [&](size_t row, size_t column, ProjectionStop stop) {
                ProjectionPage page;
                page.next = bind(row, column); page.stop = stop;
                page.source_rows_read = state.source_rows_read;
                page.snapshot.lines = std::move(state.completed);
                page.snapshot.mapping = ViewMapping(state.columns, std::move(state.sources), layout_revision_ + 1);
                result.page.emplace(std::move(page));
                result.status = ProjectionStepStatus::ready;
                ++layout_revision_;
            };
            while (state.row < state.total && result.source_rows_read < source_budget) {
                const auto& line = screen.retained_line(state.row);
                ++result.source_rows_read; ++state.source_rows_read;
                if (line.size() > 500 || line.content_columns > line.size())
                    throw std::invalid_argument("projection source fragment exceeds bounds");
                size_t extent = line.content_columns;
                for (size_t index = 0; index < line.size();) {
                    const auto& cell = line[index];
                    if (!complete_atom(cell) || cell.clipped || cell.grapheme_fragment || cell.leading_spacer)
                        throw std::invalid_argument("projection requires complete bounded core Unicode atoms");
                    const auto cells = static_cast<size_t>(cell.width);
                    if (cells == 2) {
                        if (index + 1 == line.size())
                            throw std::invalid_argument("projection wide atom has no trailing cell");
                        const auto& trail = line[index + 1];
                        if (trail.width != 0 || !trail.text.empty() || trail.marks ||
                            trail.clipped || trail.grapheme_fragment || trail.leading_spacer)
                            throw std::invalid_argument("projection wide atom has an invalid trailing cell");
                    }
                    if (cell.written || !cell.style.plain() || cell.text != " ")
                        extent = (std::max)(extent, index + cells);
                    index += cells;
                }
                if (state.column > extent || (state.column < line.size() && line[state.column].width == 0))
                    throw std::invalid_argument("invalid projection source atom boundary");
                const bool joins = state.row + 1 < state.total && continues_into(line, screen.retained_line(state.row + 1));
                while (state.column < extent) {
                    begin_row();
                    if (static_cast<size_t>(line[state.column].width) > state.columns)
                        throw std::invalid_argument("projection width cannot fit a complete atom");
                    size_t count = 0;
                    while (state.column + count < extent) {
                        const auto cells = static_cast<size_t>(line[state.column + count].width);
                        if (cells > state.columns - state.used - count) break;
                        count += cells;
                    }
                    if (!count) {
                        publish_row();
                        if (state.completed.size() == state.page_rows) {
                            finish(state.row, state.column, ProjectionStop::page_full);
                            job.state_.reset(); return result;
                        }
                        continue;
                    }
                    for (size_t index = 0; index < count; ++index)
                        state.pending[state.used + index] = line[state.column + index];
                    state.pending_sources.push_back({state.completed.size(), state.used, state.used + count,
                        {{line.id, line.version, state.column, state.epoch},
                         {line.id, line.version, state.column + count, state.epoch}}});
                    state.used += count; state.column += count; state.logical_content = true;
                    if (state.used == state.columns) {
                        publish_row();
                        if (state.completed.size() == state.page_rows) {
                            const bool same_row = state.column < extent || joins;
                            const auto row = same_row ? state.row : state.row + 1;
                            finish(row, same_row ? state.column : 0,
                                   row == state.total ? ProjectionStop::source_end : ProjectionStop::page_full);
                            job.state_.reset(); return result;
                        }
                    }
                }
                if (!joins) {
                    if (!extent && !state.logical_content) {
                        begin_row();
                        state.pending_sources.push_back({state.completed.size(), state.used, state.used,
                            {{line.id, line.version, 0, state.epoch}, {line.id, line.version, 0, state.epoch}}});
                    }
                    if (state.pending_active) {
                        publish_row();
                        if (state.completed.size() == state.page_rows) {
                            const auto row = state.row + 1;
                            finish(row, 0, row == state.total ? ProjectionStop::source_end : ProjectionStop::page_full);
                            job.state_.reset(); return result;
                        }
                    }
                    state.logical_content = false;
                }
                ++state.row; state.column = 0;
            }
            if (state.row == state.total) {
                if (state.pending_active) publish_row();
                finish(state.total, 0, ProjectionStop::source_end);
                job.state_.reset();
            }
            return result;
        } catch (...) {
            job.state_.reset();
            throw;
        }
    }
    // Explicit, read-only projection of complete core Unicode atoms. At most
    // 200 source fragments of 500 cells, with 68 bytes per lead, are examined;
    // no prefix/history expansion is performed. A wide pair never splits.
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
        if (projection_request_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("projection request revision exhausted");
        const auto epoch = screen.buffer_epoch(), order = screen.order_revision();
        const auto bind = [&](size_t row, size_t column) {
            // A document-end sentinel binds the last retained source endpoint;
            // all other cursors bind the exact next source rank and column.
            const auto& line = screen.retained_line(row == total ? total - 1 : row);
            const auto offset = row == total ? line.size() : column;
            return ProjectionCursor{row, column, {line.id, line.version, offset, epoch}, order, true, &screen};
        };
        if (start.bound) {
            const auto current = bind(start.row, start.column);
            if (start.owner != &screen || start.order_revision != order || start.source != current.source)
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
            ++projection_request_;
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
            for (size_t index = 0; index < line.size();) {
                const auto& cell = line[index];
                if (!complete_atom(cell) || cell.clipped || cell.grapheme_fragment || cell.leading_spacer)
                    throw std::invalid_argument("projection requires complete bounded core Unicode atoms");
                const auto cells = static_cast<size_t>(cell.width);
                if (cells == 2) {
                    if (index + 1 == line.size())
                        throw std::invalid_argument("projection wide atom has no trailing cell");
                    const auto& trail = line[index + 1];
                    if (trail.width != 0 || !trail.text.empty() || trail.marks ||
                        trail.clipped || trail.grapheme_fragment || trail.leading_spacer)
                        throw std::invalid_argument("projection wide atom has an invalid trailing cell");
                }
                // Explicit spaces and styled erase blanks are owned content.
                // Only untouched, plain trailing padding may be synthesized.
                if (cell.written || !cell.style.plain() || cell.text != " ") extent = (std::max)(extent, index + cells);
                index += cells;
            }
            if (column > extent)
                throw std::invalid_argument("projection starts beyond the source content extent");
            if (column < line.size() && line[column].width == 0)
                throw std::invalid_argument("projection starts inside a wide atom");
            const bool joins = row + 1 < total && continues_into(line, screen.retained_line(row + 1));
            while (column < extent) {
                begin_row(row, column);
                if (static_cast<size_t>(line[column].width) > columns)
                    throw std::invalid_argument("projection width cannot fit a complete atom");
                size_t count = 0;
                while (column + count < extent) {
                    const auto cells = static_cast<size_t>(line[column + count].width);
                    if (cells > columns - used - count) break;
                    count += cells;
                }
                if (!count) {
                    // The remaining column is display-only padding. Resume at
                    // the same source lead after publishing the preceding row.
                    publish_row();
                    if (result.snapshot.lines.size() == page_rows)
                        return finish({row, column}, ProjectionStop::page_full);
                    continue;
                }
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
