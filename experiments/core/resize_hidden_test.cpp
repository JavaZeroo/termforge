// Literal hidden-primary observations, with protocol, source and display
// coordinates kept separate. The original runtime audit failed; the unchanged
// recording passed a separate, narrowly bounded offline supplemental audit:
// research/ui/results/2026-11-02-resize-frame-reference/hidden-primary-supplement.json
// SHA256 1fde75cb4f841177ab868377b8621624afa0383601b994dd511c0f138d2a6974.
// These assertions do not classify private origin or pending-wrap bits.
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
    bool compare_source;
    std::vector<std::string> body;
    std::vector<Marker> markers;
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

static void omit_empty_tail(std::vector<std::string>& lines) {
    // The original Document allocates only through its observed body, whereas
    // Screen owns a complete viewport. Exclude only terminal empty strings:
    // neither space-only extents nor any interior empty line may be trimmed.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

static std::vector<std::string> source_lines(const Screen& screen) {
    TextDocument document(screen);
    if (document.size() > 200) throw std::runtime_error("source exceeds the 200-fragment observation bound");
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
    omit_empty_tail(result);
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
    for (const char* label : {"0123456789", "ANCHOR", "MP", "AP", "v", "w", "x",
                              "PS", "BS", "PI", "BI", "PO", "BO"}) {
        std::vector<Location> actual, wanted;
        size_t line_start = 0;
        for (size_t row = 0; row < lines.size(); ++row) {
            for (size_t column = lines[row].find(label); column != std::string::npos;
                 column = lines[row].find(label, column + 1)) {
                actual.push_back({row, column, display ? 0 : line_start + column});
            }
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

static void check_body(Checks& checks, const std::vector<std::string>& actual,
                       const std::vector<std::string>& expected) {
    checks.expect(actual.size() == expected.size(), "source body line count expected " +
        std::to_string(expected.size()) + ", observed " + std::to_string(actual.size()));
    for (size_t row = 0; row < expected.size(); ++row) {
        checks.expect(row < actual.size() && actual[row] == expected[row],
            "source body row " + std::to_string(row) + " differs: expected literal extent " +
            std::to_string(expected[row].size()) + ", observed " +
            (row < actual.size() ? std::to_string(actual[row].size()) : "missing"));
    }
}

static std::vector<Marker> primary_markers(size_t offset,
                                         std::initializer_list<Marker> markers) {
    std::vector<Marker> result = {
        {"0123456789", 1, 0, 1 + offset, 1, 0},
        {"ANCHOR", 2, 0, 12 + offset, 2, 0},
        {"MP", 2, 9, 21 + offset, 2, 9},
    };
    result.insert(result.end(), markers);
    return result;
}

static std::vector<std::string> returned_body() {
    // Literal phase-3 source, not a projection computed from candidate cells.
    // Its full original array adds one final empty string; canonical JSON SHA:
    // 031ebdc3f836dd626e360af9ffc15715ce865669b4635fcfdc46a7379092c9a1.
    return {"", "0123456789", "ANCHOR   MP", "", "", "", "",
            std::string(61, ' '), "", std::string(61, ' ') + "x"};
}

static std::vector<std::string> controlled_body(const char* upper, const char* lower) {
    // Phases 4..7 have the same literal extents with independently named
    // control markers. This does not infer the private origin setting.
    return {upper, "0123456789", "ANCHOR   MP", "", "", "", "",
            std::string(61, ' '), "", std::string(61, ' ') + "x",
            "", "", "", "", "", "", "", "", lower};
}

static std::vector<Stage> hidden_primary() {
    // 98-byte initial input, SHA256:
    // 7b74dd7b4c304ca21a70e78a6558efeabeffdd24e56ad32ea2e8a3ed58910441.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l\x1b[8;91Huv"
        "\x1b[?7h\x1b[10;91Hwx\x1b[2;5r\x1b[?6h\x1b[2;10HMP";
    // All source row/column/offset literals are zero-based. Display locations
    // are measured marker rectangles: origin (147,0), cell size 8x18. They are
    // independent of DSR and do not assert an unobserved complete active grid.
    return {
        {"primary-origin-on-body", source, 91, "\x1b[2;12R", true,
            {"", "0123456789", "ANCHOR   MP", "", "", "", "",
             std::string(90, ' ') + "v", "", std::string(90, ' ') + "wx"},
            primary_markers(0, {{"v", 7, 90, 118, 7, 90}, {"w", 9, 90, 211, 9, 90},
                                {"x", 9, 91, 212, 10, 0}})},
        // Alternate phases compare DSR and visible marker rectangles only.
        // The original document also exposes hidden primary text at negative
        // display y, with a separate allocation/index convention. No +1 source
        // offset or other frame-index correction is introduced in this test.
        {"alternate-origin-off", "\x1b[?1049h\x1b[3;7r\x1b[?6l\x1b[2;10HAP",
            91, "\x1b[2;12R", false, {}, {{"AP", 0, 0, 0, 1, 9}}},
        {"alternate-shrunk-primary-hidden", "", 61, "\x1b[2;12R", false, {},
            {{"AP", 0, 0, 0, 1, 9}}},
        // The return phase contains only DECRST 1049; no CUP or replacement
        // cursor is inserted before its separately observed 3;12 DSR reply.
        {"primary-return-unmodified", "\x1b[?1049l", 61, "\x1b[3;12R", true,
            returned_body(), primary_markers(0, {{"x", 9, 61, 152, 10, 0}})},
        {"primary-saved-origin-sample", "\x1b[1;1HPS\x1b[99;1HBS", 61, "\x1b[20;3R", true,
            controlled_body("PS", "BS"), primary_markers(2, {
                {"PS", 0, 0, 0, 0, 0}, {"x", 9, 61, 154, 10, 0}, {"BS", 18, 0, 164, 19, 0}})},
        {"primary-explicit-origin-on", "\x1b[?6h\x1b[1;1HPI\x1b[99;1HBI", 61, "\x1b[20;3R", true,
            controlled_body("PI", "BI"), primary_markers(2, {
                {"PI", 0, 0, 0, 0, 0}, {"x", 9, 61, 154, 10, 0}, {"BI", 18, 0, 164, 19, 0}})},
        {"primary-explicit-origin-off", "\x1b[?6l\x1b[1;1HPO\x1b[99;1HBO", 61, "\x1b[20;3R", true,
            controlled_body("PO", "BO"), primary_markers(2, {
                {"PO", 0, 0, 0, 0, 0}, {"x", 9, 61, 154, 10, 0}, {"BO", 18, 0, 164, 19, 0}})},
        // This stage follows explicit OFF controls. It is not a second
        // unmodified saved-state observation, and v/w must not reappear.
        {"primary-width-restored-after-controls", "", 91, "\x1b[20;3R", true,
            controlled_body("PO", "BO"), primary_markers(2, {
                {"PO", 0, 0, 0, 0, 0}, {"x", 9, 61, 154, 9, 61}, {"BO", 18, 0, 164, 18, 0}})},
    };
}

static void replay(Checks& checks, const std::vector<Stage>& stages, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    const auto prefix = std::string("hidden-primary/") +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < stages.size(); ++phase) {
        const auto& stage = stages[phase];
        checks.context = prefix + stage.label;
        const auto previous_failures = checks.failures;
        if (phase && stage.columns != stages[phase - 1].columns)
            parser.screen.resize_columns_preserving(stage.columns);
        feed(parser, stage.bytes, bytewise);
        checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                      "actual protocol grid differs from the literal phase dimensions");
        feed(parser, "\x1b[6n", bytewise);
        const auto replies = parser.take_replies();
        checks.expect(replies == std::vector<std::string>{stage.reply},
                      "DSR reply differs from the literal phase response ESC" + std::string(stage.reply + 1));
        if (stage.compare_source) {
            try {
                const auto lines = source_lines(parser.screen);
                check_body(checks, lines, stage.body);
                check_markers(checks, lines, stage.markers, false);
            } catch (const std::exception& error) {
                checks.expect(false, std::string("source observation failed: ") + error.what());
            }
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
    const auto stages = hidden_primary();
    Checks checks;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool bytewise : {false, true}) {
            try {
                replay(checks, stages, mode, bytewise);
            } catch (const std::exception& error) {
                checks.expect(false, std::string("replay failed: ") + error.what());
            }
        }
    std::cout << "retained-column hidden-primary checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (1 separately audited original case x strict/recovery x whole/bytewise; "
                 "6 main source/body stages and 8 DSR/display stages; no internal-mode/grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
