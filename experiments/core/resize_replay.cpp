// Replay one captured byte stream across an explicit resize timeline.
//
// The plain `replay` tool feeds a whole capture at one fixed geometry, so a
// capture that changed size mid-session (DPI switch, zoom, restored window)
// cannot be replayed faithfully. This tool reads the same bytes but applies a
// caller-supplied sequence of geometries, reporting the viewport and buffer
// state after each step so a repaint hypothesis can be checked against real
// bytes (#194).
#include "terminal.hpp"
#include "history_view.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace termforge;

// One step of the timeline: resize to rows x columns, then consume `bytes`
// bytes of the capture.
struct Step {
    int rows = 0;
    int columns = 0;
    size_t bytes = 0;
    std::string label;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<Step> read_steps(const char* path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "cannot open steps file");
    std::vector<Step> steps;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        Step step;
        require(static_cast<bool>(fields >> step.rows >> step.columns >> step.bytes), "malformed step line");
        require(step.rows > 0 && step.columns > 0, "step geometry must be positive");
        std::getline(fields, step.label);
        if (!step.label.empty() && step.label.front() == ' ') step.label.erase(0, 1);
        steps.push_back(step);
    }
    require(!steps.empty(), "steps file has no steps");
    return steps;
}

void quoted(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') out << '\\' << ch;
        else if (ch == '\n') out << "\\n";
        else out << ch;
    }
    out << '"';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("usage: resize_replay input steps history");
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        require(static_cast<bool>(input), "cannot open input file");
        require(input.tellg() <= 64 * 1024 * 1024, "input file too large");
        input.seekg(0);
        const std::string data((std::istreambuf_iterator<char>(input)), {});
        const std::vector<Step> steps = read_steps(argv[2]);
        const size_t history = std::stoull(argv[3]);

        const Step& first = steps.front();
        Parser parser(first.rows, first.columns, history);
        HistoryViewport viewport;
        const auto start = std::chrono::steady_clock::now();
        size_t consumed = 0;
        for (size_t index = 0; index < steps.size(); ++index) {
            const Step& step = steps[index];
            if (index > 0) parser.screen.resize(step.rows, step.columns);
            const size_t take = (std::min)(step.bytes, data.size() - consumed);
            parser.feed(std::string_view(data).substr(consumed, take));
            parser.finish();
            consumed += take;
            const ViewportState state = viewport.reconcile(parser.screen);
            std::cout << "{\"step\":" << index + 1 << ",\"label\":";
            quoted(std::cout, step.label);
            std::cout << ",\"rows\":" << step.rows << ",\"columns\":" << step.columns
                      << ",\"consumed\":" << take << ",\"total_consumed\":" << consumed
                      << ",\"history\":" << parser.screen.history_lines().size()
                      << ",\"retained\":" << parser.screen.retained_size()
                      << ",\"top\":" << state.top << ",\"maximum_top\":" << state.maximum_top
                      << ",\"following\":" << (state.following ? "true" : "false")
                      << ",\"cursor_row\":" << parser.screen.row
                      << ",\"cursor_column\":" << parser.screen.column << "}\n";
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"bytes\":" << data.size() << ",\"consumed\":" << consumed << ",\"steps\":"
                  << steps.size() << ",\"parse_ms\":" << ms << ",\"screen\":";
        parser.screen.json(std::cout);
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
