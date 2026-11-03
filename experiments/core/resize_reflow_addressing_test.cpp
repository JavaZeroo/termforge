// One independent WindTerm 2.7.0 observation: narrowing a 123-character line
// before the target rows shifts later display rows by one, while CUP, DSR and
// caret keep addressing the retained (pre-resize) source rows. The literal
// expectations come from research/ui/results/2026-11-03-reflow-addressing-reference;
// this exercises the explicit retained-width transaction, not default Screen::resize.
#include "terminal.hpp"
#include "history_view.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

struct Checks {
    std::string context;
    size_t failures = 0, assertions = 0, phases = 0, passed_phases = 0;

    void expect(bool condition, const std::string& message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << ": " << message << '\n';
    }
};

static void feed(Parser& parser, std::string_view bytes, bool bytewise) {
    if (bytewise) {
        for (const char byte : bytes) parser.feed(std::string_view(&byte, 1));
    } else {
        parser.feed(bytes);
    }
}

// 222-byte fixed body: a 123-character soft-wrapped line on source rows 4-5,
// then single-row markers on source rows 6-10, cursor placed on source row 11.
static std::string initial_body() {
    std::string body = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J";
    body += "\x1b[2;1H0123456789";
    body += "\x1b[3;1HANCHOR";
    body += "\x1b[5;1H" + std::string(122, 'r') + "v";
    body += "\x1b[7;1H5\x1b[8;1H6\x1b[9;1H7\x1b[10;1H8\x1b[11;1H9\x1b[12;1H";
    return body;
}

static std::string reframe(size_t count) {
    return std::string(count, 'r');
}

// Public document fragments joined only across real soft continuations.
static std::vector<std::string> source_lines(const Screen& screen) {
    TextDocument document(screen);
    if (document.size() > 200) throw std::runtime_error("source observation exceeds its 200-fragment bound");
    std::vector<std::string> result;
    std::string logical;
    for (size_t index = 0; index < document.size(); ++index) {
        const auto line = document.line(index);
        logical += line.projection.text;
        if (!line.joins_next) {
            result.push_back(std::move(logical));
            logical.clear();
        }
    }
    if (!logical.empty()) result.push_back(std::move(logical));
    return result;
}

static std::string trimmed(std::string value) {
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

static std::vector<std::string> display_lines(const ViewSnapshot& snapshot) {
    std::vector<std::string> result;
    for (const auto& line : snapshot.lines) {
        std::string text;
        for (const auto& cell : line) text += cell.text;
        result.push_back(trimmed(std::move(text)));
    }
    return result;
}

using Rows = std::vector<std::pair<size_t, std::string>>;

// Every non-empty line must be one of the literal expected rows and every
// expected row must be observed once; trailing padding is not compared.
static void check_rows(Checks& checks, const std::vector<std::string>& lines, const Rows& expected, const char* space) {
    std::vector<bool> seen(expected.size(), false);
    for (size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].empty()) continue;
        bool matched = false;
        for (size_t position = 0; position < expected.size(); ++position)
            if (expected[position].first == index) matched = expected[position].second == lines[index];
        checks.expect(matched, std::string(space) + " line " + std::to_string(index) + " content differs");
        for (size_t position = 0; position < expected.size(); ++position)
            if (expected[position].first == index) seen[position] = true;
    }
    for (size_t position = 0; position < expected.size(); ++position)
        checks.expect(seen[position], std::string(space) + " expected line " +
            std::to_string(expected[position].first) + " is missing");
}

// Q and v are unique marker labels; assert their exact line/column occurrence.
static void check_marker(Checks& checks, const std::vector<std::string>& lines, const std::string& label,
                         size_t row, size_t column, const char* space) {
    size_t found = 0;
    bool placed = false;
    for (size_t index = 0; index < lines.size(); ++index) {
        for (size_t offset = lines[index].find(label); offset != std::string::npos;
             offset = lines[index].find(label, offset + 1)) {
            ++found;
            placed = index == row && offset == column;
        }
    }
    checks.expect(found == 1 && placed, std::string(space) + " marker " + label + " position differs");
}

