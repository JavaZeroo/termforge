// Literal marker/DSR observations from four independent WindTerm 2.7.0
// resize-address cases. This exercises real Screen::resize, not view width only.
// Full grids, styles, native transport and unobserved actions are not asserted.
#include "terminal.hpp"
#include "history_view.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace termforge;

enum class Action { next_char, carriage_return, line_feed, explicit_cup };

struct Case {
    const char* name;
    Action action;
    std::string_view bytes;
    const char* after_reply;
};

struct Marker {
    const char* text;
    size_t source_row, source_column;
    size_t display_row, display_column;
};

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

// Observe the public document's actual soft continuations. The expected
// logical coordinates below are independent literals, not physical row IDs.
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

static std::vector<std::string> display_lines(const ViewSnapshot& snapshot) {
    std::vector<std::string> result;
    for (const auto& line : snapshot.lines) {
        std::string text;
        for (const auto& cell : line) text += cell.text;
        result.push_back(std::move(text));
    }
    return result;
}

static void marker_position(Checks& checks, const std::vector<std::string>& lines,
                            const char* label, bool present, size_t row, size_t column,
                            const char* coordinate_space) {
    size_t found = 0;
    bool expected_position = false;
    for (size_t index = 0; index < lines.size(); ++index) {
        for (size_t offset = lines[index].find(label); offset != std::string::npos;
             offset = lines[index].find(label, offset + 1)) {
            ++found;
            expected_position = index == row && offset == column;
        }
    }
    const std::string prefix = std::string(coordinate_space) + " marker " + label;
    checks.expect(present ? found == 1 && expected_position : found == 0,
                  prefix + (present ? " must occur once at (" + std::to_string(row) + "," +
                      std::to_string(column) + ")" : " must be absent"));
}

static std::vector<Marker> expected_markers(Action action, size_t phase) {
    // Source: resize_address_reference phase records and the strict exports.
    // Display literals use the observed 8x18 grid: rect origin x=147, y=0.
    // DSR and document coordinates remain separate from those rectangles.
    const bool narrow = phase == 1 || phase == 2;
    std::vector<Marker> expected = {
        {"0123456789", 1, 0, 1, 0},
        {"ANCHOR", 2, 0, 2, 0},
        {"v", 7, 90, narrow ? 8u : 7u, narrow ? 29u : 90u},
        {"w", 9, 90, narrow ? 11u : 9u, narrow ? 29u : 90u},
    };
    if (phase < 2 || action != Action::carriage_return)
        expected.push_back({"x", 9, 91, narrow ? 11u : 10u, narrow ? 30u : 0u});
    if (phase >= 2) {
        switch (action) {
        case Action::next_char:
        case Action::explicit_cup:
            expected.push_back({"Q", 9, 92, narrow ? 11u : 10u, narrow ? 31u : 1u});
            break;
        case Action::carriage_return:
            expected.push_back({"Q", 9, 91, narrow ? 11u : 10u, narrow ? 30u : 0u});
            break;
        case Action::line_feed:
            expected.push_back({"Q", 10, 1, narrow ? 12u : 11u, 1});
            break;
        }
    }
    return expected;
}

static void check_markers(Checks& checks, const std::vector<std::string>& lines,
                          const std::vector<Marker>& expected, bool display) {
    for (const char* label : {"0123456789", "ANCHOR", "v", "w", "x", "Q"}) {
        const auto found = std::find_if(expected.begin(), expected.end(), [&](const Marker& marker) {
            return std::string_view(marker.text) == label;
        });
        const bool present = found != expected.end();
        const auto row = present ? (display ? found->display_row : found->source_row) : 0;
        const auto column = present ? (display ? found->display_column : found->source_column) : 0;
        marker_position(checks, lines, label, present, row, column, display ? "display" : "source");
    }
}

