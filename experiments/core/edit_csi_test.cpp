#include "terminal.hpp"
#include <cstdio>
#include <sstream>
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
    try {
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

    // IL/DL respect the margins: rows outside the region never move.
    Parser margin(4, 10, 10, termforge::ParseMode::recovery);
    margin.feed("a\r\nb\r\nc\r\nd");
    margin.feed("\x1b[2;3r");
    margin.feed("\x1b[2;1H");
    margin.feed("\x1b[L");
    require(row_text(margin.screen, 0) == "a" && row_text(margin.screen, 1).empty() &&
            row_text(margin.screen, 2) == "b" && row_text(margin.screen, 3) == "d",
            "IL ignored the scroll margins");
    margin.feed("\x1b[M");
    require(row_text(margin.screen, 0) == "a" && row_text(margin.screen, 1) == "b" &&
            row_text(margin.screen, 2).empty() && row_text(margin.screen, 3) == "d",
            "DL ignored the scroll margins");

    // Margins belong to the buffer: a 1049 switch keeps each buffer's region.
    {
        Parser buffers(4, 10, 10, termforge::ParseMode::recovery);
        buffers.feed("\x1b[2;3r");
        require(buffers.screen.region_top() == 1 && buffers.screen.region_bottom() == 2,
                "primary region was not set");
        buffers.feed("\x1b[?1049h");
        require(buffers.screen.region_top() == 0 && buffers.screen.region_bottom() == 3,
                "alternate buffer inherited the primary region");
        buffers.feed("\x1b[1;2r");
        require(buffers.screen.region_top() == 0 && buffers.screen.region_bottom() == 1,
                "alternate region was not set");
        buffers.feed("\x1b[?1049l");
        require(buffers.screen.region_top() == 1 && buffers.screen.region_bottom() == 2,
                "primary region was lost across the switch");
    }
    // A resize drops the margins: scrolling afterwards reaches history again.
    {
        Parser resized(4, 10, 10, termforge::ParseMode::recovery);
        resized.feed("a\r\nb\r\nc\r\nd");
        resized.feed("\x1b[2;3r");
        require(resized.screen.region_bottom() == 2, "test setup lost the region");
        resized.screen.resize(4, 12);
        require(resized.screen.region_bottom() == 3, "resize kept the stale region bottom");
        resized.feed("\x1b[4;1H");
        resized.feed("\n");
        require(resized.screen.scrollback_lines() > 0,
                "full-screen scrolling after resize did not reach history");
    }
    // ED3 empties the scrollback without touching the visible rows.
    {
        Parser scrollback(2, 6, 10, termforge::ParseMode::recovery);
        scrollback.feed("a\r\nb\r\nc");
        require(scrollback.screen.scrollback_lines() > 0, "no scrollback was produced");
        scrollback.feed("\x1b[3J");
        require(scrollback.screen.scrollback_lines() == 0, "ED3 left scrollback behind");
        require(row_text(scrollback.screen, 0) == "b" && row_text(scrollback.screen, 1) == "c",
                "ED3 changed the visible screen");
    }
    // RIS returns attributes, cursor and modes to their start state.
    {
        Parser reset(2, 6, 10, termforge::ParseMode::recovery);
        reset.feed("\x1b[31;1mX\x1b[?25l\x1b[?1h");
        require(!reset.screen.visible_lines()[0][0].style.plain(), "test setup lost the style");
        reset.feed("\x1b" "c");
        require(reset.screen.row == 0 && reset.screen.column == 0, "RIS did not home the cursor");
        require(reset.cursor_visible() && !reset.application_cursor_keys(), "RIS did not reset the modes");
        require(row_text(reset.screen, 0).empty(), "RIS did not clear the screen");
        reset.feed("Y");
        require(reset.screen.visible_lines()[0][0].style.plain() &&
                reset.screen.visible_lines()[0][0].style.fg == -1, "RIS did not reset attributes");
    }
    // Editing cells cancels a pending delayed wrap, so the next glyph stays on
    // the current row instead of starting a new one.
    {
        Parser wrap(2, 5, 10, termforge::ParseMode::recovery);
        wrap.feed("abcde");
        require(wrap.screen.wrap_pending, "test setup did not arm the delayed wrap");
        wrap.feed("\x1b[P");
        require(!wrap.screen.wrap_pending, "DCH left the delayed wrap armed");
        wrap.feed("Z");
        require(row_text(wrap.screen, 0) == "abcdZ" && row_text(wrap.screen, 1).empty(),
                "text after a cancelled wrap did not stay on the row");

        Parser insert(2, 5, 10, termforge::ParseMode::recovery);
        insert.feed("abcde");
        require(insert.screen.wrap_pending, "test setup did not arm the delayed wrap");
        insert.feed("\x1b[@");
        require(!insert.screen.wrap_pending, "ICH left the delayed wrap armed");
        insert.feed("Q");
        require(row_text(insert.screen, 0) == "abcdQ" && row_text(insert.screen, 1).empty(),
                "text after a cancelled ICH wrap did not stay on the row");
    }
    // The 175-byte margin source from experiments/shell/margin_checks.py, with
    // its lone line feeds translated to CRLF the way a console does on output.
    // The core must land on the "required" screen, not the ConPTY "observed" one.
    {
        Parser margin(24, 80, 100, termforge::ParseMode::recovery);
        margin.feed("\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r");
        margin.feed("\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\r\nALT_C\r\nALT_D");
        margin.feed("\x1b[?1049l");
        margin.feed("\x1b[3;1H\r\nREGION_C\r\nREGION_D\x1b[6;1HTF_MARGIN_DONE\r\n");
        const std::string expected[] = {"TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "",
                                         "TF_MARGIN_DONE"};
        for (int index = 0; index < 6; ++index)
            require(row_text(margin.screen, index) == expected[index],
                    "margin source produced the ConPTY-observed screen");
        require(margin.screen.row == 6 && margin.screen.column == 0,
                "margin source cursor mismatch");
    }
    // SGR 9/29 toggle strikethrough without touching the other attributes.
    {
        Parser strike(2, 10, 10, termforge::ParseMode::recovery);
        strike.feed("\x1b[9mA\x1b[29mB");
        require(strike.screen.visible_lines()[0][0].style.strikethrough, "SGR 9 was not applied");
        require(!strike.screen.visible_lines()[0][1].style.strikethrough, "SGR 29 did not clear strikethrough");
        std::ostringstream json; strike.screen.visible_lines()[0][1].style.json(json);
        require(json.str() == "{\"fg\":-1,\"bg\":-1,\"bold\":false}",
                "cleared strikethrough leaked into the style JSON");
    }
    // SGR 4/24 and 7/27 toggle underline and reverse independently.
    {
        Parser attrs(2, 10, 10, termforge::ParseMode::recovery);
        attrs.feed("\x1b[4;7mA");
        const auto& first = attrs.screen.visible_lines()[0][0].style;
        require(first.underline && first.reverse, "SGR 4;7 was not applied");
        attrs.feed("\x1b[24mB");
        const auto& second = attrs.screen.visible_lines()[0][1].style;
        require(!second.underline && second.reverse, "SGR 24 did not clear underline alone");
        attrs.feed("\x1b[27mC");
        require(attrs.screen.visible_lines()[0][2].style.plain(), "SGR 27 did not clear reverse");
        // The JSON keeps the old shape while nothing extra is set.
        std::ostringstream plain_json; attrs.screen.visible_lines()[0][2].style.json(plain_json);
        require(plain_json.str() == "{\"fg\":-1,\"bg\":-1,\"bold\":false}",
                "default style JSON shape changed");
    }
    std::puts("edit csi contracts passed");
    return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "edit csi contract failed: %s\n", error.what());
        return 1;
    }
}
