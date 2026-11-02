// Literal WindTerm 2.7.0 width-height-retention observation: one run, five stages.
// Strict public JSON SHA256:
// 6a1561f3e6f6bc480c6a2f086adf7ed4798d5007b04a7bc71c7ae9deba90cfe6.
// Authorized plan SHA256:
// 97e42ad2f90d4fd44a096f3843d32e329fc182748febae6d3099ef674a347c4b.
// Original logical text, DSR and ASCII marker rectangles are separate oracles.
// Source mapping and full-text selection are TermForge engineering checks.
// Original scroll=5 does not establish an internal physical history layout.
// This does not cover full original grids, styles, Unicode, ConPTY or Qt.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

struct Checks {
    std::string context;
    size_t literal_assertions = 0, literal_failures = 0;
    size_t engineering_assertions = 0, engineering_failures = 0;
    size_t phases = 0, literal_passed_phases = 0, engineering_passed_phases = 0;

    void expect(bool condition, const std::string& message, bool engineering = false) {
        if (engineering) ++engineering_assertions;
        else ++literal_assertions;
        if (condition) return;
        if (engineering) ++engineering_failures;
        else ++literal_failures;
        std::cerr << context << (engineering ? " [engineering]: " : " [literal]: ") << message << '\n';
    }
};

using DisplayPosition = std::pair<int, int>;
using LogicalPosition = std::pair<size_t, size_t>;

struct Stage {
    const char* label;
    int columns, rows;
    const char* reply;
    std::array<DisplayPosition, 7> markers;
};

static constexpr std::array<const char*, 7> marker_names = {
    "HTOP", "HARD_A", "HARD_B", "u", "v", "w", "LOWEND"
};
static constexpr std::array<LogicalPosition, 7> original_logical_positions = {{
    {1, 0}, {5, 0}, {6, 0}, {11, 89}, {11, 90}, {11, 91}, {17, 0}
}};

// Pixel origin x=147 and ASCII pitch 8x18. These literal row/column values
// come from the fixed rectangles, not from original scroll or caretPosition.
// Initial x=147/147/147/859/867/147/147, y=18/90/108/198/198/216/324.
// Narrow u/v/w x=371/379/387, y=216; after height change their y=126.
// The last stage restores u/v to x=859/867,y=108 and w to x=147,y=126.
// HTOP y=-72 after shrinking height remains off-viewport, never clamped to 0.
static const std::array<Stage, 5> stages = {{
    {"initial-body", 91, 20, "\x1b[19;7R", {{{1, 0}, {5, 0}, {6, 0}, {11, 89}, {11, 90}, {12, 0}, {18, 0}}}},
    {"width-shrunk-unmodified", 61, 20, "\x1b[19;7R", {{{1, 0}, {5, 0}, {6, 0}, {12, 28}, {12, 29}, {12, 30}, {18, 0}}}},
    {"height-shrunk-unmodified", 61, 14, "\x1b[14;7R", {{{-4, 0}, {0, 0}, {1, 0}, {7, 28}, {7, 29}, {7, 30}, {13, 0}}}},
    {"height-restored-unmodified", 61, 20, "\x1b[14;7R", {{{-4, 0}, {0, 0}, {1, 0}, {7, 28}, {7, 29}, {7, 30}, {13, 0}}}},
    {"width-restored-unmodified", 91, 20, "\x1b[14;7R", {{{-4, 0}, {0, 0}, {1, 0}, {6, 89}, {6, 90}, {7, 0}, {13, 0}}}}
}};

static void feed(Parser& parser, std::string_view bytes, bool bytewise) {
    if (bytewise) {
        for (const char byte : bytes) parser.feed(std::string_view(&byte, 1));
    } else parser.feed(bytes);
}

