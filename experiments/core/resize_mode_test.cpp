// Literal WindTerm 2.7.0 resize-mode observations: three independent original
// cases from research/ui/resize_mode_reference.md. Public recording SHA256:
// active-primary: ad30c3e9228a8fe27da6f47b32fcc6cd91d08685e7622ff037cb26b58dba13b9
// active-alternate: 8345169c6f5cc71ee00996d0dee96a7f3ffebfb2ce4dde64f75098abc6213f64
// saved-primary-edge: 22408df94f492e267dea7ad3de2c3a412d6eac3a16c677b250f6d224474fd43a
// Protocol replies, main-screen source/body and visible marker coordinates are
// separate observations. No private origin, margin or pending bit is an oracle.
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
    size_t failures = 0, assertions = 0, phases = 0, passed_phases = 0, source_phases = 0;

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
    for (const char* label : {"T0", "R2", "R3", "R4", "R5", "B6", "B0", "MP", "AP", "UP", "DN",
                              "0123456789", "ANCHOR", "W", "X"}) {
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


static std::vector<std::string> region_body(bool upper, bool lower) {
    // Independent literal main document. Its 20-line array hashes are:
    // phases 0/1: 558ba8f0712129813799ed178a0759c985f4c5bfd937bcf4c939a34d252f5567
    // phase 2:    30761b9487f188cace80622f063df015011043518b10f1f323834caf9fdc26ac
    // phases 3/4: 1f745ba5d8d6e7fa041f03ef27e79ba3f290578f32bfad5f01009032f227250c
    std::vector<std::string> result = {
        "     T0", "     R2", "     R3  MP", "     R4", "     R5", "     B6",
        "", "", "", "", "", "", "", "", "", "", "", "", "", "     B0"
    };
    if (upper) result[0] = "UP   T0";
    if (lower) result[19] = "DN   B0";
    return result;
}

static std::vector<Marker> region_primary_markers(bool upper, bool lower) {
    std::vector<Marker> result = {
        {"T0", 0, 5, 5, 0, 5}, {"R2", 1, 5, 13, 1, 5},
        {"R3", 2, 5, 21, 2, 5}, {"MP", 2, 9, 25, 2, 9},
        {"R4", 3, 5, 33, 3, 5}, {"R5", 4, 5, 41, 4, 5},
        {"B6", 5, 5, 49, 5, 5}, {"B0", 19, 5, 70, 19, 5}
    };
    if (upper) result.push_back({"UP", 0, 0, 0, 0, 0});
    if (lower) result.push_back({"DN", 19, 0, 65, 19, 0});
    return result;
}

static std::vector<Marker> region_alternate_display(bool upper, bool lower) {
    // Source coordinates are deliberately unused for alternate observations.
    // The original document has its own extra index/scroll convention; no +1
    // correction is applied to candidate source coordinates. Visible cells
    // come only from the measured rectangles, origin (147,0), cell size 8x18.
    std::vector<Marker> result = {
        {"T0", 0, 0, 0, 0, 5}, {"R2", 0, 0, 0, 1, 5},
        {"R3", 0, 0, 0, 2, 5}, {"AP", 0, 0, 0, 2, 9},
        {"R4", 0, 0, 0, 3, 5}, {"R5", 0, 0, 0, 4, 5},
        {"B6", 0, 0, 0, 5, 5}, {"B0", 0, 0, 0, 19, 5}
    };
    if (upper) result.push_back({"UP", 0, 0, 0, 0, 0});
    if (lower) result.push_back({"DN", 0, 0, 0, 19, 0});
    return result;
}

static Case active_primary() {
    // Exact 98-byte input; SHA256:
    // 8fac11288896ac3d083ec37a0f756084ae32f6a649573a44c9125d1493075cbb.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[1;6HT0\x1b[2;6HR2\x1b[3;6HR3\x1b[4;6HR4\x1b[5;6HR5"
        "\x1b[6;6HB6\x1b[20;6HB0\x1b[2;5r\x1b[?6h\x1b[2;10HMP";
    return {"active-primary", {
        {"primary-region-origin-on", source, 91, "\x1b[2;12R", true,
            region_body(false, false), region_primary_markers(false, false)},
        {"primary-shrunk-unmodified", "", 61, "\x1b[3;12R", true,
            region_body(false, false), region_primary_markers(false, false)},
        {"primary-upper-CUP", "\x1b[1;1HUP", 61, "\x1b[1;3R", true,
            region_body(true, false), region_primary_markers(true, false)},
        {"primary-lower-CUP", "\x1b[99;1HDN", 61, "\x1b[20;3R", true,
            region_body(true, true), region_primary_markers(true, true)},
        {"primary-restored-unmodified", "", 91, "\x1b[20;3R", true,
            region_body(true, true), region_primary_markers(true, true)}
    }};
}

static Case active_alternate() {
    // Exact 106-byte input; SHA256:
    // e898817eda5e89bbe010ac5ca1f5f1a99f7560c7dd8afc7d86da32007ffd4306.
    const std::string_view source = "\x1b[?1049h\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[1;6HT0\x1b[2;6HR2\x1b[3;6HR3\x1b[4;6HR4\x1b[5;6HR5"
        "\x1b[6;6HB6\x1b[20;6HB0\x1b[2;5r\x1b[?6h\x1b[2;10HAP";
    return {"active-alternate", {
        {"alternate-region-origin-on", source, 91, "\x1b[2;12R", false, {},
            region_alternate_display(false, false)},
        {"alternate-shrunk-unmodified", "", 61, "\x1b[3;12R", false, {},
            region_alternate_display(false, false)},
        {"alternate-upper-CUP", "\x1b[1;1HUP", 61, "\x1b[1;3R", false, {},
            region_alternate_display(true, false)},
        {"alternate-lower-CUP", "\x1b[99;1HDN", 61, "\x1b[20;3R", false, {},
            region_alternate_display(true, true)},
        {"alternate-restored-unmodified", "", 91, "\x1b[20;3R", false, {},
            region_alternate_display(true, true)}
    }};
}

static std::vector<std::string> saved_body(std::string tail) {
    return {"", "0123456789", "ANCHOR", "", "", "", "", std::move(tail)};
}

static std::vector<Marker> saved_markers(std::initializer_list<Marker> markers) {
    std::vector<Marker> result = {
        {"0123456789", 1, 0, 1, 1, 0}, {"ANCHOR", 2, 0, 12, 2, 0}
    };
    result.insert(result.end(), markers);
    return result;
}

static Case saved_primary_edge() {
    // Exact 57-byte input; SHA256:
    // a052dbd92f9c02f5ca260e577ac30424caa775da86dd37532dc2f611f89e2819.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;91HW";
    return {"saved-primary-edge", {
        {"primary-old-edge-W", source, 91, "\x1b[8;91R", true,
            saved_body(std::string(90, ' ') + "W"), saved_markers({{"W", 7, 90, 113, 7, 90}})},
        // Main ANCHOR/W still appear in the original document here, but their
        // rectangles have negative y. They are not visible alternate markers.
        {"alternate-before-shrink", "\x1b[?1049h\x1b[2;10HAP", 91, "\x1b[2;12R", false, {},
            {{"AP", 0, 0, 0, 1, 9}}},
        {"alternate-shrunk-primary-hidden", "", 61, "\x1b[2;12R", false, {},
            {{"AP", 0, 0, 0, 1, 9}}},
        // Return is observed before X and contains no corrective CUP. The
        // entire source array ends with 61 spaces, not an empty string:
        // d406c228d3792cc9550b15fdc7e4ea405e0be7c4a53d859b3159a5f2ef6da919.
        {"primary-return-unmodified", "\x1b[?1049l", 61, "\x1b[8;61R", true,
            saved_body(std::string(61, ' ')), saved_markers({})},
        // The following two whole source arrays have canonical JSON SHA256:
        // 09574749f53998f85aa66dfbcb8083375c03543ddbdcd89a0ce16e573f1008bc.
        {"primary-next-X", "X", 61, "\x1b[8;61R", true,
            saved_body(std::string(60, ' ') + "X"), saved_markers({{"X", 7, 60, 83, 7, 60}})},
        {"primary-restored-after-X", "", 91, "\x1b[8;61R", true,
            saved_body(std::string(60, ' ') + "X"), saved_markers({{"X", 7, 60, 83, 7, 60}})}
    }};
}

static void replay(Checks& checks, const Case& sample, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    const auto& stages = sample.stages;
    const auto prefix = std::string(sample.name) + "/" +
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
            ++checks.source_phases;
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
    const std::vector<Case> cases = {active_primary(), active_alternate(), saved_primary_edge()};
    Checks checks;
    for (const auto& sample : cases)
        for (const auto mode : {ParseMode::strict, ParseMode::recovery})
            for (const bool bytewise : {false, true}) {
                try {
                    replay(checks, sample, mode, bytewise);
                } catch (const std::exception& error) {
                    checks.expect(false, std::string("replay failed: ") + error.what());
                }
            }
    std::cout << "retained-column resize-mode checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (3 separately audited original cases x strict/recovery x whole/bytewise; "
              << checks.source_phases << " main source/body observations; all stages DSR/display; "
                 "no alternate document-index correction or internal-mode/grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
