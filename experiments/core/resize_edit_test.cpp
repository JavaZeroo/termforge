// Literal WindTerm 2.7.0 resize/edit observations through the retained-column
// transaction. Document locations, display locations and DSR stay independent;
// this does not assert full grids, styles, Qt or native transport behavior.
#include "terminal.hpp"
#include "history_view.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

enum class Edit { right_edge_wrap, insert_char, delete_char };

struct Case {
    const char* name;
    Edit edit;
};

struct Marker {
    const char* text;
    size_t source_row, source_column, source_position;
    size_t display_row, display_column;
};

struct Location {
    size_t row, column;
    // Absolute ASCII document offset, including hard-line separators. Display
    // comparisons use zero here because display rows are not document lines.
    size_t position;

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

// The public document supplies actual soft continuations. Expected positions
// below come from the strict export and do not depend on physical source IDs.
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
    for (const char* label : {"0123456789", "ANCHOR", "v", "w", "x", "Q", "R"}) {
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
        // Both lists are in reading order. Exact sets reject extra occurrences,
        // including confusion between ANCHOR's R and the subsequent edit R.
        checks.expect(actual == wanted, std::string(display ? "display" : "source") + " marker " + label +
            " expected " + describe(wanted, display) + ", observed " + describe(actual, display));
    }
}

static std::vector<Marker> expected_markers(Edit edit, size_t phase) {
    // Zero-based source coordinates and scalar offsets are literal public
    // document observations. Display literals use the observed 8x18 grid,
    // rect origin (147,0); no protocol-cursor-to-display conversion is assumed.
    std::vector<Marker> expected = {
        {"0123456789", 1, 0, 1, 1, 0},
        {"ANCHOR", 2, 0, 12, 2, 0},
        {"R", 2, 5, 17, 2, 5},
    };
    if (edit != Edit::right_edge_wrap && phase >= 3) {
        // Independent ICH and DCH exports have equal observed results for this
        // source. Offsets distinguish 61 retained blanks from an empty line.
        if (phase == 3) {
            expected.insert(expected.end(), {
                {"w", 9, 90, 176, 10, 29},
                {"x", 9, 91, 177, 10, 30},
            });
        } else if (phase == 4) {
            expected.insert(expected.end(), {
                {"w", 9, 90, 176, 9, 90},
                {"x", 9, 91, 177, 10, 0},
            });
        } else {
            throw std::invalid_argument("unknown ICH/DCH phase");
        }
        return expected;
    }
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
        // Q already removes v and 30 document characters before R consumes
        // pending wrap. Combining these stages would hide that distinction.
        expected.insert(expected.end(), {
            {"Q", 7, 60, 83, 7, 60},
            {"w", 9, 90, 176, 10, 29},
            {"x", 9, 91, 177, 10, 30},
        });
        break;
    case 4:
        expected.insert(expected.end(), {
            {"Q", 7, 60, 83, 7, 60},
            {"R", 7, 61, 84, 8, 0},
            {"w", 8, 90, 176, 10, 29},
            {"x", 8, 91, 177, 10, 30},
        });
        break;
    case 5:
        expected.insert(expected.end(), {
            {"Q", 7, 60, 83, 7, 60},
            {"R", 7, 61, 84, 7, 61},
            {"w", 8, 90, 176, 8, 90},
            {"x", 8, 91, 177, 9, 0},
        });
        break;
    default:
        throw std::invalid_argument("unknown right-edge-wrap phase");
    }
    return expected;
}

static void replay(Checks& checks, const Case& test, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    // Original phase 0: 78 bytes, SHA256
    // c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l\x1b[8;91Huv"
        "\x1b[?7h\x1b[10;91Hwx";
    struct Stage { const char* label; std::string_view bytes; int columns; const char* reply; };
    std::vector<Stage> stages = {
        {"initial-body", source, 91, "\x1b[11;2R"},
        {"after-shrink-before-preparation", "", 61, "\x1b[11;2R"},
        {"after-preparation-before-edit", "\x1b[8;61H", 61, "\x1b[8;61R"},
    };
    switch (test.edit) {
    case Edit::right_edge_wrap:
        stages.insert(stages.end(), {
            {"after-Q", "Q", 61, "\x1b[8;61R"},
            {"after-R", "R", 61, "\x1b[9;2R"},
            {"after-restore-after-edit", "", 91, "\x1b[9;2R"},
        });
        break;
    case Edit::insert_char:
        stages.insert(stages.end(), {
            {"after-insert-char", "\x1b[1@", 61, "\x1b[8;61R"},
            {"after-restore-after-edit", "", 91, "\x1b[8;61R"},
        });
        break;
    case Edit::delete_char:
        stages.insert(stages.end(), {
            {"after-delete-char", "\x1b[1P", 61, "\x1b[8;61R"},
            {"after-restore-after-edit", "", 91, "\x1b[8;61R"},
        });
        break;
    }
    const auto prefix = std::string(test.name) + "/" +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < stages.size(); ++phase) {
        const auto& stage = stages[phase];
        checks.context = prefix + stage.label;
        const auto previous_failures = checks.failures;
        if (phase == 1 || phase + 1 == stages.size()) parser.screen.resize_columns_preserving(stage.columns);
        feed(parser, stage.bytes, bytewise);
        checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                      "actual protocol grid must match the observed phase dimensions");
        // Preparation CUP belongs only to phase 2. Earlier DSR is captured
        // before that action and cannot be inferred from its resulting cursor.
        feed(parser, "\x1b[6n", bytewise);
        checks.expect(parser.take_replies() == std::vector<std::string>{stage.reply},
                      "DSR reply differs from the literal phase response");
        const auto expected = expected_markers(test.edit, phase);
        try {
            check_markers(checks, source_lines(parser.screen), expected, false);
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
        // Keep subsequent action/restore observations after an earlier failure.
        ++checks.phases;
        if (checks.failures == previous_failures) ++checks.passed_phases;
    }
    parser.finish();
}

int main() {
    // Oracles: research/ui/results/2026-11-02-resize-edit-reference/*.json
    // right-edge-wrap: 7bb6fe826a60aac32209cc9a7dab33963b71d5e6445a68f8a1b3bdee24d31676
    // insert-char: 1b1880db342fcd73bd228c3ba65af22a2ee5d2a6b06fe5c3702452fddfd78ba7
    // delete-char: 9ae8f94d4ea6289e1c945abac27f4ca53ff0acb86937b662989e6f3baadbe349
    const Case cases[] = {
        {"right-edge-wrap", Edit::right_edge_wrap},
        {"insert-char", Edit::insert_char},
        {"delete-char", Edit::delete_char},
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
    std::cout << "retained-column resize edit checks: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (3 original cases x strict/recovery x whole/bytewise; no full-grid/style claim)\n";
    return checks.failures ? 1 : 0;
}
