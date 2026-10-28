#include "terminal.hpp"
#include "text_projection.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// WindTerm phase 15: the two source letters have no intervening DSR.
static void disabled_edge_overwrites(ParseMode mode) {
    Parser parser(25, 91, 2, mode);
    parser.feed("\x1b[?7l\x1b[10;91HCD");
    parser.finish();
    const auto& screen = parser.screen;
    check(screen.row == 9 && screen.column == 90,
          "disabled edge CD must end at (10,91)");
    check(project_line(screen.lines[9]).text == std::string(90, ' ') + "D" &&
          project_line(screen.lines[10]).text.empty() && screen.history.empty(),
          "disabled edge CD must retain only D without a wrapped successor");
}

int main() {
    int failures = 0;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
        const auto* name = mode == ParseMode::strict ? "strict" : "recovery";
        try {
            disabled_edge_overwrites(mode);
            std::cout << name << " disabled edge passed\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << name << " disabled edge failed: " << error.what() << '\n';
        }
    }
    std::cout << "DECAWM checks: " << (2 - failures) << "/2 passed\n";
    return failures ? 1 : 0;
}
