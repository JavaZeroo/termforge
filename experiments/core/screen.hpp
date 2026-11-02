#pragma once
#include "unicode_width.hpp"
#include <algorithm>
#include <deque>
#include <cstdint>
#include <limits>
#include <ostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace termforge {
// Only malformed or unsupported terminal input uses this exception. Allocation
// failures and programming errors must propagate, including in recovery mode.
class InputError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct Style {
    int fg = -1, bg = -1; // -1 default, 0..15 palette index
    bool bold = false;
    bool dim = false; // SGR 2 faint; SGR 22 clears bold and faint together.
    bool underline = false;
    bool reverse = false;
    bool strikethrough = false;
    bool hidden = false; // SGR 8/28: the cell keeps its background only.
    bool overline = false; // SGR 53/55: a line above the cell.
    bool plain() const {
        return fg == -1 && bg == -1 && !bold && !dim && !underline && !reverse && !strikethrough && !hidden &&
               !overline;
    }
    void json(std::ostream& out) const {
        out << "{\"fg\":" << fg << ",\"bg\":" << bg << ",\"bold\":" << (bold ? "true" : "false");
        // New attributes appear only when set, so existing records keep their
        // shape while the added ones stay observable.
        if (dim) out << ",\"dim\":true";
        if (underline) out << ",\"underline\":true";
        if (reverse) out << ",\"reverse\":true";
        if (strikethrough) out << ",\"strikethrough\":true";
        if (hidden) out << ",\"hidden\":true";
        if (overline) out << ",\"overline\":true";
        out << '}';
    }
};
struct Cell {
    std::string text = " ";
    int width = 1; // 0 is the trailing cell of a width-2 scalar
    Style style;
    size_t marks = 0;
    bool written = false; // Distinguish explicit spaces from unused terminal padding.
    bool clipped = false; // Candidate retains full text in a constrained footprint.
    bool grapheme_fragment = false; // Lossless fragment after the bounded EGC budget.
    bool leading_spacer = false; // Autowrap padding; paint it but never copy it as text.
};
enum class LineEnding { none, hard_break, soft_wrap };
enum class ScreenBuffer { primary, alternate };
// A value container: moving/copying a row preserves its identity and metadata.
// Screen alone creates identities; callers must mutate through Screen.
struct Line : std::vector<Cell> {
    using std::vector<Cell>::vector;
    uint64_t id = 0, version = 0;
    LineEnding ending = LineEnding::none;
    uint64_t next_id = 0; // Only an actual autowrap creates a continuation link.
    // Used columns at the recorded autowrap, not a cap on later visible text.
    // Cursor-addressed writes preserve this event and its successor; they may
    // fill the old wrap gap or columns added by a later physical resize.
    size_t wrap_columns = 0;
    // Minimum source extent, including retained editing blanks. This is not a
    // claim that those cells were explicitly printed, nor allocated padding.
    size_t content_columns = 0;
    bool cropped = false; // Physical resize discarded cells; never implies reflow.
};

