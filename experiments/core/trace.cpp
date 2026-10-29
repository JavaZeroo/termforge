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
        const std::string option = argc == 7 ? argv[6] : "";
        const bool recovery = option == "--recovery";
        const bool resize = option.compare(0, 9, "--resize=") == 0;
        if (argc < 6 || argc > 7 || (argc == 7 && !recovery && !resize))
            throw std::runtime_error("usage: terminal_trace input rows columns history chunks.txt [--recovery|--resize=RxC]");
        int resize_rows = 0, resize_columns = 0;
        if (resize) {
            const auto split = option.find('x', 9);
            if (split == std::string::npos) throw std::runtime_error("invalid resize option");
            resize_rows = std::stoi(option.substr(9, split - 9));
            resize_columns = std::stoi(option.substr(split + 1));
        }
        termforge::Parser parser(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]),
                                recovery ? termforge::ParseMode::recovery : termforge::ParseMode::strict);
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() > 1024 * 1024) throw std::runtime_error("invalid trace input file or size");
        input.seekg(0);
        const std::string data((std::istreambuf_iterator<char>(input)), {});
        std::ifstream plan(argv[5]);
        if (!plan) throw std::runtime_error("invalid chunk plan");
        size_t traced_cells = 0;
        const auto snapshot = [&]() {
            const auto& screen = parser.screen;
            const size_t count = (screen.visible_lines().size() + screen.history_lines().size()) * screen.width();
            if (count > 1000000 - traced_cells) throw std::runtime_error("trace cell budget exceeded");
            traced_cells += count;
            std::cout << "{\"offset\":" << offset << ",\"screen\":{\"row\":" << screen.row
                << ",\"column\":" << screen.column << ",\"wrap_pending\":"
                << (screen.wrap_pending ? "true" : "false") << ",\"cells\":";
            cells(std::cout, screen.visible_lines());
            std::cout << ",\"history\":"; cells(std::cout, screen.history_lines());
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
        if (resize) {
            parser.screen.resize(resize_rows, resize_columns);
            snapshot();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "trace failed after checkpoint " << offset << ": " << e.what() << '\n';
        return 2;
    }
}