static void observe_phase(Checks& checks, Parser& parser, HistoryViewport& viewport,
                          const Case& test, size_t phase, bool bytewise) {
    const auto previous_failures = checks.failures;
    const int columns = phase == 1 || phase == 2 ? 61 : 91;
    const auto expected = expected_markers(test.action, phase);
    checks.expect(parser.screen.width() == columns && parser.screen.height() == 20,
                  "actual protocol grid must match the observed phase dimensions");
    // Each original phase sends only its declared action followed by this DSR.
    // The next action is never used to infer the preceding cursor position.
    feed(parser, "\x1b[6n", bytewise);
    checks.expect(parser.take_replies() == std::vector<std::string>{phase < 2 ? "\x1b[11;2R" : test.after_reply},
                  "DSR reply differs from the literal phase response");
    try {
        check_markers(checks, source_lines(parser.screen), expected, false);
    } catch (const std::exception& error) {
        checks.expect(false, std::string("source observation failed: ") + error.what());
    }
    try {
        const auto page = viewport.project(parser.screen, static_cast<size_t>(columns), 20,
                                           {0, 0}, parser.screen.retained_size());
        check_markers(checks, display_lines(page.snapshot), expected, true);
    } catch (const std::exception& error) {
        checks.expect(false, std::string("display projection failed: ") + error.what());
    }
    ++checks.phases;
    if (checks.failures == previous_failures) ++checks.passed_phases;
}

static void replay(Checks& checks, const Case& test, ParseMode mode, bool bytewise, bool retained) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    // Original phase 0: 78 bytes, SHA256
    // c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l\x1b[8;91Huv"
        "\x1b[?7h\x1b[10;91Hwx";
    const auto prefix = std::string(test.name) + "/" + (mode == ParseMode::strict ? "strict/" : "recovery/") +
                        (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < 4; ++phase) {
        checks.context = prefix + "phase-" + std::to_string(phase);
        if (phase == 0) feed(parser, source, bytewise);
        else if (phase == 1) {
            if (retained) parser.screen.resize_columns_preserving(61);
            else parser.screen.resize(20, 61);
        }
        else if (phase == 2) feed(parser, test.bytes, bytewise);
        else {
            if (retained) parser.screen.resize_columns_preserving(91);
            else parser.screen.resize(20, 91);
        }
        // Keep all four observations even if an earlier phase failed. A failed
        // pre-action retention check must not hide action/restore diagnostics.
        observe_phase(checks, parser, viewport, test, phase, bytewise);
    }
    parser.finish();
}

int main(int argc, char** argv) {
    const bool retained = argc == 2 && std::string_view(argv[1]) == "--retained-columns";
    if (argc > 1 && !retained) {
        std::cerr << "usage: resize_address_test [--retained-columns]\n";
        return 2;
    }
    // Strict-export SHA256, in case order:
    // next-char: a4c4a2bca0247984d64f8b121c7828839294397ab5c1d8b3539690310eb93610
    // carriage-return: 4bd3e5fe4e9dccb22cbab05f367beb37eaece8760ffee069f1f96e4d2265532d
    // line-feed: 6104667d2cc60c79eec7d32d6dac567d969b427537a8bb48f332da5079a527c1
    // explicit-cup: 052ab73815322a3db97f7531da60b480d7b61efcfe53fdc0cfcec4c91397813e
    const Case cases[] = {
        {"next-char", Action::next_char, "Q", "\x1b[11;3R"},
        {"carriage-return", Action::carriage_return, "\rQ", "\x1b[11;2R"},
        {"line-feed", Action::line_feed, "\nQ", "\x1b[12;3R"},
        {"explicit-cup", Action::explicit_cup, "\x1b[11;2HQ", "\x1b[11;3R"},
    };
    Checks checks;
    for (const auto& test : cases)
        for (const auto mode : {ParseMode::strict, ParseMode::recovery})
            for (const bool bytewise : {false, true}) {
                try {
                    replay(checks, test, mode, bytewise, retained);
                } catch (const std::exception& error) {
                    checks.expect(false, std::string("replay failed: ") + error.what());
                }
            }
    std::cout << (retained ? "retained-column transaction: " : "default resize: ")
              << "resize address checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (4 original cases x strict/recovery x whole/bytewise; no full-grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