static void observe_phase(Checks& checks, Parser& parser, HistoryViewport& viewport, size_t phase, bool bytewise) {
    const auto previous_failures = checks.failures;
    const size_t columns = phase == 1 || phase == 2 ? 61 : 91;
    checks.expect(parser.screen.width() == columns && parser.screen.height() == 20,
                  "actual protocol grid must match the observed phase dimensions");
    // Each original phase sends only its declared action followed by this DSR.
    const char* replies[] = {"\x1b[12;1R", "\x1b[12;1R", "\x1b[8;3R", "\x1b[8;3R"};
    feed(parser, "\x1b[6n", bytewise);
    checks.expect(parser.take_replies() == std::vector<std::string>{replies[phase]},
                  "DSR reply differs from the literal observed phase response");
    Rows source;
    if (phase < 2) {
        source = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(122) + "v"},
                  {5, "5"}, {6, "6"}, {7, "7"}, {8, "8"}, {9, "9"}};
    } else {
        source = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(122) + "v"},
                  {5, "5"}, {6, "6Q"}, {7, "7"}, {8, "8"}, {9, "9"}};
    }
    try {
        check_rows(checks, source_lines(parser.screen), source, "source");
    } catch (const std::exception& error) {
        checks.expect(false, std::string("source observation failed: ") + error.what());
    }
    Rows display;
    if (phase == 0) {
        display = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(91)}, {5, reframe(31) + "v"},
                   {6, "5"}, {7, "6"}, {8, "7"}, {9, "8"}, {10, "9"}};
    } else if (phase == 1) {
        display = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(61)}, {5, reframe(61)}, {6, "v"},
                   {7, "5"}, {8, "6"}, {9, "7"}, {10, "8"}, {11, "9"}};
    } else if (phase == 2) {
        display = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(61)}, {5, reframe(61)}, {6, "v"},
                   {7, "5"}, {8, "6Q"}, {9, "7"}, {10, "8"}, {11, "9"}};
    } else {
        display = {{1, "0123456789"}, {2, "ANCHOR"}, {4, reframe(91)}, {5, reframe(31) + "v"},
                   {6, "5"}, {7, "6Q"}, {8, "7"}, {9, "8"}, {10, "9"}};
    }
    try {
        const auto page = viewport.project(parser.screen, columns, 20, {0, 0}, parser.screen.retained_size());
        check_rows(checks, display_lines(page.snapshot), display, "display");
        // Independent visible-rectangle evidence: v moves by one display row when
        // the earlier line reflows; Q stays on its document line and re-projects.
        if (phase == 0) {
            check_marker(checks, display_lines(page.snapshot), "v", 5, 31, "display");
        } else if (phase == 1) {
            check_marker(checks, display_lines(page.snapshot), "v", 6, 0, "display");
        } else if (phase == 2) {
            check_marker(checks, display_lines(page.snapshot), "v", 6, 0, "display");
            check_marker(checks, display_lines(page.snapshot), "Q", 8, 1, "display");
        } else {
            check_marker(checks, display_lines(page.snapshot), "v", 5, 31, "display");
            check_marker(checks, display_lines(page.snapshot), "Q", 7, 1, "display");
        }
    } catch (const std::exception& error) {
        checks.expect(false, std::string("display projection failed: ") + error.what());
    }
    ++checks.phases;
    if (checks.failures == previous_failures) ++checks.passed_phases;
}

static void replay(Checks& checks, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    const auto body = initial_body();
    checks.expect(body.size() == 222, "fixed initial body differs from the frozen 222 bytes");
    const auto prefix = std::string(mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < 4; ++phase) {
        checks.context = prefix + "phase-" + std::to_string(phase);
        if (phase == 0) feed(parser, body, bytewise);
        else if (phase == 1) parser.screen.resize_columns_preserving(61);
        else if (phase == 2) feed(parser, "\x1b[8;2HQ", bytewise);
        else parser.screen.resize_columns_preserving(91);
        // Keep all four observations even if an earlier phase failed.
        observe_phase(checks, parser, viewport, phase, bytewise);
    }
    parser.finish();
}

int main() {
    Checks checks;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool bytewise : {false, true}) {
            try {
                replay(checks, mode, bytewise);
            } catch (const std::exception& error) {
                checks.expect(false, std::string("replay failed: ") + error.what());
            }
        }
    std::cout << "retained reflow addressing: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (4 observed phases x strict/recovery x whole/bytewise; no full-grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
