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
    std::puts("edit csi contracts passed");
    return 0;
}
