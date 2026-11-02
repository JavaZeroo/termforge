// Literal WindTerm 2.7.0 right-edge observations across real protocol resize.
// Case names identify the source experiment; internal pending-wrap bits are
// not an oracle. DSR, document positions and display positions stay separate.
#include "terminal.hpp"
#include "history_view.hpp"
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

struct Marker {
    const char* text;
    size_t source_row, source_column, source_position;
    size_t display_row, display_column;
};

struct Stage {
    const char* label;
    std::string_view bytes;
    int columns;
    const char* reply;
    std::vector<Marker> markers;
};

struct Case {
    const char* name;
    std::vector<Stage> stages;
};

struct Location {
    size_t row, column, position;
    bool operator==(const Location& other) const {
        return row == other.row && column == other.column && position == other.position;
    }
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

// Public document continuations determine the actual logical lines. Expected
// coordinates are independent literals, never derived from physical row IDs.
static std::vector<std::string> source_lines(const Screen& screen) {
    TextDocument document(screen);
    if (document.size() > 200) throw std::runtime_error("source exceeds its 200-fragment observation bound");
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

static std::string describe(const std::vector<Location>& positions, bool display) {
    std::string result = "{";
    for (size_t index = 0; index < positions.size(); ++index) {
        if (index) result += ", ";
        const auto& location = positions[index];
        result += "(" + std::to_string(location.row) + "," + std::to_string(location.column);
        if (!display) result += "; offset " + std::to_string(location.position);
        result += ")";
    }
    return result + "}";
}

static void check_markers(Checks& checks, const std::vector<std::string>& lines,
                          const std::vector<Marker>& expected, bool display) {
    for (const char* label : {"0123456789", "ANCHOR", "W", "X"}) {
        std::vector<Location> actual, wanted;
        size_t line_start = 0;
        for (size_t row = 0; row < lines.size(); ++row) {
            for (size_t column = lines[row].find(label); column != std::string::npos;
                 column = lines[row].find(label, column + 1)) {
                actual.push_back({row, column, display ? 0 : line_start + column});
            }
            // ASCII source offsets include one separator per hard logical line;
            // display offsets have no document meaning and remain zero.
            line_start += lines[row].size() + 1;
        }
        for (const auto& marker : expected) {
            if (std::string_view(marker.text) != label) continue;
            if (display) wanted.push_back({marker.display_row, marker.display_column, 0});
            else wanted.push_back({marker.source_row, marker.source_column, marker.source_position});
        }
        checks.expect(actual == wanted, std::string(display ? "display" : "source") + " marker " + label +
            " expected " + describe(wanted, display) + ", observed " + describe(actual, display));
    }
}

static std::vector<Marker> with_controls(std::initializer_list<Marker> markers) {
    std::vector<Marker> result = {
        {"0123456789", 1, 0, 1, 1, 0},
        {"ANCHOR", 2, 0, 12, 2, 0},
    };
    result.insert(result.end(), markers);
    return result;
}

static Case pending_on_shrink() {
    // Oracle: research/ui/results/2026-11-02-resize-frame-reference/pending-on-shrink.json, SHA256
    // 0e37a46dccec9c2d0620487c63486d086c205874c62835ce506ba333ebd70bb9.
    // Initial source: 57 bytes, SHA256
    // a052dbd92f9c02f5ca260e577ac30424caa775da86dd37532dc2f611f89e2819.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;91HW";
    // Source coordinates/offsets are zero-based. Display coordinates are
    // literal observed rects on the 8x18 grid with origin (147,0).
    return {"pending-on-shrink", {
        {"wrap-on-old-right-edge", source, 91, "\x1b[8;91R",
            with_controls({{"W", 7, 90, 113, 7, 90}})},
        {"shrunk-before-next-character", "", 61, "\x1b[8;61R",
            with_controls({{"W", 7, 90, 113, 8, 29}})},
        {"shrunk-after-X", "X", 61, "\x1b[8;61R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
        {"restored-after-X", "", 91, "\x1b[8;61R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
    }};
}

static Case wrap_off_shrink() {
    // Independent oracle: resize-frame-reference/wrap-off-shrink.json, SHA256
    // 19b31b4ebe6603f699c203ed7c9b5cb91fa8ff342e5ddecc7951fb5f44a5169a.
    // Initial source: 62 bytes, SHA256
    // 714add82da77eea02bf051e1b69fae25dc450ffbc0d44a215774a07878545ece.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l\x1b[8;91HW";
    return {"wrap-off-shrink", {
        {"wrap-off-old-right-edge", source, 91, "\x1b[8;91R",
            with_controls({{"W", 7, 90, 113, 7, 90}})},
        {"shrunk-before-next-character", "", 61, "\x1b[8;61R",
            with_controls({{"W", 7, 90, 113, 8, 29}})},
        {"shrunk-after-X", "X", 61, "\x1b[8;61R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
        {"restored-after-X", "", 91, "\x1b[8;61R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
    }};
}

static Case pending_on_grow() {
    // Independent oracle: resize-frame-reference/pending-on-grow.json, SHA256
    // ee5230fcc35cbab1bf47d09eae675733eebcd79f054fa514974f0abc6df7613c.
    // Initial source: 49 bytes, SHA256
    // 9e108759e947647cd215586e8584b0de4201203a983c0a2ebacd4c7e504f5c7d.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR";
    return {"pending-on-grow", {
        {"wrap-on-base", source, 91, "\x1b[3;7R", with_controls({})},
        {"shrunk-before-edge-setup", "", 61, "\x1b[3;7R", with_controls({})},
        {"wrap-on-narrow-right-edge", "\x1b[8;61HW", 61, "\x1b[8;61R",
            with_controls({{"W", 7, 60, 83, 7, 60}})},
        {"grown-before-next-character", "", 91, "\x1b[8;61R",
            with_controls({{"W", 7, 60, 83, 7, 60}})},
        {"grown-after-X", "X", 91, "\x1b[8;62R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
    }};
}

static Case wrap_off_grow() {
    // Independent oracle: resize-frame-reference/wrap-off-grow.json, SHA256
    // c67554247cd10eb20c9f28c8fb117ac48f395782027f7215947189f80c82ff0f.
    // Initial source: 54 bytes, SHA256
    // c477da5cadc30cb119275b13f74a2c245874e5db22b80646df99d600ebdf80ac.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l";
    return {"wrap-off-grow", {
        {"wrap-off-base", source, 91, "\x1b[3;7R", with_controls({})},
        {"shrunk-before-edge-setup", "", 61, "\x1b[3;7R", with_controls({})},
        {"wrap-off-narrow-right-edge", "\x1b[8;61HW", 61, "\x1b[8;61R",
            with_controls({{"W", 7, 60, 83, 7, 60}})},
        {"grown-before-next-character", "", 91, "\x1b[8;61R",
            with_controls({{"W", 7, 60, 83, 7, 60}})},
        {"grown-after-X", "X", 91, "\x1b[8;62R",
            with_controls({{"X", 7, 60, 83, 7, 60}})},
    }};
}

static void replay(Checks& checks, const Case& test, ParseMode mode, bool bytewise) {
    if (test.stages.empty()) throw std::invalid_argument("empty original case");
    Parser parser(20, test.stages.front().columns, 0, mode);
    HistoryViewport viewport;
    const auto prefix = std::string(test.name) + "/" +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < test.stages.size(); ++phase) {
        const auto& stage = test.stages[phase];
        checks.context = prefix + stage.label;
        const auto previous_failures = checks.failures;
        if (phase && stage.columns != test.stages[phase - 1].columns)
            parser.screen.resize_columns_preserving(stage.columns);
        feed(parser, stage.bytes, bytewise);
        checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                      "actual protocol grid differs from the literal phase dimensions");
        // Query before the next stage's action. No CUP or cursor fix-up is
        // inserted to make a resize observation agree with the expected reply.
        feed(parser, "\x1b[6n", bytewise);
        checks.expect(parser.take_replies() == std::vector<std::string>{stage.reply},
                      "DSR reply differs from the literal phase response");
        try {
            check_markers(checks, source_lines(parser.screen), stage.markers, false);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("source observation failed: ") + error.what());
        }
        try {
            const auto page = viewport.project(parser.screen, static_cast<size_t>(stage.columns), 20,
                                               {0, 0}, parser.screen.retained_size());
            check_markers(checks, display_lines(page.snapshot), stage.markers, true);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("display projection failed: ") + error.what());
        }
        ++checks.phases;
        if (checks.failures == previous_failures) ++checks.passed_phases;
    }
    parser.finish();
}

int main() {
    const std::vector<Case> cases = {
        pending_on_shrink(), wrap_off_shrink(), pending_on_grow(), wrap_off_grow(),
    };
    Checks checks;
    for (const auto& test : cases)
        for (const auto mode : {ParseMode::strict, ParseMode::recovery})
            for (const bool bytewise : {false, true}) {
                try {
                    replay(checks, test, mode, bytewise);
                } catch (const std::exception& error) {
                    checks.expect(false, std::string("replay failed: ") + error.what());
                }
            }
    std::cout << "retained-column resize pending checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (" << cases.size() << " original cases x strict/recovery x whole/bytewise; no internal-mode/grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
