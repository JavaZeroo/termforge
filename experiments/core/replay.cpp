#include "terminal.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::runtime_error("usage: replay input rows columns history chunk");
        termforge::Parser parser(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]));
        const size_t chunk = std::stoull(argv[5]);
        if (!chunk || chunk > 1048576) throw std::runtime_error("invalid chunk size");
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() > 64 * 1024 * 1024) throw std::runtime_error("invalid input file or size");
        input.seekg(0);
        const std::string data((std::istreambuf_iterator<char>(input)), {});
        const auto start = std::chrono::steady_clock::now();
        for (size_t offset = 0; offset < data.size(); offset += chunk)
            parser.feed(std::string_view(data).substr(offset, chunk));
        parser.finish();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"bytes\":" << data.size() << ",\"parse_ms\":" << ms << ",\"screen\":";
        parser.screen.json(std::cout); std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
