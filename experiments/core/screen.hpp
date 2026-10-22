#pragma once
#include "unicode_width.hpp"
#include <algorithm>
#include <deque>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace termforge {
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
};
using Line = std::vector<Cell>;

class Screen {
    int columns_;
    int last_row = -1, last_column = -1;
    Cell blank() const { return {" ", 1, style, 0}; }
    void break_glyph(Line& line, int col) {
        if (line[col].width == 0 && col > 0) line[col - 1] = blank();
        else if (line[col].width == 2 && col + 1 < width()) line[col + 1] = blank();
        line[col] = blank();
    }
    void erase_range(Line& line, int first, int last) {
        for (int c = first; c < last; ++c) break_glyph(line, c);
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
        lines.assign(rows, Line(columns));
    }
    int height() const { return static_cast<int>(lines.size()); }
    int width() const { return columns_; }
    void invalidate_attachment() { last_row = last_column = -1; }
    void move(int r, int c) {
        row = std::clamp(r, 0, height() - 1); column = std::clamp(c, 0, width() - 1);
        wrap_pending = false; invalidate_attachment();
    }
    void newline() {
        wrap_pending = false; invalidate_attachment();
        if (row + 1 < height()) { ++row; return; }
        if (history_limit) {
            history.push_back(std::move(lines.front()));
            if (history.size() > history_limit) { history.pop_front(); ++discarded; }
        } else ++discarded;
        lines.erase(lines.begin()); lines.emplace_back(width(), blank());
    }
    void put(uint32_t cp, const std::string& utf8) {
        const int cells = scalar_width(cp);
        if (cells < 0) throw std::runtime_error("unsupported Unicode scalar or sequence component");
        if (cells == 0) {
            if (last_row != row || last_column < 0) throw std::runtime_error("combining mark without adjacent base");
            auto& base = lines[last_row][last_column];
            if (base.marks == 16) throw std::runtime_error("combining mark limit exceeded");
            base.text += utf8; ++base.marks; return;
        }
        if (cells > width()) throw std::runtime_error("wide scalar cannot fit screen");
        if (wrap_pending || column + cells > width()) { column = 0; newline(); }
        auto& line = lines[row];
        for (int c = column; c < column + cells; ++c) break_glyph(line, c);
        line[column] = {utf8, cells, style, 0};
        if (cells == 2) line[column + 1] = {"", 0, style, 0};
        last_row = row; last_column = column;
        if (column + cells == width()) { column = width() - 1; wrap_pending = true; }
        else column += cells;
    }
    void erase_line(int mode) {
        if (mode > 2) throw std::runtime_error("unsupported erase mode");
        erase_range(lines[row], mode == 0 ? column : 0, mode == 1 ? column + 1 : width());
        wrap_pending = false; invalidate_attachment();
    }
    void erase_display(int mode) {
        if (mode > 2) throw std::runtime_error("unsupported erase mode");
        for (int r = 0; r < height(); ++r)
            if (mode == 2 || (mode == 0 && r > row) || (mode == 1 && r < row)) erase_range(lines[r], 0, width());
        erase_line(mode);
    }
    void sgr(const std::vector<int>& params) {
        for (int p : params) {
            if (p == 0) style = {};
            else if (p == 1) style.bold = true;
            else if (p == 22) style.bold = false;
            else if (p >= 30 && p <= 37) style.fg = p - 30;
            else if (p >= 40 && p <= 47) style.bg = p - 40;
            else if (p >= 90 && p <= 97) style.fg = p - 90 + 8;
            else if (p >= 100 && p <= 107) style.bg = p - 100 + 8;
            else if (p == 39) style.fg = -1;
            else if (p == 49) style.bg = -1;
            else throw std::runtime_error("unsupported SGR parameter");
        }
    }
    static void quoted(std::ostream& out, const std::string& value) {
        out << '"';
        for (char c : value) { if (c == '"' || c == '\\') out << '\\'; out << c; }
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
