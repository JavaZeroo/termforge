// Literal display-caret mapping for the frozen post-reflow sample.
//
// The protocol cursor stays a retained source position (see
// research/ui/results/2026-11-03-reflow-addressing-reference). The display page
// reflows the same atoms at another width, and the caret cell must follow that
// page without moving the protocol cursor or inventing source content.
#include "terminal.hpp"
#include "history_view.hpp"
#include "display_cursor.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace termforge;

struct Checks {
    std::string context;
    size_t failures = 0, assertions = 0;

    void expect(bool condition, const std::string& message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << ": " << message << '\n';
    }
};

static std::string initial_body() {
    std::string body = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J";
    body += "\x1b[2;1H0123456789";
    body += "\x1b[3;1HANCHOR";
    body += "\x1b[5;1H" + std::string(122, 'r') + "v";
    body += "\x1b[7;1H5\x1b[8;1H6\x1b[9;1H7\x1b[10;1H8\x1b[11;1H9\x1b[12;1H";
    return body;
}

static ProjectionPage project_page(Screen& screen, HistoryViewport& viewport, size_t columns, size_t rows) {
    return viewport.project(screen, columns, rows, {0, 0}, screen.retained_size());
}

int main() {
    Checks checks;
    Parser parser(20, 91, 0, ParseMode::strict);
    HistoryViewport viewport;
    parser.feed(initial_body());
    const auto cursor_at = [&](size_t columns, size_t row, size_t column, const char* note) {
        const auto page = project_page(parser.screen, viewport, columns, 20);
        const auto mapped = display_cursor(parser.screen, page.snapshot);
        const std::string actual = mapped
            ? std::to_string(mapped->row) + "," + std::to_string(mapped->column) : "none";
        checks.expect(mapped.has_value() && mapped->row == row && mapped->column == column,
                      std::string("display caret differs at ") + note + " expected " + std::to_string(row) + "," +
                      std::to_string(column) + " actual " + actual);
    };

    checks.context = "wide";
    cursor_at(91, 11, 0, "initial bottom row");
    cursor_at(91, 11, 0, "repeated projection is stable");

    checks.context = "narrow";
    parser.screen.resize_columns_preserving(61);
    cursor_at(61, 12, 0, "earlier reflow shifts the caret row");

    checks.context = "narrow-action";
    parser.feed("\x1b[8;2HQ");
    cursor_at(61, 8, 2, "CUP write keeps source row while display reflows");

    checks.context = "narrow-continuation-fragment";
    parser.feed("\x1b[6;2H");
    cursor_at(61, 5, 31, "cursor on a soft-continuation fragment");

    checks.context = "narrow-padding";
    parser.feed("\x1b[10;6H");
    cursor_at(61, 10, 5, "caret in trailing padding keeps the column offset");

    checks.context = "narrow-empty-line";
    parser.feed("\x1b[12;5H");
    cursor_at(61, 12, 4, "caret on an empty hard row keeps the column offset");

    checks.context = "restore";
    parser.screen.resize_columns_preserving(91);
    cursor_at(91, 11, 4, "restored width re-projects the same source row");

    checks.context = "continued-output";
    parser.feed("Z");
    cursor_at(91, 11, 5, "continued output advances the display caret");

    // A bounded page that ends before the cursor's source row cannot show it.
    checks.context = "truncated-page";
    const auto short_page = project_page(parser.screen, viewport, 91, 2);
    checks.expect(!display_cursor(parser.screen, short_page.snapshot).has_value(),
                  "a page that ends above the source cursor must not invent a caret");

    // Wide atom: the caret after a two-cell character is the cell after it.
    {
        Parser wide(4, 20, 0, ParseMode::strict);
        HistoryViewport wide_viewport;
        wide.feed("\xe4\xb8\xad");
        wide.feed("\x1b[1;3H");
        const auto page = project_page(wide.screen, wide_viewport, 20, 4);
        const auto mapped = display_cursor(wide.screen, page.snapshot);
        checks.context = "wide-atom";
        checks.expect(mapped.has_value() && mapped->row == 0 && mapped->column == 2,
                      "caret after a wide atom differs");
    }

    std::cout << "display cursor: " << checks.failures << " failed assertions of " << checks.assertions << '\n';
    return checks.failures ? 1 : 0;
}

