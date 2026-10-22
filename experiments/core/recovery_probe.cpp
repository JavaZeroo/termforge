// CLI-only protocol recovery probe; no transport or terminal replies.
#include "terminal.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

static size_t number(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned decimal integer");
    size_t used = 0;
    const auto n = std::stoull(text, &used);
    if (used != text.size() || n > (std::numeric_limits<size_t>::max)())
        throw std::invalid_argument("integer out of range");
    return static_cast<size_t>(n);
}

int main(int argc, char** argv) {
    try {
        if (argc != 7 && argc != 8)
            throw std::invalid_argument("usage: recovery_probe input rows cols history chunks strict|recovery [finish_offset]");
        const auto rows = number(argv[2]), cols = number(argv[3]);
        if (rows > 200 || cols > 500) throw std::invalid_argument("invalid screen dimensions");
        const std::string selected(argv[6]);
        if (selected != "strict" && selected != "recovery") throw std::invalid_argument("invalid mode");
        termforge::Parser parser(static_cast<int>(rows), static_cast<int>(cols), number(argv[4]),
            selected == "strict" ? termforge::ParseMode::strict : termforge::ParseMode::recovery);
        std::vector<size_t> chunks;
        std::istringstream parts(argv[5]);
        std::string part;
        while (std::getline(parts, part, ',')) {
            const auto n = number(part);
            if (!n || n > 1048576 || chunks.size() == 1024) throw std::invalid_argument("invalid chunk pattern");
            chunks.push_back(n);
        }
        if (chunks.empty() || std::string(argv[5]).back() == ',') throw std::invalid_argument("empty chunk pattern");
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0 || input.tellg() > 64 * 1024 * 1024)
            throw std::invalid_argument("invalid input file or size");
        input.seekg(0);
        const std::string data((std::istreambuf_iterator<char>(input)), {});
        if (input.bad()) throw std::runtime_error("input read failed");
        const auto barrier = argc == 8 ? number(argv[7]) : data.size();
        if (barrier > data.size()) throw std::invalid_argument("finish offset out of range");
        size_t offset = 0, index = 0, max_buffered = 0;
        for (const auto end : {barrier, data.size()}) {
            while (offset < end) {
                const auto count = (std::min)(chunks[index++ % chunks.size()], end - offset);
                parser.feed(std::string_view(data).substr(offset, count)); offset += count;
                max_buffered = (std::max)(max_buffered, parser.buffered_input_bytes());
            }
            parser.finish(); parser.finish(); // Idempotence is part of this probe.
        }
        const auto& d = parser.diagnostics();
        std::cout << "{\"bytes\":" << data.size() << ",\"screen\":";
        parser.screen.json(std::cout);
        std::cout << ",\"diagnostics\":{\"ignored_sequences\":" << d.ignored_sequences
            << ",\"ignored_controls\":" << d.ignored_controls
            << ",\"malformed_utf8\":" << d.malformed_utf8
            << ",\"replaced_scalars\":" << d.replaced_scalars
            << ",\"cancelled_sequences\":" << d.cancelled_sequences
            << ",\"truncated_sequences\":" << d.truncated_sequences
            << ",\"oversized_sequences\":" << d.oversized_sequences
            << ",\"resynchronized_sequences\":" << d.resynchronized_sequences
            << ",\"max_sequence_bytes\":" << d.max_sequence_bytes
            << ",\"max_parameters\":" << d.max_parameters
            << ",\"max_utf8_bytes\":" << d.max_utf8_bytes
            << "},\"max_buffered_input_bytes_after_feed\":" << max_buffered
            << ",\"buffered_input_bytes_at_end\":" << parser.buffered_input_bytes() << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
