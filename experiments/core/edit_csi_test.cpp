#include "terminal.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string row_text(const termforge::Screen& screen, int row) {
    std::string text;
    for (const auto& cell : screen.visible_lines()[static_cast<size_t>(row)]) text += cell.text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}
}  // namespace

int main() {
    using termforge::Parser;
    Parser parser(3, 10, 10, termforge::ParseMode::recovery);
    parser.feed("abcdef");
    require(row_text(parser.screen, 0) == "abcdef", "baseline text mismatch");

    // ICH 2 at column 3 (0-based 2) shifts the tail right and blanks the hole.
    parser.feed("\x1b[1;3H");
    parser.feed("\x1b[2@");
    require(row_text(parser.screen, 0) == "ab  cdef", "ICH did not open a gap");
    require(parser.screen.column == 2, "ICH moved the cursor");

    // DCH 3 from the same column deletes the gap and the following cell.
    parser.feed("\x1b[3P");
    require(row_text(parser.screen, 0) == "abdef", "DCH did not close the gap");

    // The count defaults to one when omitted.
    parser.feed("\x1b[1;1H");
    parser.feed("\x1b[@");
    require(row_text(parser.screen, 0) == " abdef", "default ICH count mismatch");
    parser.feed("\x1b[P");
    require(row_text(parser.screen, 0) == "abdef", "default DCH count mismatch");

    // Inserting past the right edge only shifts what fits.
    parser.feed("\x1b[1;1H");
    parser.feed("\x1b[99@");
    require(row_text(parser.screen, 0).empty(), "over-wide ICH should clear the row");

    // IL opens blank rows at the cursor and lets the bottom rows fall off.
    Parser lines(4, 10, 10, termforge::ParseMode::recovery);
    lines.feed("a\r\nb\r\nc\r\nd");
    lines.feed("\x1b[1;1H");
    lines.feed("\x1b[2L");
    require(row_text(lines.screen, 0).empty() && row_text(lines.screen, 1).empty() &&
            row_text(lines.screen, 2) == "a" && row_text(lines.screen, 3) == "b",
            "IL did not open blank rows");
    require(lines.screen.row == 0 && lines.screen.column == 0, "IL moved the cursor");

    // DL closes rows from the cursor and appends blanks at the bottom.
    lines.feed("\x1b[M");
    require(row_text(lines.screen, 0).empty() && row_text(lines.screen, 1) == "a" &&
            row_text(lines.screen, 2) == "b" && row_text(lines.screen, 3).empty(),
            "DL did not close a row");

    // A count past the bottom only removes what remains.
    lines.feed("\x1b[99L");
    require(row_text(lines.screen, 0).empty() && row_text(lines.screen, 1).empty() &&
            row_text(lines.screen, 2).empty() && row_text(lines.screen, 3).empty(),
            "over-large IL should clear the screen");
    // DECSTBM: rows 2..3 (1-based) scroll on their own; other rows stay put.
    Parser region(4, 10, 10, termforge::ParseMode::recovery);
    region.feed("a\r\nb\r\nc\r\nd");
    region.feed("\x1b[2;3r");
    require(region.screen.row == 0 && region.screen.column == 0, "DECSTBM did not home the cursor");
    // Two feeds only walk to the bottom margin; the third scrolls the region.
    region.feed("\n\n\n");
    require(row_text(region.screen, 0) == "a" && row_text(region.screen, 1) == "c" &&
            row_text(region.screen, 2).empty() && row_text(region.screen, 3) == "d",
            "region scroll changed rows outside the margins");
    require(region.screen.row == 2, "region scroll moved the cursor past the margin");
    std::puts("edit csi contracts passed");
    return 0;
}
