#pragma once
#include "unicode_width.hpp"
#include <algorithm>
#include <deque>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
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
};
enum class LineEnding { none, hard_break, soft_wrap };
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
    int columns_;
    uint64_t next_id_ = 1;
    int last_row = -1, last_column = -1;
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
    // Prepare copies first so invalid sizes/allocation failure leave state intact.
    void resize(int rows, int columns) {
        if (rows < 1 || rows > 200 || columns < 1 || columns > 500)
            throw std::invalid_argument("invalid screen dimensions");
        if (rows == height() && columns == width()) return;
        auto next = lines;
        auto past = history;
        auto lost = discarded;
        auto identity = next_id_;
        const int shift = (std::max)(0, row - rows + 1);
        for (int i = 0; i < shift; ++i) {
            if (history_limit) {
                past.push_back(std::move(next[i]));
                if (past.size() > history_limit) { past.pop_front(); ++lost; }
            } else ++lost;
        }
        next.erase(next.begin(), next.begin() + shift);
        // Bottom rows outside the new viewport are dropped. Never reuse their ids.
        if (next.size() > static_cast<size_t>(rows)) next.resize(rows);
        while (next.size() < static_cast<size_t>(rows)) next.push_back(make_line(columns, Cell{}, identity));
        const auto fit = [columns](Line& line) {
            if (line.size() == static_cast<size_t>(columns)) return;
            changed(line);
            if (line.size() > static_cast<size_t>(columns)) line.cropped = true;
            line.resize(columns);
            // A truncated wide glyph must never leave a dangling leading cell.
            if (line.back().width == 2) line.back() = Cell{};
        };
        for (auto& line : next) fit(line);
        for (auto& line : past) fit(line);
        lines.swap(next); history.swap(past); discarded = lost; columns_ = columns; next_id_ = identity;
        row -= shift; column = (std::min)(column, columns - 1);
        wrap_pending = false; invalidate_attachment();
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
        if (history_limit) {
            history.push_back(std::move(lines.front()));
            if (history.size() > history_limit) { history.pop_front(); ++discarded; }
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
        if (wrap_pending || column + cells > width()) {
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
