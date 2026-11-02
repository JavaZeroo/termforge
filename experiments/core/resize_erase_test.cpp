// Literal WindTerm 2.7.0 erase observations after a retained-width change.
// The original frozen audits failed; independent supplemental audits verified
// the same recordings. Their success does not replace the original failures.
// Compare observed logical text, DSR and display markers, not private state,
// full active grids, styles, Qt or native transport behavior.
#include "terminal.hpp"
#include "history_view.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

enum class Erase { right, whole_line };

struct Case {
    const char* name;
    Erase erase;
    const char* action_label;
    std::string_view action;
};

struct Marker {
    const char* text;
    size_t source_row, source_column, source_position;
    size_t display_row, display_column;
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

// Only terminal empty strings represent unobserved tail capacity here. Keep
// every interior empty line and every space, including entirely blank content.
static void remove_empty_tail(std::vector<std::string>& lines) {
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

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
    remove_empty_tail(result);
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

static std::string line_lengths(const std::vector<std::string>& lines) {
    std::string result = "[";
    for (size_t index = 0; index < lines.size(); ++index) {
        if (index) result += ",";
        result += std::to_string(lines[index].size());
    }
    return result + "]";
}

static void check_markers(Checks& checks, const std::vector<std::string>& lines,
                          const std::vector<Marker>& expected, bool display) {
    for (const char* label : {"0123456789", "ANCHOR", "L", "v", "w", "x"}) {
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

static std::vector<std::string> expected_document(Erase erase, size_t phase) {
    // Literal logical text from the same raw observations whose complete
    // arrays were verified against every supplemental synthetic-lines hash.
    // Only their terminal empty-string capacity has been removed.
    std::vector<std::string> result = {
        "", "0123456789", "ANCHOR", "", "", "", "",
        "L" + std::string(89, ' ') + "v", "", std::string(90, ' ') + "wx",
    };
    if (phase >= 3) {
        if (erase == Erase::right) result[7] = "L" + std::string(59, ' ');
        else result[7] = std::string(60, ' ');
    }
    return result;
}

static std::vector<Marker> expected_markers(Erase erase, size_t phase) {
    // Zero-based document/display coordinates; source_position includes hard
    // line separators. Display literals use the observed 8x18 grid at (147,0).
    std::vector<Marker> expected = {
        {"0123456789", 1, 0, 1, 1, 0},
        {"ANCHOR", 2, 0, 12, 2, 0},
    };
    if (phase < 3 || erase == Erase::right)
        expected.push_back({"L", 7, 0, 23, 7, 0});
    switch (phase) {
    case 0:
        expected.insert(expected.end(), {
            {"v", 7, 90, 113, 7, 90},
            {"w", 9, 90, 206, 9, 90},
            {"x", 9, 91, 207, 10, 0},
        });
        break;
    case 1:
    case 2:
        expected.insert(expected.end(), {
            {"v", 7, 90, 113, 8, 29},
            {"w", 9, 90, 206, 11, 29},
            {"x", 9, 91, 207, 11, 30},
        });
        break;
    case 3:
        expected.insert(expected.end(), {
            {"w", 9, 90, 175, 10, 29},
            {"x", 9, 91, 176, 10, 30},
        });
        break;
    case 4:
        expected.insert(expected.end(), {
            {"w", 9, 90, 175, 9, 90},
            {"x", 9, 91, 176, 10, 0},
        });
        break;
    default:
        throw std::invalid_argument("unknown erase phase");
    }
    return expected;
}

static void replay(Checks& checks, const Case& test, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    // 85B derived source: L is written before the old right edge is populated,
    // not as a post-shrink preparation that could itself discard that tail.
    // SHA256: a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;1HL\x1b[?7l\x1b[8;91Huv"
        "\x1b[?7h\x1b[10;91Hwx";
    struct Stage { const char* label; std::string_view bytes; int columns; const char* reply; };
    const Stage stages[] = {
        {"initial-derived-body-L", source, 91, "\x1b[11;2R"},
        {"after-shrink-before-preparation", "", 61, "\x1b[11;2R"},
        {"after-CUP-before-erase", "\x1b[8;61H", 61, "\x1b[8;61R"},
        {test.action_label, test.action, 61, "\x1b[8;61R"},
        {"after-restore-after-erase", "", 91, "\x1b[8;61R"},
    };
    const auto prefix = std::string(test.name) + "/" +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < 5; ++phase) {
        const auto& stage = stages[phase];
        checks.context = prefix + stage.label;
        const auto previous_failures = checks.failures;
        if (phase == 1 || phase == 4) parser.screen.resize_columns_preserving(stage.columns);
        feed(parser, stage.bytes, bytewise);
        checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                      "actual protocol grid differs from the literal phase dimensions");
        feed(parser, "\x1b[6n", bytewise);
        checks.expect(parser.take_replies() == std::vector<std::string>{stage.reply},
                      "DSR reply differs from the literal phase response");
        const auto expected = expected_markers(test.erase, phase);
        try {
            const auto actual = source_lines(parser.screen);
            const auto document = expected_document(test.erase, phase);
            checks.expect(actual == document, "complete logical text differs; expected row lengths " +
                line_lengths(document) + ", observed " + line_lengths(actual));
            check_markers(checks, actual, expected, false);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("source observation failed: ") + error.what());
        }
        try {
            const auto page = viewport.project(parser.screen, static_cast<size_t>(stage.columns), 20,
                                               {0, 0}, parser.screen.retained_size());
            check_markers(checks, display_lines(page.snapshot), expected, true);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("display projection failed: ") + error.what());
        }
        ++checks.phases;
        if (checks.failures == previous_failures) ++checks.passed_phases;
    }
    parser.finish();
}

int main() {
    // Oracles: independent supplemental audits of the original failed runs.
    // erase-right: 2db5a322d88e843f5dbb1be718d12db9710dcc6080a5f57d6b74dc560f43d57e
    // erase-whole-line: 60d064d279bfa44afdea005c46f966db661c0a0a7cd502c59a7dcc9952a28f56
    const Case cases[] = {
        {"erase-right", Erase::right, "after-erase-right", "\x1b[0K"},
        {"erase-whole-line", Erase::whole_line, "after-erase-whole-line", "\x1b[2K"},
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
    std::cout << "retained-column resize erase checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (2 supplement-audited original cases x strict/recovery x whole/bytewise; no active-grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
