// Owner transaction invariants; these are engineering checks, not observations
// of WindTerm's Unicode, inactive-frame or out-of-grid cursor behavior.
#include "terminal.hpp"
#include "text_document.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace termforge;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void same_source(const Line& before, const Line& after) {
    check(before.id == after.id && before.version == after.version &&
          before.ending == after.ending && before.next_id == after.next_id &&
          before.wrap_columns == after.wrap_columns && before.content_columns == after.content_columns &&
          before.cropped == after.cropped,
          "width transaction changed source metadata");
    check(after.size() >= before.size(), "source storage contracted");
    for (size_t i = 0; i < before.size(); ++i) {
        const auto& a = before[i]; const auto& b = after[i];
        check(a.text == b.text && a.width == b.width && a.marks == b.marks &&
              a.written == b.written && a.clipped == b.clipped &&
              a.grapheme_fragment == b.grapheme_fragment && a.leading_spacer == b.leading_spacer,
              "source cell changed");
        check(a.style.fg == b.style.fg && a.style.bg == b.style.bg &&
              a.style.bold == b.style.bold && a.style.dim == b.style.dim &&
              a.style.underline == b.style.underline && a.style.reverse == b.style.reverse &&
              a.style.strikethrough == b.style.strikethrough && a.style.hidden == b.style.hidden &&
              a.style.overline == b.style.overline, "source cell style changed");
    }
    for (size_t i = before.size(); i < after.size(); ++i)
        check(after[i].text == " " && after[i].width == 1 && after[i].style.plain() &&
              !after[i].written && !after[i].marks, "growth added owned content");
}

static void history_and_bounds() {
    Parser parser(2, 8, 5, ParseMode::strict);
    parser.feed("one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\nseven");
    auto& screen = parser.screen;
    check(screen.history_size() == 5, "history fixture did not fill its budget");
    std::vector<Line> source;
    for (size_t i = 0; i < screen.retained_size(); ++i) source.push_back(screen.retained_line(i));
    const auto* history_cells = screen.retained_line(0).data();
    const auto* live_cells = screen.visible_lines()[0].data();
    const auto revision = screen.order_revision(), epoch = screen.buffer_epoch();
    const auto row = screen.row;
    auto column = screen.column;
    for (const int width : {1, 8, 500, 2, 500}) {
        screen.resize_columns_preserving(width);
        column = (std::min)(column, width - 1);
        check(screen.width() == width && screen.height() == 2 && screen.history_size() == 5 &&
              screen.retained_size() == source.size(), "width transaction changed membership");
        check(screen.order_revision() == revision && screen.buffer_epoch() == epoch &&
              screen.row == row && screen.column == column, "width transaction changed protocol identity");
        check(screen.retained_line(0).data() == history_cells && screen.retained_line(0).size() == 8,
              "width transaction copied or padded history");
        if (width == 1 || width == 8)
            check(screen.visible_lines()[0].data() == live_cells, "contraction allocated live storage");
        for (size_t i = 0; i < source.size(); ++i) {
            same_source(source[i], screen.retained_line(i));
            check(screen.retained_line(i).size() <= 500, "fragment exceeded cell bound");
        }
    }
    for (const int width : {-1, 0, 501}) {
        bool rejected = false;
        try { screen.resize_columns_preserving(width); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && screen.width() == 500 && screen.order_revision() == revision &&
              screen.retained_line(0).data() == history_cells, "invalid width partially changed owner");
    }
}

static void cells_and_frames() {
    Parser parser(3, 8, 5, ParseMode::strict);
    parser.feed("\x1b[31;44;1;2;4;7;9;8;53mA \xe7\x95\x8c" "e\xcc\x81");
    auto& screen = parser.screen;
    const Line primary = screen.visible_lines()[0];
    // A width transaction preserves the attachment to the adjacent base.
    screen.resize_columns_preserving(2);
    parser.feed("\xcc\x82");
    check(screen.visible_lines()[0][4].text == "e\xcc\x81\xcc\x82" &&
          screen.visible_lines()[0][4].marks == 2, "width change detached a combining base");
    const Line attached = screen.visible_lines()[0];
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.resize_columns_preserving(8);
    parser.feed("12345678");
    check(screen.wrap_pending, "edge fixture missing pending wrap");
    screen.resize_columns_preserving(8);
    check(screen.wrap_pending && screen.column == 7, "unchanged width edited the pending edge");
    screen.set_scroll_region(0, 1);
    screen.set_origin_mode(true);
    screen.move(1, 7);
    parser.feed("Z");
    const Line alternate = screen.visible_lines()[1];
    const auto epoch = screen.buffer_epoch(), revision = screen.order_revision();
    screen.resize_columns_preserving(1);
    check(screen.row == 1 && screen.column == 0 && !screen.wrap_pending && screen.origin_mode() &&
          screen.region_top() == 0 && screen.region_bottom() == 1 &&
          screen.buffer_epoch() == epoch && screen.order_revision() == revision,
          "width transaction failed to bound the active cursor independently of content");
    screen.resize_columns_preserving(12);
    same_source(alternate, screen.visible_lines()[1]);
    screen.activate_buffer(ScreenBuffer::primary);
    same_source(attached, screen.visible_lines()[0]);
    check(screen.visible_lines()[0].size() == 12 && primary[2].width == 2 && primary[3].width == 0,
          "hidden primary padding or wide fixture differs");
}

static void blank_extent() {
    Parser parser(3, 91, 0, ParseMode::strict);
    parser.feed("\x1b[1;91Hv");
    auto& screen = parser.screen;
    screen.resize_columns_preserving(61);
    parser.feed("\x1b[1;61H\x1b[1@");
    const auto identity = screen.visible_lines()[0].id;
    const auto version = screen.visible_lines()[0].version;
    check(TextDocument(screen).line(0).projection.text == std::string(61, ' '),
          "edited blank extent was trimmed to allocation padding");
    for (const auto& cell : screen.visible_lines()[0])
        check(!cell.written, "editing blanks were mislabeled as explicitly printed spaces");
    screen.resize_columns_preserving(91);
    check(screen.visible_lines()[0].version == version &&
          TextDocument(screen).line(0).projection.text.size() == 61,
          "growth changed the retained blank extent");
    parser.feed("\x1b[1;1HL");
    screen.resize_columns_preserving(20);
    parser.feed("\x1b[1;1HM");
    const auto text = TextDocument(screen).line(0).projection.text;
    check(text == "M" + std::string(19, ' ') && screen.visible_lines()[0].id == identity &&
          screen.visible_lines()[0].version > version,
          "a later edit lost blank extent or retained a deleted tail");
    auto invalid = screen.visible_lines();
    invalid[0].content_columns = invalid[0].size() + 1;
    bool rejected = false;
    try { screen.publish({}, std::move(invalid)); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && TextDocument(screen).line(0).projection.text == text,
          "invalid source extent was published");
}

int main() {
    try {
        history_and_bounds();
        cells_and_frames();
        blank_extent();
        std::cout << "retained-column owner invariants passed (no original-policy claim)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
