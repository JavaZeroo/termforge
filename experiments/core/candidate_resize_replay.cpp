// Reproduce a captured parser/feed ordering without a console or UI.
// Input and full screen snapshots can contain local paths; keep them private.
#include "candidate_terminal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::string read_bounded(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 128 * 1024)
        throw std::runtime_error("invalid resize replay input size");
    input.seekg(0);
    std::ostringstream out; out << input.rdbuf();
    if (!input) throw std::runtime_error("resize replay input read failed");
    return out.str();
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: candidate_resize_replay transport.bin plan.txt");
        const auto bytes = read_bounded(argv[1]);
        const auto commands = read_bounded(argv[2]);
        std::istringstream plan(commands);
        termforge::CandidateTerminal terminal;
        char kind; size_t a, b, offset = 0, step = 0, traced_cells = 0;
        while (plan >> kind >> a >> b) {
            if (++step > 1024) throw std::runtime_error("resize replay event limit exceeded");
            if (kind == 'R') {
                if (a < 1 || a > 200 || b < 1 || b > 500)
                    throw std::runtime_error("resize replay dimensions invalid");
                terminal.resize(static_cast<int>(a), static_cast<int>(b));
            } else if (kind == 'F') {
                if (a != offset || !b || b > bytes.size() - offset)
                    throw std::runtime_error("resize replay feed partition invalid");
                terminal.feed(std::string_view(bytes).substr(offset, b)); offset += b;
            } else throw std::runtime_error("resize replay event invalid");
            const auto cells = (terminal.screen.history.size() + terminal.screen.lines.size()) * terminal.screen.width();
            if (cells > 1000000 - traced_cells) throw std::runtime_error("resize replay snapshot limit exceeded");
            traced_cells += cells;
            std::cout << "{\"step\":" << step << ",\"offset\":" << offset << ",\"kind\":\"" << kind << "\",\"state\":";
            terminal.screen.json(std::cout); std::cout << "}\n";
        }
        if (!plan.eof() || offset != bytes.size()) throw std::runtime_error("resize replay plan incomplete");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
