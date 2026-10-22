// Original bounded UTF-8 terminal experiment; see README for contract.
#pragma once
#include "screen.hpp"
#include <string_view>

namespace termforge {
class Parser {
    enum class State { ground, escape, csi } state = State::ground;
    std::vector<int> params;
    size_t sequence_bytes = 0;
    uint32_t scalar = 0, minimum = 0;
    int remaining = 0;
    std::string encoded;
    void dispatch(unsigned char final) {
        if (final == 'm') { screen.sgr(params); return; }
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
            if (remaining) {
                if ((c & 0xC0) != 0x80) throw std::runtime_error("invalid UTF-8 continuation");
                encoded += static_cast<char>(c); scalar = (scalar << 6) | (c & 0x3F);
                if (--remaining == 0) {
                    if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF))
                        throw std::runtime_error("invalid UTF-8 scalar");
                    screen.put(scalar, encoded);
                }
                continue;
            }
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
                    if (params.size() == 16) throw std::runtime_error("too many CSI parameters");
                    params.push_back(0);
                } else { dispatch(c); state = State::ground; }
                continue;
            }
            if (c >= 0x80) {
                encoded.assign(1, static_cast<char>(c));
                if (c >= 0xC2 && c <= 0xDF) { remaining = 1; minimum = 0x80; scalar = c & 0x1F; }
                else if (c >= 0xE0 && c <= 0xEF) { remaining = 2; minimum = 0x800; scalar = c & 0x0F; }
                else if (c >= 0xF0 && c <= 0xF4) { remaining = 3; minimum = 0x10000; scalar = c & 0x07; }
                else throw std::runtime_error("invalid UTF-8 leading byte");
            }
            else if (c >= 32 && c <= 126) screen.put(c, std::string(1, static_cast<char>(c)));
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
    void finish() const { if (state != State::ground || remaining) throw std::runtime_error("incomplete sequence at EOF"); }
};
} // namespace termforge
