// Independent expected-content checks for the documented physical-row policy.
#include "terminal.hpp"
#include "text_selection.hpp"
#include <iostream>
#include <sstream>
using termforge::Parser;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string text(const termforge::Line& line) {
    std::string value; for (const auto& c : line) value += c.text; return value;
}
static void invariant(const termforge::Screen& s) {
    check(s.row >= 0 && s.row < s.height() && s.column >= 0 && s.column < s.width(), "cursor bounds");
    const auto lines = [&](const auto& rows) {
        for (const auto& line : rows) {
            check(int(line.size()) == s.width(), "row width");
            for (int c = 0; c < s.width(); ++c) {
                if (line[c].width == 0) check(c > 0 && line[c-1].width == 2, "orphan trail");
                if (line[c].width == 2) check(c+1 < s.width() && line[c+1].width == 0, "orphan lead");
            }
        }
    };
    lines(s.visible_lines()); lines(s.history_lines());
    check(s.history_lines().size() <= s.history_limit, "history bound");
}
static std::string first_two_rows(termforge::Parser& parser, size_t end_column) {
    termforge::TextDocument document(parser.screen);
    const auto begin = document.position(0, 0), end = document.position(1, end_column);
    check(begin && end, "resize selection fixture positions exist");
    termforge::TextSelection selection;
    check(selection.set(document, {*begin, *end}), "resize selection fixture has valid positions");
    const auto value = selection.copy(document, 128);
    check(value.status == termforge::SelectionCopyStatus::ok, "resize selection fixture copies actual text");
    return value.text;
}
static void complete_erasure_removes_only_obsolete_crop_provenance() {
    // Independently authored bytes: a new four-cell row really wraps into xy.
    // No runtime transport is used to generate this literal expected result.
    for (const auto clear : {"\x1b[2J\x1b[H", "\x1b[1;1H\x1b[2K",
                             "\x1b[1;1H\x1b[K", "\x1b[1;4H\x1b[1K\x1b[1;1H"}) {
        Parser fresh(3, 6, 0); fresh.feed("OLDxyz"); fresh.screen.resize(3, 4);
        fresh.feed(clear); fresh.feed("ABCDxy");
        check(first_two_rows(fresh, 2) == "ABCDxy", "full row erasure must restore subsequent real soft-wrap copy");
    }
    for (const auto partial : {"\x1b[1;2H\x1b[K", "\x1b[1;2H\x1b[1K"}) {
        Parser remains(3, 6, 0); remains.feed("OLDxyz"); remains.screen.resize(3, 4);
        remains.feed(partial); remains.feed("\x1b[1;1HABCDxy");
        check(first_two_rows(remains, 2) == "ABCD\r\nxy", "partial row erasure must keep crop provenance conservative");
    }
    Parser cropped(3, 6, 0); cropped.feed("ABCDEFgh"); cropped.screen.resize(3, 4);
    check(first_two_rows(cropped, 2) == "ABCD\r\ngh", "retained clipped content must not silently rejoin");
    Parser hard(3, 6, 0); hard.screen.resize(3, 4); hard.feed("\x1b[2J\x1b[HABCD\r\nxy");
    check(first_two_rows(hard, 2) == "ABCD\r\nxy", "full erasure must not infer a soft wrap from full columns");
}
int main() {
    try {
        complete_erasure_removes_only_obsolete_crop_provenance();
        Parser p(3, 4, 2); p.feed("abcdEF\r\nGH");
        p.screen.resize(3, 3);
        check(text(p.screen.visible_lines()[0]) == "abc" && text(p.screen.visible_lines()[1]) == "EF " &&
              text(p.screen.visible_lines()[2]) == "GH ", "soft and hard rows do not reflow");
        p.screen.resize(4, 6);
        check(text(p.screen.visible_lines()[0]) == "abc   " && text(p.screen.visible_lines()[3]) == "      ", "growth blanks, no restore");
        p.screen.resize(1, 6);
        check(text(p.screen.visible_lines()[0]) == "GH    " && p.screen.row == 0 && p.screen.history_lines().size() == 2, "retain cursor via top history");
        p.feed("\r\nnext"); p.screen.resize(2, 2);
        check(p.screen.discarded == 1 && text(p.screen.history_lines()[0]) == "EF" && text(p.screen.history_lines()[1]) == "GH", "history eviction and width");
        check(text(p.screen.visible_lines()[1]) == "  ", "growth never pulls history");
        Parser top(3, 4, 0); top.feed("AB\r\nCD\r\nEF\x1b[1;2H"); top.screen.resize(1, 4);
        check(text(top.screen.visible_lines()[0]) == "AB  " && top.screen.column == 1 && top.screen.discarded == 0, "drop bottom without scroll history");
        Parser zero(3, 2, 0); zero.feed("A\r\nB\r\nC"); zero.screen.resize(1, 2);
        check(zero.screen.discarded == 2 && text(zero.screen.visible_lines()[0]) == "C ", "zero history");
        Parser wide(2, 4, 2); wide.feed("A\xe4\xb8\xad\xcc\x81"); wide.screen.resize(2, 2);
        check(text(wide.screen.visible_lines()[0]) == "A " && wide.screen.visible_lines()[0][1].marks == 0, "clip wide including mark");
        wide.screen.resize(2, 1); invariant(wide.screen);
        Parser historical(1, 4, 2); historical.feed("A\xe4\xb8\xad\r\nZ"); historical.screen.resize(1, 2);
        check(text(historical.screen.history_lines()[0]) == "A ", "wide history truncation"); invariant(historical.screen);
        Parser narrow(2, 3, 0, termforge::ParseMode::recovery); narrow.screen.resize(2, 1);
        narrow.feed("\xe4\xb8\xad"); narrow.finish(); invariant(narrow.screen);
        check(narrow.diagnostics().replaced_scalars == 1, "one-column wide recovery");
        Parser keep(2, 4, 2); keep.feed("\x1b[31m\xe4\xb8\xad\xcc\x81"); keep.screen.resize(3, 5);
        check(keep.screen.visible_lines()[0][0].width == 2 && keep.screen.visible_lines()[0][0].marks == 1 &&
              keep.screen.visible_lines()[0][0].style.fg == 1 && keep.screen.visible_lines()[2][0].style.plain(), "preserve style and marks, default blanks");
        Parser wrap(2, 2); wrap.feed("ab"); wrap.screen.resize(2, 2);
        check(wrap.screen.wrap_pending, "same size is no-op"); wrap.screen.resize(2, 3); wrap.feed("X");
        check(text(wrap.screen.visible_lines()[0]) == "aX " && !wrap.screen.wrap_pending, "clear delayed wrap on actual resize");
        Parser split(2, 4); split.feed("\xe4"); split.screen.resize(3, 5); split.feed("\xb8\xad\x1b[2;");
        split.screen.resize(4, 6); split.feed("3HZ"); split.finish();
        check(text(split.screen.visible_lines()[1]) == "  Z   ", "preserve partial UTF8 and CSI parser state");
        Parser attach(2, 4); attach.feed("a"); attach.screen.resize(2, 5);
        bool rejected = false; try { attach.feed("\xcc\x81"); } catch (const termforge::InputError&) { rejected = true; }
        check(rejected, "resize clears combining attachment");
        std::ostringstream before; keep.screen.json(before);
        for (auto size : {std::pair<int,int>{0,1}, {201,1}, {1,0}, {1,501}}) {
            bool invalid = false; try { keep.screen.resize(size.first, size.second); } catch (const std::invalid_argument&) { invalid = true; }
            std::ostringstream after; keep.screen.json(after); check(invalid && before.str() == after.str(), "invalid resize atomicity");
        }
        for (int i = 1; i <= 300; ++i) {
            keep.screen.resize(1 + i % 8, 1 + i % 11); invariant(keep.screen);
            keep.feed("x\r\n"); invariant(keep.screen);
        }
        std::cout << "resize content, cursor, history, Unicode and stream boundaries passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
