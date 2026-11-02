// Literal pure-height observation from height-retention-measured, one original run.
// Strict public JSON SHA256:
// 32d3f6cf2d1c6746ff517b684ecd8c631c86ecc87874209ba5f831ea244730e0.
// This checks fixed-width source text, DSR and seven ASCII marker positions.
// Original Document offsets are not Cell coordinates. History/IDs below are
// separately labelled TermForge engineering checks, not original storage facts.
// No joint width/height, full original grid, styles, Unicode or Qt acceptance.
#include "terminal.hpp"
#include "history_view.hpp"
#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

struct Checks {
    std::string context;
    size_t failures = 0, assertions = 0, engineering_assertions = 0;
    size_t phases = 0, passed_phases = 0;
    void expect(bool condition, const std::string& message, bool engineering = false) {
        ++assertions;
        if (engineering) ++engineering_assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << (engineering ? " [engineering]: " : " [literal]: ") << message << '\n';
    }
};

static void feed(Parser& parser, std::string_view bytes, bool bytewise) {
    if (bytewise) {
        for (const char byte : bytes) parser.feed(std::string_view(&byte, 1));
    } else parser.feed(bytes);
}

static void omit_empty_tail(std::vector<std::string>& lines) {
    // Only allocation-capacity differences are excluded. Interior empty rows
    // and every written or source-significant space remain part of the oracle.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

static std::vector<std::string> source_lines(const Screen& screen) {
    TextDocument document(screen);
    std::vector<std::string> lines;
    std::string logical;
    for (size_t rank = 0; rank < document.size(); ++rank) {
        const auto line = document.line(rank);
        logical += line.projection.text;
        if (!line.joins_next) {
            lines.push_back(std::move(logical));
            logical.clear();
        }
    }
    if (!logical.empty()) lines.push_back(std::move(logical));
    omit_empty_tail(lines);
    return lines;
}

using DisplayPosition = std::pair<int, int>;
static std::vector<DisplayPosition> marker_positions(const ViewSnapshot& snapshot,
                                                     std::string_view marker) {
    std::vector<DisplayPosition> result;
    // Every fixture marker is ASCII and one character per Cell. Match actual
    // Cells; do not reinterpret original API offsets or general UTF-8 lengths.
    for (size_t row = 0; row < snapshot.lines.size(); ++row) {
        const auto& line = snapshot.lines[row];
        for (size_t column = 0; column + marker.size() <= line.size(); ++column) {
            bool matches = true;
            for (size_t i = 0; i < marker.size(); ++i) {
                const auto& cell = line[column + i];
                if (cell.width != 1 || cell.text != std::string(1, marker[i])) { matches = false; break; }
            }
            if (matches) result.emplace_back(static_cast<int>(row), static_cast<int>(column));
        }
    }
    return result;
}

static void replay(Checks& checks, ParseMode mode, bool bytewise) {
    // Exactly 75 bytes; SHA256:
    // ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e.
    constexpr std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B\x1b[12;90Huvw\x1b[19;1HLOWEND";
    static_assert(source.size() == 75);
    // The original has 19 logical entries, including a final empty string.
    // All three phases share canonical array SHA256:
    // ac5cc0c46768d14945e8c63103f3d7d56f9a5fc2f5631dd4fe8f485e9ba19efc.
    std::vector<std::string> expected_body = {
        "", "HTOP", "", "", "", "HARD_A", "HARD_B", "", "", "", "",
        std::string(89, ' ') + "uvw", "", "", "", "", "", "LOWEND", ""
    };
    omit_empty_tail(expected_body);
    constexpr std::array<int, 3> heights = {20, 14, 20};
    constexpr std::array<const char*, 3> replies = {"\x1b[19;7R", "\x1b[14;7R", "\x1b[14;7R"};
    constexpr std::array<const char*, 7> markers = {"HTOP", "HARD_A", "HARD_B", "u", "v", "w", "LOWEND"};
    // Literal rectangle observations: origin x=147, ASCII pitch 8x18.
    // x=[147,147,147,859,867,147,147]; initial y=[18,90,108,198,198,216,324].
    // Both later y=[-72,0,18,108,108,126,234]. Negative y is preserved below
    // as an off-viewport position, not forced into a visible snapshot row.
    constexpr std::array<int, 7> columns = {0, 0, 0, 89, 90, 0, 0};
    constexpr std::array<std::array<int, 7>, 3> rows = {{
        {1, 5, 6, 11, 11, 12, 18}, {-4, 0, 1, 6, 6, 7, 13}, {-4, 0, 1, 6, 6, 7, 13}
    }};
    Parser parser(20, 91, 1000, mode);
    HistoryViewport live_viewport, projection_viewport;
    std::vector<std::pair<uint64_t, uint64_t>> original_retained_identity;
    const auto prefix = std::string("pure-height/") +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < heights.size(); ++phase) {
        checks.context = prefix + std::to_string(phase);
        const auto previous_failures = checks.failures;
        if (phase == 0) feed(parser, source, bytewise);
        else parser.screen.resize(heights[phase], 91); // Pure height; no retained-width API.
        checks.expect(parser.screen.width() == 91 && parser.screen.height() == heights[phase], "protocol grid differs");
        feed(parser, "\x1b[6n", bytewise);
        checks.expect(parser.take_replies() == std::vector<std::string>{replies[phase]}, "DSR differs");

        const auto body = source_lines(parser.screen);
        checks.expect(body.size() == expected_body.size(), "logical body extent differs");
        for (size_t row = 0; row < expected_body.size(); ++row)
            checks.expect(row < body.size() && body[row] == expected_body[row],
                          "logical body row " + std::to_string(row) + " differs");

        const auto snapshot = live_viewport.snapshot(parser.screen);
        const auto page = projection_viewport.project(parser.screen, 91, 200, {0, 0}, parser.screen.retained_size());
        checks.expect(page.stop == ProjectionStop::source_end, "source projection did not cover the bounded document", true);
        for (size_t marker = 0; marker < markers.size(); ++marker) {
            std::vector<DisplayPosition> expected_visible;
            if (rows[phase][marker] >= 0 && rows[phase][marker] < heights[phase])
                expected_visible.emplace_back(rows[phase][marker], columns[marker]);
            checks.expect(marker_positions(snapshot, markers[marker]) == expected_visible,
                          std::string("visible marker ") + markers[marker] + " differs");
            auto projected = marker_positions(page.snapshot, markers[marker]);
            for (auto& position : projected) position.first -= static_cast<int>(snapshot.viewport.top);
            checks.expect(projected == std::vector<DisplayPosition>{{rows[phase][marker], columns[marker]}},
                          std::string("projected marker ") + markers[marker] + " relative to live top differs");
        }

        // Engineering conservation checks. Five retained rows and stable IDs
        // are this implementation's means of matching the visible observation;
        // the original's scroll=5 scalar does not establish its storage layout.
        checks.expect(parser.screen.history_lines().size() == (phase == 0 ? 0u : 5u), "history size differs", true);
        if (phase == 0) {
            for (size_t rank = 0; rank < 19; ++rank) {
                const auto& line = parser.screen.retained_line(rank);
                original_retained_identity.emplace_back(line.id, line.version);
            }
        } else {
            for (size_t rank = 0; rank < original_retained_identity.size(); ++rank) {
                const auto& line = parser.screen.retained_line(rank);
                checks.expect(std::make_pair(line.id, line.version) == original_retained_identity[rank],
                              "retained source identity/version changed at rank " + std::to_string(rank), true);
            }
        }
        ++checks.phases;
        if (checks.failures == previous_failures) ++checks.passed_phases;
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
    std::cout << "measured pure-height replay: " << checks.passed_phases << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (" << checks.engineering_assertions << " separate engineering assertions; "
                 "1 original case x 3 stages x strict/recovery x whole/bytewise; no joint width/height or Qt claim)\n";
    return checks.failures ? 1 : 0;
}
