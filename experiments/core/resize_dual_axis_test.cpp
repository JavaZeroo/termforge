// Bounded literal replay of the 2026-11-04 WindTerm dual-axis observation.
//
// Recorded original sample (research/ui/results/2026-11-04-resize-dual-axis-reference):
// 91x20 body -> one window action 61x14 -> CUP(7;2)+"DQ"+DSR (original DSR
// 7;4, scroll 5, document row 11 " DQ"+58 spaces+"w", 62 chars) -> one window
// action back to 91x20, DSR/scroll still 7;4/5 and the written row unchanged.
// The retained owner keeps the full 92-character row through the dual-axis
// transaction; the fixed write then materializes that edited row to the
// current 61-column footprint, which reproduces the recorded 62-character
// document row and the 7;4 cursor. This is the observed original behavior for
// this fixed write; it is not a general reflow or scroll oracle.
#include "terminal.hpp"
#include "text_document.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace termforge;

namespace {
struct Checks {
    size_t assertions = 0, failures = 0;
    void expect(bool condition, const char* message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << "dual-axis replay: " << message << '\n';
    }
};
constexpr std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
    "\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B\x1b[12;90Huvw\x1b[19;1HLOWEND";
// Recorded original write/query and its literal reply. The original viewport
// scrolled by 5; the core owner has no viewport scroll layer, so the same
// protocol row/column is replayed directly.
constexpr std::string_view write_and_query = "\x1b[7;2HDQ\x1b[6n";
std::vector<std::string> source_lines(const TextDocument& document) {
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
    while (!result.empty() && result.back().empty()) result.pop_back();
    return result;
}
std::string line_text(const Line& line) {
    std::string text;
    for (const auto& cell : line) text += cell.text;
    return text;
}
std::string take_reply(Parser& parser) {
    const auto replies = parser.take_replies();
    return replies.empty() ? std::string{} : replies.front();
}
} // namespace

int main() {
    try {
        Checks checks;
        const std::string clipped = " DQ" + std::string(58, ' ') + "w";
        Parser parser(20, 91, 1000);
        parser.feed(source);
        parser.feed("\x1b[6n");
        checks.expect(take_reply(parser) == "\x1b[19;7R", "initial DSR differs from the recorded 19;7");
        const auto initial = source_lines(TextDocument(parser.screen));
        checks.expect(initial.size() == 18 && initial[11] == std::string(89, ' ') + "uvw",
                      "initial retained body differs from the fixed 92-character row");

        // The recorded single dual-axis window action. The original run has no
        // pre-write body endpoint: its only post-resize body row includes the
        // fixed CUP+write. The retained pre-write row is therefore a TermForge
        // engineering invariant, not a verified original difference.
        parser.screen.resize_preserving(14, 61);
        const auto retained = source_lines(TextDocument(parser.screen));
        checks.expect(retained.size() == 18 && retained[11] == std::string(89, ' ') + "uvw",
                      "dual-axis resize lost the retained source row before the fixed write");

        parser.feed(write_and_query);
        checks.expect(take_reply(parser) == "\x1b[7;4R", "post-shrink DSR differs from the recorded 7;4");
        checks.expect(parser.screen.row == 6 && parser.screen.column == 3,
                      "core protocol cursor does not match the recorded 1-based 7;4");
        const auto shrunk = source_lines(TextDocument(parser.screen));
        checks.expect(shrunk.size() == 18, "post-write document row count differs from the recording");
        checks.expect(shrunk[11] == clipped,
                      "edited row differs from the recorded 62-character clipped row");
        checks.expect(line_text(parser.screen.visible_lines()[6]).find("DQ") != std::string::npos,
                      "fixed write is not addressable on retained row 11");

        // The recorded second dual-axis window action; the written row stays at
        // 62 characters and the literal DSR stays 7;4.
        parser.screen.resize_preserving(20, 91);
        parser.feed("\x1b[6n");
        checks.expect(take_reply(parser) == "\x1b[7;4R", "post-restore DSR differs from the recorded 7;4");
        const auto restored = source_lines(TextDocument(parser.screen));
        checks.expect(restored == shrunk, "width restore rewrote the recorded written row");
        checks.expect(restored[11] == clipped, "restored document row differs from the recording");
        parser.finish();
        if (checks.failures) throw std::runtime_error("dual-axis literal replay failures");
        std::cout << "dual-axis literal replay passed; " << checks.assertions
                  << " assertions against the fixed 2026-11-04 observation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