static void omit_empty_tail(std::vector<std::string>& lines) {
    // Only terminal empty strings from allocation capacity are excluded.
    // Interior empty lines and every source-significant space remain exact.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

static std::vector<std::string> source_lines(const TextDocument& document) {
    if (document.size() > 200) throw std::runtime_error("source exceeds the bounded fixture");
    std::vector<std::string> result;
    std::string logical;
    for (size_t rank = 0; rank < document.size(); ++rank) {
        const auto line = document.line(rank);
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

static std::vector<DisplayPosition> marker_positions(const ViewSnapshot& page, std::string_view marker) {
    std::vector<DisplayPosition> result;
    for (size_t row = 0; row < page.lines.size(); ++row) {
        const auto& line = page.lines[row];
        for (size_t column = 0; column + marker.size() <= line.size(); ++column) {
            bool matches = true;
            for (size_t offset = 0; offset < marker.size(); ++offset) {
                const auto& cell = line[column + offset];
                if (cell.width != 1 || cell.text != std::string(1, marker[offset])) {
                    matches = false;
                    break;
                }
            }
            if (matches) result.emplace_back(static_cast<int>(row), static_cast<int>(column));
        }
    }
    return result;
}

static std::optional<LogicalPosition> logical_position(const TextDocument& document, TextPosition position) {
    if (!document.valid(position)) return std::nullopt;
    size_t row = 0, prefix = 0;
    for (size_t rank = 0; rank < document.size(); ++rank) {
        const auto line = document.line(rank);
        if (line.projection.id == position.line_id) return LogicalPosition{row, prefix + position.column};
        // This fixture is ASCII; Cell columns and text offsets agree within
        // each source fragment. This is not a UTF-8 or original API offset rule.
        if (line.joins_next) prefix += line.projection.text.size();
        else { ++row; prefix = 0; }
    }
    return std::nullopt;
}

static std::optional<TextRange> complete_text_range(const TextDocument& document) {
    const auto first = document.position(0, 0);
    if (!first) return std::nullopt;
    for (size_t rank = document.size(); rank > 0; --rank) {
        const auto line = document.line(rank - 1);
        if (line.projection.spans.empty()) continue;
        const auto last = document.position(rank - 1, line.projection.spans.back().column_end);
        if (last) return TextRange{*first, *last};
    }
    return std::nullopt;
}

static void resize_existing_path(Screen& screen, const Stage& stage) {
    // Exercise the existing real APIs. Width and height change in separate
    // stages; the height path must not be replaced by an invented stub.
    if (screen.width() != stage.columns) {
        if (screen.height() != stage.rows) throw std::runtime_error("fixture changes two axes at once");
        screen.resize_columns_preserving(stage.columns);
    } else if (screen.height() != stage.rows) {
        screen.resize(stage.rows, stage.columns);
    }
}

static void replay(Checks& checks, ParseMode mode, bool bytewise) {
    // Original 75 B input SHA256:
    // ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e.
    constexpr std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B\x1b[12;90Huvw\x1b[19;1HLOWEND";
    static_assert(source.size() == 75);
    // Independently reconstructed original array: 19 entries in every stage.
    // Canonical JSON SHA256:
    // ac5cc0c46768d14945e8c63103f3d7d56f9a5fc2f5631dd4fe8f485e9ba19efc.
    std::vector<std::string> expected_body = {
        "", "HTOP", "", "", "", "HARD_A", "HARD_B", "", "", "", "",
        std::string(89, ' ') + "uvw", "", "", "", "", "", "LOWEND", ""
    };
    omit_empty_tail(expected_body);
    std::string expected_copy;
    for (size_t row = 0; row < expected_body.size(); ++row) {
        if (row) expected_copy += "\r\n";
        expected_copy += expected_body[row];
    }

    Parser parser(20, 91, 1000, mode);
    HistoryViewport live_viewport, projection_viewport;
    TextSelection full_selection;
    const auto prefix = std::string("combined/") +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");

    for (size_t phase = 0; phase < stages.size(); ++phase) {
        const auto& stage = stages[phase];
        checks.context = prefix + stage.label;
        const auto literal_before = checks.literal_failures;
        const auto engineering_before = checks.engineering_failures;
        if (phase == 0) feed(parser, source, bytewise);
        else resize_existing_path(parser.screen, stage);
        checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == stage.rows,
                      "protocol grid differs");
        feed(parser, "\x1b[6n", bytewise);
        checks.expect(parser.take_replies() == std::vector<std::string>{stage.reply}, "DSR differs");

        TextDocument document(parser.screen);
        const auto body = source_lines(document);
        checks.expect(body.size() == expected_body.size(), "logical body extent differs");
        for (size_t row = 0; row < expected_body.size(); ++row)
            checks.expect(row < body.size() && body[row] == expected_body[row],
                          "logical body row " + std::to_string(row) + " differs");

        if (phase == 0) {
            const auto range = complete_text_range(document);
            checks.expect(range && full_selection.set(document, *range), "cannot select complete source text", true);
        }
        const auto copied = full_selection.copy(document, 4096);
        checks.expect(copied.status == SelectionCopyStatus::ok && copied.text == expected_copy,
                      "full-text selection lost or changed retained content", true);

        // Source traversal uses the real retained size, not an out-of-range
        // source_end=200. The 200-row output cap bounds this fixed small case.
        const auto page = projection_viewport.project(parser.screen, static_cast<size_t>(stage.columns),
                                                      200, {0, 0}, parser.screen.retained_size());
        const auto revision = projection_viewport.layout_revision();
        checks.expect(page.stop == ProjectionStop::source_end, "projection did not cover the complete source", true);
        const auto live = live_viewport.reconcile(parser.screen);
        // The live source anchor is an implementation value, not original
        // scroll. In this fixture every line above it is a short hard line,
        // so its source rank also denotes the projection's display origin.
        for (size_t marker = 0; marker < marker_names.size(); ++marker) {
            const auto positions = marker_positions(page.snapshot, marker_names[marker]);
            auto relative = positions;
            for (auto& position : relative) position.first -= static_cast<int>(live.top);
            checks.expect(relative == std::vector<DisplayPosition>{stage.markers[marker]},
                          std::string("projected marker ") + marker_names[marker] + " differs");

            const auto name = std::string(marker_names[marker]);
            checks.expect(positions.size() == 1, "mapping requires exactly one " + name, true);
            if (positions.size() != 1) continue;
            const auto row = static_cast<size_t>(positions.front().first);
            const auto column = static_cast<size_t>(positions.front().second);
            const auto begin = page.snapshot.mapping.hit(document, row, column, ViewAffinity::leading, revision);
            const auto end = page.snapshot.mapping.hit(document, row, column + name.size(), ViewAffinity::trailing, revision);
            checks.expect(begin && logical_position(document, *begin) == original_logical_positions[marker],
                          name + " maps to a different logical source position", true);
            TextSelection marker_selection;
            const bool selected = begin && end && *begin != *end &&
                marker_selection.set(document, {*begin, *end}) && marker_selection.range().has_value();
            checks.expect(selected, "cannot select mapped " + name, true);
            if (selected) {
                const auto text = marker_selection.copy(document, 64);
                checks.expect(text.status == SelectionCopyStatus::ok && text.text == name,
                              "mapped selection does not copy " + name, true);
                const auto highlight = page.snapshot.mapping.project(document, *marker_selection.range(), revision);
                checks.expect(highlight.size() == 1 && highlight.front().row == row &&
                              highlight.front().column_begin == column && highlight.front().column_end == column + name.size(),
                              "mapped highlight differs for " + name, true);
            }
        }
        ++checks.phases;
        if (checks.literal_failures == literal_before) ++checks.literal_passed_phases;
        if (checks.engineering_failures == engineering_before) ++checks.engineering_passed_phases;
    }
    parser.finish();
}

int main() {
    Checks checks;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool bytewise : {false, true}) {
            try { replay(checks, mode, bytewise); }
            catch (const std::exception& error) { checks.expect(false, std::string("replay exception: ") + error.what()); }
        }
    std::cout << "combined width/height literal replay: " << checks.literal_passed_phases << '/' << checks.phases
              << " phases, " << checks.literal_failures << " failed assertions of " << checks.literal_assertions << '\n';
    std::cout << "separate mapping/selection engineering checks: " << checks.engineering_passed_phases << '/' << checks.phases
              << " phases, " << checks.engineering_failures << " failed assertions of " << checks.engineering_assertions
              << " (1 original case x 5 stages x strict/recovery x whole/bytewise; "
                 "existing width-preserving + legacy-height route; no original internal-history or Qt claim)\n";
    return checks.literal_failures || checks.engineering_failures ? 1 : 0;
}
