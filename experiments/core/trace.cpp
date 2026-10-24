#include "terminal.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

template<class Rows>
void cells(std::ostream& out, const Rows& rows) {
    out << '[';
    for (size_t r = 0; r < rows.size(); ++r) {
        if (r) out << ',';
        out << '[';
        for (size_t c = 0; c < rows[r].size(); ++c) {
            if (c) out << ',';
            const auto& cell = rows[r][c];
            out << "{\"text\":"; termforge::Screen::quoted(out, cell.text);
            out << ",\"width\":" << cell.width << ",\"style\":";
            cell.style.json(out); out << '}';
        }
        out << ']';
    }
    out << ']';
}

// State trace only: timings here would include serialization and are not benchmarks.
int main(int argc, char** argv) {
    size_t offset = 0;
    try {
        if (argc < 6 || argc > 7 || (argc == 7 && std::string_view(argv[6]) != "--recovery"))
            throw std::runtime_error("usage: terminal_trace input rows columns history chunks.txt [--recovery]");
        termforge::Parser parser(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]),
                                argc == 7 ? termforge::ParseMode::recovery : termforge::ParseMode::strict);
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() > 1024 * 1024) throw std::runtime_error("invalid trace input file or size");
        input.seekg(0);
        const std::string data((std::istreambuf_iterator<char>(input)), {});
        std::ifstream plan(argv[5]);
        if (!plan) throw std::runtime_error("invalid chunk plan");
        size_t traced_cells = 0;
        const auto snapshot = [&]() {
            const auto& screen = parser.screen;
            const size_t count = (screen.lines.size() + screen.history.size()) * screen.width();
            if (count > 1000000 - traced_cells) throw std::runtime_error("trace cell budget exceeded");
            traced_cells += count;
            std::cout << "{\"offset\":" << offset << ",\"screen\":{\"row\":" << screen.row
                << ",\"column\":" << screen.column << ",\"wrap_pending\":"
                << (screen.wrap_pending ? "true" : "false") << ",\"cells\":";
            cells(std::cout, screen.lines);
            std::cout << ",\"history\":"; cells(std::cout, screen.history);
            std::cout << ",\"current_style\":"; screen.style.json(std::cout);
            std::cout << "}}\n";
        };
        snapshot();
        size_t size = 0;
        while (plan >> size) {
            if (!size || size > data.size() - offset) throw std::runtime_error("chunk plan exceeds input or contains zero");
            parser.feed(std::string_view(data).substr(offset, size));
            offset += size;
            snapshot();
        }
        if (!plan.eof() || offset != data.size()) throw std::runtime_error("chunk plan does not cover input");
        parser.finish();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "trace failed after checkpoint " << offset << ": " << e.what() << '\n';
        return 2;
    }
}
