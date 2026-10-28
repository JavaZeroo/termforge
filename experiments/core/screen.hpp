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
    bool plain() const { return fg == -1 && bg == -1 && !bold; }
    void json(std::ostream& out) const {
        out << "{\"fg\":" << fg << ",\"bg\":" << bg << ",\"bold\":" << (bold ? "true" : "false") << '}';
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
        template<class State> void swap(State& other) noexcept {
            std::swap(row, other.row); std::swap(column, other.column);
            std::swap(last_row, other.last_row); std::swap(last_column, other.last_column);
            std::swap(wrap_pending, other.wrap_pending); std::swap(discarded, other.discarded);
            std::swap(style, other.style);
            lines.swap(other.lines); history.swap(other.history);
        }
    };
    // Permission belongs to the terminal; each frame retains its own edge state.
    bool autowrap_enabled_ = true;
    int columns_;
    uint64_t next_id_ = 1;
    uint64_t buffer_epoch_ = 0;
    int last_row = -1, last_column = -1;
    ScreenBuffer active_buffer_ = ScreenBuffer::primary;
    std::optional<FrameState> inactive_;
    FrameState active_state_copy() const {
        return {row, column, last_row, last_column, wrap_pending, discarded, style, lines, history};
    }
    void swap_active(FrameState& state) noexcept {
        state.swap(*this);
    }
    static void changed(Line& line) {
        if (line.version == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("line version exhausted");
        ++line.version;
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
            if (line.back().width == 2) line.back() = Cell{};
        };
        for (auto& line : state.lines) fit(line);
        for (auto& line : state.history) fit(line);
        state.row -= shift; state.column = (std::min)(state.column, columns - 1);
        state.wrap_pending = false;
        state.last_row = state.last_column = -1;
    }
    Cell blank() const { return {" ", 1, style, 0}; }
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
    std::vector<Line> lines;
    std::deque<Line> history;
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
    }
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
        const auto index = lower_bound_line(identity);
        return index < retained_size() && retained_line(index).id == identity ? &retained_line(index) : nullptr;
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
    }
    void invalidate_attachment() { last_row = last_column = -1; }
    void move(int r, int c) {
        row = std::clamp(r, 0, height() - 1); column = std::clamp(c, 0, width() - 1);
        wrap_pending = false; invalidate_attachment();
    }
    void newline(bool automatic = false, size_t used_columns = 0) {
        auto& previous = lines[row];
        changed(previous);
        previous.ending = automatic ? LineEnding::soft_wrap : LineEnding::hard_break;
        previous.wrap_columns = automatic ? used_columns : 0;
        // A newly allocated bottom row is known before moving its predecessor.
        previous.next_id = automatic ? (row + 1 < height() ? lines[row + 1].id : next_id_) : 0;
        wrap_pending = false; invalidate_attachment();
        if (row + 1 < height()) { ++row; return; }
        const auto limit = active_history_limit();
        if (limit) {
            history.push_back(std::move(lines.front()));
            if (history.size() > limit) { history.pop_front(); ++discarded; }
        } else ++discarded;
        lines.erase(lines.begin()); lines.push_back(make_line(width(), Cell{}, next_id_));
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
        if (!autowrap_enabled_ && column + cells > width())
            throw InputError("wide scalar cannot fit with autowrap disabled");
        if ((autowrap_enabled_ && wrap_pending) || column + cells > width()) {
            const auto used = static_cast<size_t>(wrap_pending ? width() : column);
            column = 0; newline(true, used);
        }
        auto& line = lines[row];
        changed(line);
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
    void erase_display(int mode) {
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
            else if (p == 22) next.bold = false;
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