class Screen {
    struct FrameState {
        int row = 0, column = 0;
        int last_row = -1, last_column = -1;
        bool wrap_pending = false;
        size_t discarded = 0;
        Style style;
        std::vector<Line> lines;
        std::deque<Line> history;
        // True only while the retained sequence is known to be ordered by id.
        // False is the safe fallback: lookups scan instead of bisecting.
        bool ordered_ = true;
        // DECSTBM margins belong to the frame: the primary and alternate buffers
        // keep their own scrolling region across a 1049 switch.
        int scroll_top = 0;
        int scroll_bottom = -1;
        bool origin_mode_ = false;
        template<class State> void swap(State& other) noexcept {
            std::swap(row, other.row); std::swap(column, other.column);
            std::swap(scroll_top, other.scroll_top); std::swap(scroll_bottom, other.scroll_bottom);
            std::swap(origin_mode_, other.origin_mode_);
            std::swap(last_row, other.last_row); std::swap(last_column, other.last_column);
            std::swap(wrap_pending, other.wrap_pending); std::swap(discarded, other.discarded);
            std::swap(style, other.style);
            std::swap(ordered_, other.ordered_);
            lines.swap(other.lines); history.swap(other.history);
        }
    };
    // Permission belongs to the terminal; each frame retains its own edge state.
    bool autowrap_enabled_ = true;
    // Each frame owns its DECOM origin alongside its scrolling margins. A new
    // alternate frame starts with origin off; switching back restores primary.
    bool origin_mode_ = false;
    // Retained storage. Only the owner changes its membership or order:
    // resize, newline, activate_buffer, publish() and restore_frame().
    std::vector<Line> lines;
    std::deque<Line> history;
    int columns_;
    uint64_t next_id_ = 1;
    // DECSTBM margins travel with the buffer through active_state_copy/swap.
    // Negative bottom means "the last row", which survives a height change.
    int scroll_top = 0;
    int scroll_bottom = -1;
    uint64_t buffer_epoch_ = 0;
    uint64_t order_revision_ = 0;
    bool ordered_ = true;
    int last_row = -1, last_column = -1;
    ScreenBuffer active_buffer_ = ScreenBuffer::primary;
    std::optional<FrameState> inactive_;
    FrameState active_state_copy() const {
        return {row, column, last_row, last_column, wrap_pending, discarded, style, lines, history, ordered_,
                scroll_top, scroll_bottom, origin_mode_};
    }
    void swap_active(FrameState& state) noexcept {
        state.swap(*this);
    }
    static void changed(Line& line) {
        if (line.version == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("line version exhausted");
        ++line.version;
    }
    // Internal structure mutations advance the document-order revision here.
    // Overflow is reported rather than wrapped; reaching it requires 2^64
    // structural changes in one session, so it is not a caller-visible path.
    void advance_order(bool ordered) {
        if (order_revision_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("screen order revision exhausted");
        ++order_revision_;
        ordered_ = ordered;
    }
    static Line make_line(int columns, const Cell& cell, uint64_t& identity) {
        if (identity == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("line identities exhausted");
        Line line(columns, cell); line.id = identity++; line.version = 1;
        return line;
    }
    static void resize_frame(FrameState& state, int rows, int columns, size_t limit, uint64_t& identity) {
        const int shift = (std::max)(0, state.row - rows + 1);
        for (int index = 0; index < shift; ++index) {
            if (limit) {
                state.history.push_back(std::move(state.lines[index]));
                if (state.history.size() > limit) { state.history.pop_front(); ++state.discarded; }
            } else ++state.discarded;
        }
        state.lines.erase(state.lines.begin(), state.lines.begin() + shift);
        // Bottom rows outside the new viewport are dropped; their ids stay spent.
        if (state.lines.size() > static_cast<size_t>(rows)) state.lines.resize(rows);
        while (state.lines.size() < static_cast<size_t>(rows))
            state.lines.push_back(make_line(columns, Cell{}, identity));
        const auto fit = [columns](Line& line) {
            if (line.size() == static_cast<size_t>(columns)) return;
            changed(line);
            if (line.size() > static_cast<size_t>(columns)) line.cropped = true;
            line.resize(columns);
            line.content_columns = (std::min)(line.content_columns, static_cast<size_t>(columns));
            if (line.back().width == 2) line.back() = Cell{};
        };
        for (auto& line : state.lines) fit(line);
        for (auto& line : state.history) fit(line);
        state.row -= shift; state.column = (std::min)(state.column, columns - 1);
        state.wrap_pending = false;
        state.last_row = state.last_column = -1;
    }
    Cell blank() const { return {" ", 1, style, 0}; }
    // Editing a retained row uses the current protocol footprint. Preserve the
    // surviving document extent separately from Cell::written so erased blanks
    // are not mistaken for untouched allocation padding.
    static void materialize_columns(Line& line, int columns, const Cell& empty) {
        if (line.size() <= static_cast<size_t>(columns)) return;
        size_t extent = line.content_columns;
        for (size_t index = 0; index < line.size(); ++index)
            if (line[index].written || !line[index].style.plain() || line[index].text != " ")
                extent = (std::max)(extent, index + 1);
        line.resize(columns);
        line.content_columns = (std::min)(extent, line.size());
        if (line.back().width == 2) line.back() = empty;
    }
    void materialize_columns(Line& line) { materialize_columns(line, width(), blank()); }
    void break_glyph(Line& line, int col) {
        if (line[col].width == 0 && col > 0) line[col - 1] = blank();
        else if (line[col].width == 2 && col + 1 < width()) line[col + 1] = blank();
        line[col] = blank();
    }
    void erase_range(Line& line, int first, int last) {
        changed(line);
        for (int c = first; c < last; ++c) break_glyph(line, c);
        // Erasing the end destroys a recorded continuation, even if the row
        // happens to remain full of visible characters after later edits.
        if (last == width()) { line.ending = LineEnding::none; line.next_id = 0; line.wrap_columns = 0; }
        // A complete erasure replaces every remnant of the clipped row.
        // Later real wraps describe new content at the current width.
        if (first == 0 && last == width()) line.cropped = false;
    }
    static std::string text(const Line& line) {
        std::string value;
        for (const auto& cell : line) value += cell.text;
        return value;
    }
    template<class Rows> static void details(std::ostream& out, const char* key, const Rows& rows) {
        bool first = true;
        for (size_t r = 0; r < rows.size(); ++r) for (size_t c = 0; c < rows[r].size(); ++c) {
            const auto& cell = rows[r][c];
            if (cell.width == 1 && cell.style.plain() && cell.text.size() == 1) continue;
            if (first) { out << ",\"" << key << "\":["; first = false; } else out << ',';
            out << "{\"row\":" << r << ",\"column\":" << c << ",\"text\":";
            quoted(out, cell.text); out << ",\"width\":" << cell.width << ",\"style\":";
            cell.style.json(out); out << '}';
        }
        if (!first) out << ']';
    }
public:
    int row = 0, column = 0;
    bool wrap_pending = false;
    size_t discarded = 0;
    Style style;
    const size_t history_limit;
    Screen(int rows, int columns, size_t limit) : columns_(columns), history_limit(limit) {
        if (rows < 1 || rows > 200 || columns < 1 || columns > 500 || limit > 10000)
            throw std::invalid_argument("invalid screen dimensions or history limit");
        lines.reserve(rows);
        for (int i = 0; i < rows; ++i) lines.push_back(make_line(columns, Cell{}, next_id_));
    }
    int height() const { return static_cast<int>(lines.size()); }
    int width() const { return columns_; }
    ScreenBuffer active_buffer() const { return active_buffer_; }
    uint64_t buffer_epoch() const { return buffer_epoch_; }
    // Keep the edge state: enabling after a disabled last-column write must
    // still let the next printable character start the continuation.
    void set_autowrap(bool enabled) { autowrap_enabled_ = enabled; }
    // DECOM (?6): enabling homes the cursor to the region top-left, disabling to
    // the screen origin, matching the observed replies for the fixed margin sample.
    bool origin_mode() const { return origin_mode_; }
    void set_origin_mode(bool enabled) {
        origin_mode_ = enabled;
        move(enabled ? region_top() : 0, 0);
    }
    // An in-place logical reset must invalidate old readers even if the next
    // writes reproduce identical text using the same physical row storage.
    void invalidate_buffer_readers() {
        if (buffer_epoch_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("buffer epoch exhausted");
        ++buffer_epoch_;
    }
    // The configured limit belongs to primary history; the independent
    // alternate storage budget is zero. This does not define a document view.
    size_t active_history_limit() const { return active_buffer_ == ScreenBuffer::primary ? history_limit : 0; }
    // Activation exchanges owned state without applying a terminal mode's
    // clearing, inheritance or cursor-reset policy.
    void activate_buffer(ScreenBuffer buffer) {
        if (buffer == active_buffer_) return;
        if (buffer_epoch_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("buffer epoch exhausted");
        auto identity = next_id_;
        if (!inactive_) {
            FrameState target;
            target.lines.reserve(lines.size());
            for (int index = 0; index < height(); ++index)
                target.lines.push_back(make_line(width(), Cell{}, identity));
            inactive_.emplace(std::move(target));
        }
        swap_active(*inactive_);
        active_buffer_ = buffer;
        next_id_ = identity;
        ++buffer_epoch_;
        advance_order(ordered_);
    }
    // Short-lived, read-only views. Callers must not retain them across a
    // structure change; copy rows they need as values.
    const std::vector<Line>& visible_lines() const { return lines; }
    const std::deque<Line>& history_lines() const { return history; }
    size_t history_size() const { return history.size(); }
    size_t retained_size() const { return history.size() + lines.size(); }
    const Line& retained_line(size_t index) const {
        if (index >= retained_size()) throw std::out_of_range("retained line index");
        return index < history.size() ? history[index] : lines[index - history.size()];
    }
    // Rows remain ordered by their monotonically allocated id, including gaps
    // after bottom-row clipping. Lower bound also resolves an evicted anchor.
    size_t lower_bound_line(uint64_t identity) const {
        size_t first = 0, last = retained_size();
        while (first < last) {
            const size_t middle = first + (last - first) / 2;
            if (retained_line(middle).id < identity) first = middle + 1;
            else last = middle;
        }
        return first;
    }
    const Line* find_line(uint64_t identity) const {
        const auto index = locate(identity);
        return index ? &retained_line(*index) : nullptr;
    }
    // Owner-published structure revision: advances whenever retained membership,
    // document order or the history/live split changes. It never stands in for a
    // row content version or for the buffer epoch.
    uint64_t order_revision() const { return order_revision_; }
    // Exact retained rank for a live identity. A missing id is never replaced by
    // a neighbour: callers that need the old evicted-anchor fallback must ask for
    // it explicitly (lower_bound_line).
    std::optional<size_t> locate(uint64_t identity) const {
        if (!identity) return std::nullopt;
        if (ordered_) {
            const auto index = lower_bound_line(identity);
            if (index < retained_size() && retained_line(index).id == identity) return index;
            return std::nullopt;
        }
        for (size_t index = 0; index < retained_size(); ++index)
            if (retained_line(index).id == identity) return index;
        return std::nullopt;
    }
    // Restore a complete frame image captured from this same screen family
    // (replay). The restored order is not provably the active sequence any more,
    // so lookups fall back to scanning and readers lose their rank-derived state.
    // Columns and the identity allocator stay with this screen.
    void restore_frame(const Screen& origin) {
        if (order_revision_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("screen order revision exhausted");
        std::vector<Line> next_lines = origin.lines;
        std::deque<Line> next_history = origin.history;
        row = origin.row; column = origin.column;
        wrap_pending = origin.wrap_pending; discarded = origin.discarded; style = origin.style;
        lines.swap(next_lines); history.swap(next_history);
        ordered_ = origin.ordered_;
        ++order_revision_;
        invalidate_attachment();
    }
    bool order_known() const { return ordered_; }
    // Explicit publication of an owned row image (import/replay). Identities are
    // validated, never renumbered: a duplicate or zero identity is rejected
    // before anything is committed. Order is recorded so lookups stay exact.
    // Cursor, style, discarded counters and the inactive frame are untouched.
    void publish(std::vector<Line> history_rows, std::vector<Line> live_rows) {
        const auto row_width = static_cast<size_t>(columns_);
        const auto validate = [row_width](const std::vector<Line>& rows, const char* what) {
            for (const auto& line : rows) {
                if (line.size() != row_width) throw std::invalid_argument(what);
                if (line.content_columns > line.size()) throw std::invalid_argument("published content extent exceeds row");
                if (!line.id) throw std::invalid_argument("published row identity is zero");
            }
        };
        validate(history_rows, "published row width differs from the screen");
        validate(live_rows, "published row width differs from the screen");
        if (live_rows.empty() || live_rows.size() > 200)
            throw std::invalid_argument("published live rows must be between 1 and 200");
        if (history_rows.size() > active_history_limit())
            throw std::invalid_argument("published history exceeds the active retained limit");
        std::vector<uint64_t> identities;
        identities.reserve(history_rows.size() + live_rows.size());
        for (const auto& line : history_rows) identities.push_back(line.id);
        for (const auto& line : live_rows) identities.push_back(line.id);
        std::sort(identities.begin(), identities.end());
        if (std::adjacent_find(identities.begin(), identities.end()) != identities.end())
            throw std::invalid_argument("published row identity is duplicated");
        bool ordered = true;
        uint64_t maximum = next_id_ ? next_id_ - 1 : 0, previous = 0;
        bool first = true;
        const auto track = [&](const std::vector<Line>& rows) {
            for (const auto& line : rows) {
                if (!first && line.id < previous) ordered = false;
                if (line.id > maximum) maximum = line.id;
                previous = line.id; first = false;
            }
        };
        track(history_rows); track(live_rows);
        if (maximum == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("screen line identities exhausted");
        const auto differs = [](const auto& current, const std::vector<Line>& next) {
            if (current.size() != next.size()) return true;
            for (size_t index = 0; index < next.size(); ++index)
                if (current[index].id != next[index].id) return true;
            return false;
        };
        const bool changed = differs(history, history_rows) || differs(lines, live_rows);
        if (changed && order_revision_ == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("screen order revision exhausted");
        // Prepare the replacement storage before touching the published frame so
        // an allocation failure cannot leave history and live rows disagreeing.
        std::deque<Line> next_history(std::make_move_iterator(history_rows.begin()),
                                      std::make_move_iterator(history_rows.end()));
        std::vector<Line> next_live(std::make_move_iterator(live_rows.begin()),
                                    std::make_move_iterator(live_rows.end()));
        history.swap(next_history);
        lines.swap(next_live);
        if (changed) ++order_revision_;
        ordered_ = ordered;
        next_id_ = maximum + 1;
        // The replaced frame may be shorter or narrower, and the old attachment
        // points at a row that no longer exists.
        row = std::clamp(row, 0, height() - 1);
        column = std::clamp(column, 0, width() - 1);
        invalidate_attachment();
    }
    // Imported rows retain ordered unique identities; conflicts get a fresh identity and version.
    void normalize_identities(std::vector<Line>& retained) {
        uint64_t maximum = next_id_ - 1;
        for (const auto& line : retained) maximum = (std::max)(maximum, line.id);
        uint64_t previous = 0;
        for (auto& line : retained) {
            if (!line.id || line.id <= previous) {
                if (maximum == (std::numeric_limits<uint64_t>::max)())
                    throw std::overflow_error("screen line identities exhausted");
                line.id = ++maximum;
                line.version = 1;
            }
            previous = line.id;
        }
        next_id_ = maximum == (std::numeric_limits<uint64_t>::max)() ? maximum : maximum + 1;
    }
    // Width-only content transaction within this owner.
    // Active source fragments keep their cells, identities and soft links.
    // Inactive live rows fit the protocol footprint while keeping valid links;
    // discarded cells change their source versions. History is not rewritten.
    // A real width change clamps the active protocol column and cancels the
    // deferred edge write. It does not advance to the next cell when growing.
    // Margins and combining attachment remain independent protocol state.
    void resize_columns_preserving(int columns) {
        if (columns < 1 || columns > 500)
            throw std::invalid_argument("invalid screen width");
        if (columns == width()) return;
        const auto needs_padding = [columns](const auto& rows) {
            return std::any_of(rows.begin(), rows.end(), [columns](const Line& line) {
                return line.size() < static_cast<size_t>(columns);
            });
        };
        const auto prepare = [columns](const std::vector<Line>& rows) {
            auto next = rows;
            for (auto& line : next)
                if (line.size() < static_cast<size_t>(columns)) line.resize(columns);
            return next;
        };
        // No history scan/copy. Prepare every live replacement before publishing
        // either frame; a failed allocation cannot expose a partial transaction.
        std::optional<std::vector<Line>> next, hidden;
        if (needs_padding(lines)) next = prepare(lines);
        if (inactive_) {
            hidden = prepare(inactive_->lines);
            for (auto& line : *hidden) {
                if (line.size() <= static_cast<size_t>(columns)) continue;
                changed(line);
                materialize_columns(line, columns, Cell{});
                line.wrap_columns = (std::min)(line.wrap_columns, static_cast<size_t>(columns));
            }
        }
        if (next) lines.swap(*next);
        if (hidden) inactive_->lines.swap(*hidden);
        columns_ = columns;
        column = (std::min)(column, columns - 1);
        wrap_pending = false;
        if (inactive_) {
            inactive_->column = (std::min)(inactive_->column, columns - 1);
            inactive_->wrap_pending = false;
            inactive_->scroll_top = 0; inactive_->scroll_bottom = -1;
            inactive_->last_row = inactive_->last_column = -1;
        }
    }
    // Physical-row policy: no reflow, no pulling old history into the viewport.
    // Both frames share geometry. Prepare both before committing so invalid
    // dimensions or allocation failure cannot leave one frame half-resized.
    void resize(int rows, int columns) {
        if (rows < 1 || rows > 200 || columns < 1 || columns > 500)
            throw std::invalid_argument("invalid screen dimensions");
        if (rows == height() && columns == width()) return;
        auto next = active_state_copy();
        auto hidden = inactive_;
        auto identity = next_id_;
        resize_frame(next, rows, columns, active_history_limit(), identity);
        if (hidden)
            resize_frame(*hidden, rows, columns, active_buffer_ == ScreenBuffer::primary ? 0 : history_limit, identity);
        swap_active(next);
        if (hidden) inactive_->swap(*hidden);
        columns_ = columns; next_id_ = identity;
        // Physical-row resize policy resets both frames' margins so a stale
        // bottom cannot point past the new height. This is not a WindTerm claim.
        scroll_top = 0; scroll_bottom = -1;
        if (inactive_) { inactive_->scroll_top = 0; inactive_->scroll_bottom = -1; }
        advance_order(ordered_);
    }
    void invalidate_attachment() { last_row = last_column = -1; }
    void move(int r, int c) {
        // Origin mode confines the row to the scroll region; without it the whole
        // viewport is addressable. Columns are never origin-relative.
        const int top = origin_mode_ ? region_top() : 0;
        const int bottom = origin_mode_ ? region_bottom() : height() - 1;
        row = std::clamp(r, top, bottom); column = std::clamp(c, 0, width() - 1);
        wrap_pending = false; invalidate_attachment();
    }
    int region_bottom() const { return scroll_bottom < 0 ? height() - 1 : scroll_bottom; }
    int region_top() const { return scroll_top; }
    // 1-based row argument of CUP/HVP/VPA. Origin mode makes it relative to the
    // scroll-region top; without it the row is absolute.
    int position_row(int one_based) const { return (origin_mode_ ? region_top() : 0) + one_based - 1; }
    // DECSTBM: rows inside [top, bottom] scroll together. The list itself keeps
    // whole rows only, so a partial region scroll shifts entries in place; rows
    // that leave a full-screen region still reach history.
    void set_scroll_region(int top, int bottom) {
        if (top < 0) top = 0;
        if (bottom < 0 || bottom >= height()) bottom = height() - 1;
        // An invalid region is ignored rather than rejected: the reference
        // terminal logs and returns, leaving the previous margins in place.
        if (bottom - top < 1) return;
        scroll_top = top; scroll_bottom = bottom;
        // DECSTBM homes the cursor to the screen origin, matching the reference
        // terminal (vendor/alacritty_terminal/src/term/mod.rs: goto(0, 0)).
        move(0, 0);
    }
    void newline(bool automatic = false, size_t used_columns = 0) {
        const int bottom = region_bottom();
        auto& previous = lines[row];
        changed(previous);
        previous.ending = automatic ? LineEnding::soft_wrap : LineEnding::hard_break;
        previous.wrap_columns = automatic ? used_columns : 0;
        // At the bottom margin, the successor is the row about to be allocated,
        // even when a physical row still exists below the scrolling region.
        previous.next_id = automatic ? (row == bottom || row + 1 == height() ? next_id_ : lines[row + 1].id) : 0;
        wrap_pending = false; invalidate_attachment();
        // Only the bottom margin scrolls; rows above it move down normally and
        // rows below it must not disturb the region at all.
        if (row < bottom) { ++row; return; }
        if (row > bottom) { if (row + 1 < height()) ++row; return; }
        if (region_top() > 0 || bottom < height() - 1) {
            // Scrolling inside the margins moves rows up and blanks the last one.
            for (int r = region_top(); r < bottom; ++r) lines[r] = std::move(lines[r + 1]);
            lines[bottom] = make_line(width(), Cell{}, next_id_);
            row = bottom;
            // A fresh row before retained rows below the region breaks the
            // increasing-id shortcut. Exact lookup must then use document order.
            advance_order(ordered_ && bottom == height() - 1);
            return;
        }
        const auto limit = active_history_limit();
        if (limit) {
            history.push_back(std::move(lines.front()));
            if (history.size() > limit) { history.pop_front(); ++discarded; }
        } else ++discarded;
        lines.erase(lines.begin()); lines.push_back(make_line(width(), Cell{}, next_id_));
        advance_order(ordered_);
    }
    void put(uint32_t cp, const std::string& utf8) {
        const int cells = scalar_width(cp);
        if (cells < 0) throw InputError("unsupported Unicode scalar or sequence component");
        if (cells == 0) {
            if (last_row != row || last_column < 0) throw InputError("combining mark without adjacent base");
            auto& base = lines[last_row][last_column];
            if (base.marks == 16) throw InputError("combining mark limit exceeded");
            base.text += utf8; ++base.marks; changed(lines[last_row]); return;
        }
        if (cells > width()) throw InputError("wide scalar cannot fit screen");
        // Insufficient room for a wide scalar keeps the existing pre-wrap
        // policy. The permission bit only gates consuming the delayed edge.
        if ((autowrap_enabled_ && wrap_pending) || column + cells > width()) {
            const auto used = static_cast<size_t>(wrap_pending ? width() : column);
            column = 0; newline(true, used);
        }
        auto& line = lines[row];
        changed(line);
        materialize_columns(line);
        for (int c = column; c < column + cells; ++c) break_glyph(line, c);
        line[column] = {utf8, cells, style, 0, true};
        if (cells == 2) line[column + 1] = {"", 0, style, 0, true};
        last_row = row; last_column = column;
        if (column + cells == width()) { column = width() - 1; wrap_pending = true; }
        else column += cells;
    }
    void erase_line(int mode) {
        if (mode > 2) throw InputError("unsupported erase mode");
        erase_range(lines[row], mode == 0 ? column : 0, mode == 1 ? column + 1 : width());
        wrap_pending = false; invalidate_attachment();
    }
    void erase_characters(int count) {
        if (count < 1) throw InputError("invalid erase character count");
        erase_range(lines[row], column, column + (std::min)(count, width() - column));
        // ECH edits the current physical row without moving the cursor. The
        // Microsoft Terminal ECH contract explicitly cancels delayed wrapping.
        wrap_pending = false; invalidate_attachment();
    }
    void insert_lines(int count) {
        if (count < 1) throw InputError("invalid insert line count");
        const int bottom = region_bottom();
        const int insert = (std::min)(count, bottom + 1 - row);
        if (insert <= 0) { wrap_pending = false; return; }
        // Rows pushed past the bottom margin leave the screen; their identities
        // stay spent. Inserted rows are new lines and get fresh identities.
        for (int r = bottom; r >= row + insert; --r) lines[r] = std::move(lines[r - insert]);
        for (int r = row; r < row + insert; ++r) lines[r] = make_line(width(), Cell{}, next_id_);
        wrap_pending = false;
        invalidate_attachment();
        // New ids may precede surviving rows; readers must refresh cached ranks.
        advance_order(false);
    }
    void delete_lines(int count) {
        if (count < 1) throw InputError("invalid delete line count");
        const int bottom = region_bottom();
        const int remove = (std::min)(count, bottom + 1 - row);
        if (remove <= 0) { wrap_pending = false; return; }
        for (int r = row; r <= bottom - remove; ++r) lines[r] = std::move(lines[r + remove]);
        for (int r = bottom - remove + 1; r <= bottom; ++r) lines[r] = make_line(width(), Cell{}, next_id_);
        wrap_pending = false;
        invalidate_attachment();
        advance_order(ordered_ && bottom == height() - 1);
    }
    void insert_characters(int count) {
        if (count < 1) throw InputError("invalid insert character count");
        const int shift = (std::min)(count, width() - column);
        if (shift <= 0) { wrap_pending = false; return; }
        Line& line = lines[row];
        changed(line);
        materialize_columns(line);
        // A wide glyph cannot be split: blank the pair that straddles either
        // boundary instead of leaving half a character behind.
        if (column > 0 && line[column].width == 0) break_glyph(line, column - 1);
        if (line[width() - shift].width == 2) break_glyph(line, width() - shift);
        for (int c = width() - 1; c >= column + shift; --c) line[c] = line[c - shift];
        erase_range(line, column, column + shift);
        wrap_pending = false; invalidate_attachment();
    }
    void delete_characters(int count) {
        if (count < 1) throw InputError("invalid delete character count");
        const int shift = (std::min)(count, width() - column);
        if (shift <= 0) { wrap_pending = false; return; }
        Line& line = lines[row];
        changed(line);
        materialize_columns(line);
        if (column > 0 && line[column].width == 0) break_glyph(line, column);
        if (line[width() - shift].width == 2) break_glyph(line, width() - shift);
        for (int c = column; c < width() - shift; ++c) line[c] = line[c + shift];
        erase_range(line, width() - shift, width());
        wrap_pending = false; invalidate_attachment();
    }
    void reset() {
        // Clear the active text/history and reset the supported modes before
        // returning to primary. Identities already spent stay spent.
        for (int r = 0; r < height(); ++r) erase_range(lines[r], 0, width());
        if (!history.empty()) {
            history.clear();
            advance_order(ordered_);
        }
        style = Style{};
        row = column = 0;
        wrap_pending = false;
        autowrap_enabled_ = true;
        origin_mode_ = false;
        // Preserve the existing RIS origin-off policy across the saved frame.
        if (inactive_) inactive_->origin_mode_ = false;
        activate_buffer(ScreenBuffer::primary);
        invalidate_attachment();
    }
    size_t scrollback_lines() const { return history.size(); }
    void erase_scrollback() {
        // ED3 drops scrollback only; the visible screen is untouched.
        if (!history.empty()) {
            history.clear();
            advance_order(ordered_);
        }
        discarded = 0;
    }
    void erase_display(int mode) {
        if (mode == 3) { erase_scrollback(); return; }
        if (mode > 2) throw InputError("unsupported erase mode");
        for (int r = 0; r < height(); ++r)
            if (mode == 2 || (mode == 0 && r > row) || (mode == 1 && r < row)) erase_range(lines[r], 0, width());
        erase_line(mode);
    }
    void sgr(const std::vector<int>& params) {
        Style next = style;
        for (int p : params) {
            if (p == 0) next = {};
            else if (p == 1) next.bold = true;
            else if (p == 2) next.dim = true;
            // SGR 22 means "normal intensity": it clears bold and faint together.
            else if (p == 22) { next.bold = false; next.dim = false; }
            else if (p == 4) next.underline = true;
            else if (p == 24) next.underline = false;
            else if (p == 7) next.reverse = true;
            else if (p == 27) next.reverse = false;
            else if (p == 9) next.strikethrough = true;
            else if (p == 29) next.strikethrough = false;
            else if (p == 8) next.hidden = true;
            else if (p == 28) next.hidden = false;
            else if (p == 53) next.overline = true;
            else if (p == 55) next.overline = false;
            else if (p >= 30 && p <= 37) next.fg = p - 30;
            else if (p >= 40 && p <= 47) next.bg = p - 40;
            else if (p >= 90 && p <= 97) next.fg = p - 90 + 8;
            else if (p >= 100 && p <= 107) next.bg = p - 100 + 8;
            else if (p == 39) next.fg = -1;
            else if (p == 49) next.bg = -1;
            else throw InputError("unsupported SGR parameter");
        }
        style = next;
    }
    static void quoted(std::ostream& out, const std::string& value) {
        out << '"';
        static constexpr char hex[] = "0123456789abcdef";
        for (unsigned char c : value) {
            if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
            else { if (c == '"' || c == '\\') out << '\\'; out << static_cast<char>(c); }
        }
        out << '"';
    }
    void json(std::ostream& out) const {
        out << "{\"row\":" << row << ",\"column\":" << column
            << ",\"wrap_pending\":" << (wrap_pending ? "true" : "false")
            << ",\"discarded_history_lines\":" << discarded << ",\"lines\":[";
        for (size_t i = 0; i < lines.size(); ++i) { if (i) out << ','; quoted(out, text(lines[i])); }
        out << "],\"history\":[";
        for (size_t i = 0; i < history.size(); ++i) { if (i) out << ','; quoted(out, text(history[i])); }
        out << ']';
        details(out, "cell_details", lines); details(out, "history_cell_details", history);
        if (!style.plain()) { out << ",\"current_style\":"; style.json(out); }
        out << '}';
    }
};
} // namespace termforge
