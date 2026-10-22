// Original, deliberately limited ASCII terminal experiment; see README for contract.
#pragma once
#include <algorithm>
#include <deque>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace termforge {
class Screen {
public:
    int row = 0, column = 0;
    bool wrap_pending = false;
    size_t discarded = 0;
    std::vector<std::string> lines;
    std::deque<std::string> history;
    const size_t history_limit;
    Screen(int rows, int columns, size_t limit) : history_limit(limit) {
        if (rows < 1 || rows > 200 || columns < 1 || columns > 500 || limit > 10000)
            throw std::invalid_argument("invalid screen dimensions or history limit");
        lines.assign(rows, std::string(columns, ' '));
    }
    int height() const { return static_cast<int>(lines.size()); }
    int width() const { return static_cast<int>(lines[0].size()); }
    void move(int r, int c) {
        row = std::clamp(r, 0, height() - 1); column = std::clamp(c, 0, width() - 1); wrap_pending = false;
    }
    void newline() {
        wrap_pending = false;
        if (row + 1 < height()) { ++row; return; }
        if (history_limit) {
            history.push_back(lines.front());
            if (history.size() > history_limit) { history.pop_front(); ++discarded; }
        } else ++discarded;
        const int columns = width();
        lines.erase(lines.begin()); lines.emplace_back(columns, ' ');
    }
    void put(char c) {
        if (wrap_pending) { column = 0; newline(); }
        lines[row][column] = c;
        if (column == width() - 1) wrap_pending = true;
        else ++column;
    }
    void erase_line(int mode) {
        if (mode > 2) throw std::runtime_error("unsupported erase mode");
        int first = mode == 0 ? column : 0;
        int last = mode == 1 ? column + 1 : width();
        std::fill(lines[row].begin() + first, lines[row].begin() + last, ' ');
        wrap_pending = false;
    }
    void erase_display(int mode) {
        if (mode > 2) throw std::runtime_error("unsupported erase mode");
        for (int r = 0; r < height(); ++r) {
            if (mode == 2 || (mode == 0 && r > row) || (mode == 1 && r < row))
                std::fill(lines[r].begin(), lines[r].end(), ' ');
        }
        erase_line(mode);
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
        for (size_t i = 0; i < lines.size(); ++i) { if (i) out << ','; quoted(out, lines[i]); }
        out << "],\"history\":[";
        for (size_t i = 0; i < history.size(); ++i) { if (i) out << ','; quoted(out, history[i]); }
        out << "]}";
    }
};

class Parser {
    enum class State { ground, escape, csi } state = State::ground;
    std::vector<int> params;
    size_t sequence_bytes = 0;
    void dispatch(unsigned char final) {
        if (params.size() > ((final == 'H' || final == 'f') ? 2u : 1u))
            throw std::runtime_error("too many CSI parameters");
        const int n = params[0] == 0 ? 1 : params[0];
        switch (final) {
        case 'A': screen.move(screen.row - n, screen.column); break;
        case 'B': screen.move(screen.row + n, screen.column); break;
        case 'C': screen.move(screen.row, screen.column + n); break;
        case 'D': screen.move(screen.row, screen.column - n); break;
        case 'G': screen.move(screen.row, n - 1); break;
        case 'd': screen.move(n - 1, screen.column); break;
        case 'H': case 'f': screen.move(n - 1, params.size() < 2 || params[1] == 0 ? 0 : params[1] - 1); break;
        case 'J': screen.erase_display(params[0]); break;
        case 'K': screen.erase_line(params[0]); break;
        default: throw std::runtime_error("unsupported CSI command");
        }
    }
public:
    Screen screen;
    Parser(int rows = 24, int columns = 80, size_t history = 100) : screen(rows, columns, history) {}
    void feed(std::string_view bytes) {
        for (unsigned char c : bytes) {
            if (state == State::escape) {
                if (c != '[') throw std::runtime_error("unsupported escape");
                state = State::csi; params.assign(1, 0); sequence_bytes = 0; continue;
            }
            if (state == State::csi) {
                if (++sequence_bytes > 32) throw std::runtime_error("CSI exceeds limit");
                if (c >= '0' && c <= '9') {
                    if (params.back() > 9999) throw std::runtime_error("CSI parameter exceeds limit");
                    params.back() = params.back() * 10 + c - '0';
                } else if (c == ';') {
                    if (params.size() == 2) throw std::runtime_error("too many CSI parameters");
                    params.push_back(0);
                } else { dispatch(c); state = State::ground; }
                continue;
            }
            if (c >= 32 && c <= 126) screen.put(static_cast<char>(c));
            else switch (c) {
            case 27: state = State::escape; break;
            case '\r': screen.move(screen.row, 0); break;
            case '\n': screen.newline(); break;
            case '\b': screen.move(screen.row, screen.column - 1); break;
            case '\t': screen.move(screen.row, (screen.column / 8 + 1) * 8); break;
            default: throw std::runtime_error("unsupported input byte");
            }
        }
    }
    void finish() const { if (state != State::ground) throw std::runtime_error("incomplete sequence at EOF"); }
};
} // namespace termforge
