// Bounded-slice literal oracle for #29 resize edges: extreme 1xN / Nx1 resize
// combined with DECAWM soft wrap and CR/LF hard wrap. Contract baseline is the
// existing core + ADR-0005 (docs/adr/0005-physical-row-resize.md): physical-row
// crop (no reflow), leftmost cells retained, growth blanks and never pulls
// history, wrap_pending/combining attachment cleared on a real size change and
// preserved on a no-op, scrolling margins reset on resize, and stable, unique,
// monotonically allocated line identities that are never reused after eviction.
// Expectations are authored literally from ADR-0005 wording; none are generated
// by this parser. Only strict mode is spoken here; recovery edges stay out of
// this slice's oracle.
#include "screen.hpp"
#include "terminal.hpp"
#include "text_document.hpp"
#include "text_projection.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string line_text(const Line& line) {
    std::string value;
    for (const auto& cell : line) value += cell.text;
    return value;
}

// Shared structural invariants: cursor bounds, per-row width, wide-cell
// pairing, history bound, and (while the order is self-reported known) the
// retained sequence holds unique, strictly increasing identities.
static void invariant(const Screen& s) {
    check(s.row >= 0 && s.row < s.height() && s.column >= 0 && s.column < s.width(), "cursor bounds");
    uint64_t previous = 0;
    bool first = true;
    size_t count = 0;
    const auto rows = [&](const auto& collection) {
        for (const auto& line : collection) {
            check(int(line.size()) == s.width(), "row width");
            for (int c = 0; c < s.width(); ++c) {
                if (line[c].width == 0) check(c > 0 && line[c - 1].width == 2, "orphan trail");
                if (line[c].width == 2) check(c + 1 < s.width() && line[c + 1].width == 0, "orphan lead");
            }
            check(line.id != 0, "identity zero leaked");
            if (s.order_known()) {
                if (!first) check(line.id > previous, "retained ids not strictly increasing");
                previous = line.id;
                first = false;
            }
            ++count;
        }
    };
    rows(s.history_lines());
    rows(s.visible_lines());
    check(s.history_lines().size() <= s.history_limit, "history bound");
    check(count == s.retained_size(), "retained size mismatch");
}

// Canonical state signature for cross-chunk equality (not a contract oracle).
static std::string signature(const Screen& s) {
    std::ostringstream out;
    out << s.row << ',' << s.column << ',' << (s.wrap_pending ? 1 : 0) << ',' << s.discarded << ';';
    const auto emit = [&](const Line& line) {
        out << line.id << ':' << line.version << ':' << static_cast<int>(line.ending) << ':' << line.next_id
            << ':' << line.wrap_columns << ':' << (line.cropped ? 1 : 0) << '|' << line_text(line) << ';';
    };
    for (const auto& line : s.history_lines()) emit(line);
    out << '#';
    for (const auto& line : s.visible_lines()) emit(line);
    return out.str();
}

// ADR-0005 L13: an unchanged size is a strict no-op that keeps wrap_pending and
// the pending combining attachment alive.
static void no_op_size_preserves_pending_state() {
    {
        Parser p(2, 4, 2);
        p.feed("a\xcc\x81"); // base 'a' + combining acute -> marks 1, pending base (0,0)
        check(p.screen.visible_lines()[0][0].marks == 1, "fixture did not attach the first mark");
        p.screen.resize(2, 4); // same size: strict no-op
        p.feed("\xcc\x81"); // attachment survived: second mark appends to the base
        check(p.screen.visible_lines()[0][0].marks == 2, "no-op resize broke the pending combining base");
    }
    {
        Parser w(2, 2);
        w.feed("ab"); // both columns filled, delayed wrap armed
        check(w.screen.wrap_pending, "fixture did not arm delayed wrap");
        w.screen.resize(2, 2); // same size: strict no-op
        check(w.screen.wrap_pending, "no-op resize cleared delayed wrap");
        w.feed("X"); // delayed wrap still armed: X must wrap onto the next row
        check(w.screen.row == 1 && w.screen.column == 1 && w.screen.visible_lines()[1][0].text == "X",
              "no-op resize changed the delayed-wrap behaviour");
    }
}

static std::string one_column_crop_signature(const std::string& source, int style) {
    Parser p(3, 4, 2);
    if (style == 0) p.feed(source);
    else if (style == 1) for (const auto byte : source) p.feed(std::string_view(&byte, 1));
    else { p.feed(std::string_view(source).substr(0, 3)); p.feed(std::string_view(source).substr(3)); }
    p.screen.resize(3, 1);
    return signature(p.screen);
}

// ADR-0005 L14: shrinking width keeps the leftmost cell, drops the rest, and a
// later widening never restores it. Soft-wrap metadata survives the crop but the
// continuation link is severed by the crop flag (no reflow).
static void one_column_soft_wrap_crop() {
    const std::string source = "abcdef";
    const auto single = one_column_crop_signature(source, 0);
    check(one_column_crop_signature(source, 1) == single, "byte-wise differs from single-shot");
    check(one_column_crop_signature(source, 2) == single, "split differs from single-shot");
    Parser p(3, 4, 2);
    p.feed(source);
    const auto id0 = p.screen.visible_lines()[0].id, id1 = p.screen.visible_lines()[1].id;
    p.screen.resize(3, 1);
    invariant(p.screen);
    check(line_text(p.screen.visible_lines()[0]) == "a" && line_text(p.screen.visible_lines()[1]) == "e" &&
          line_text(p.screen.visible_lines()[2]) == " ", "one-column crop kept leftmost cells");
    check(p.screen.visible_lines()[0].id == id0 && p.screen.visible_lines()[1].id == id1 &&
          p.screen.visible_lines()[2].id > id1, "one-column crop unstable ids");
    const auto& a = p.screen.visible_lines()[0];
    check(a.ending == LineEnding::soft_wrap && a.next_id == id1 && a.wrap_columns == 4 && a.cropped,
          "soft-wrap provenance shrunk with the row");
    check(!continues_into(a, p.screen.visible_lines()[1]), "cropped soft wrap must not rejoin");
    check(p.screen.row == 1 && p.screen.column == 0, "one-column cursor clamp");
    check(!p.screen.wrap_pending && p.screen.history_lines().empty() && p.screen.discarded == 0,
          "one-column crop of history/shifts");
    bool rejected = false;
    try { p.feed("\xe4\xb8\xad"); } catch (const InputError&) { rejected = true; }
    check(rejected, "one-column strict wide scalar must be rejected");
}

// ADR-0005 L15: shortening to one row drops discardable rows below the cursor
// without counting them as history eviction. The dropped soft-successor identity
// stays spent; growth after the crop mints fresh ids and never resurrects it.
static void one_row_hard_wrap_and_spent_identities() {
    Parser p(3, 4, 2);
    p.feed("abcdefgh"); // row0 "abcd" soft-wraps to row1 "efgh"
    p.feed("\x1b[1;1H"); // park the cursor on row0 before the height crop
    const auto id0 = p.screen.visible_lines()[0].id;
    const auto id1 = p.screen.visible_lines()[1].id;
    check(p.screen.visible_lines()[0].ending == LineEnding::soft_wrap &&
          p.screen.visible_lines()[0].next_id == id1, "fixture soft successor missing");
    p.screen.resize(1, 4);
    invariant(p.screen);
    check(line_text(p.screen.visible_lines()[0]) == "abcd" && p.screen.visible_lines()[0].id == id0,
          "one-row height crop dropped the cursor row");
    check(p.screen.visible_lines()[0].ending == LineEnding::soft_wrap &&
          p.screen.visible_lines()[0].next_id == id1 && p.screen.visible_lines()[0].cropped == false,
          "height-only crop must keep the full-width soft-wrap row uncropped");
    check(!p.screen.locate(id1), "dropped soft successor identity was resurrected");
    check(p.screen.history_lines().empty() && p.screen.discarded == 0,
          "bottom-cropped rows must not count as history eviction");
    p.screen.resize(3, 4);
    invariant(p.screen);
    check(p.screen.visible_lines()[0].id == id0 && line_text(p.screen.visible_lines()[0]) == "abcd",
          "grow-back replaced a surviving identity");
    check(p.screen.visible_lines()[1].id > id1 && p.screen.visible_lines()[2].id > p.screen.visible_lines()[1].id,
          "growth reused a spent bottom identity");
    check(!p.screen.locate(id1), "spent identity returned after growth");
}

// ADR-0005 L14/L15/L16: 80x24 -> 1x1 -> 80x24 keeps only the single leftmost
// cursor-row cell, retains the top rows as 1-column history, and growth adds
// fresh blank rows without pulling history back or restoring cropped text.
static void eighty_by_twenty_four_roundtrip() {
    Parser p(24, 80, 100);
    std::string fill;
    for (int i = 0; i < 24; ++i) {
        fill.push_back(char('A' + i));
        fill.push_back(char('a' + i));
        if (i + 1 < 24) { fill.push_back('\r'); fill.push_back('\n'); }
    }
    p.feed(fill);
    const auto epoch = p.screen.buffer_epoch();
    p.screen.resize(1, 1);
    check(p.screen.height() == 1 && p.screen.width() == 1, "1x1 geometry");
    check(line_text(p.screen.visible_lines()[0]) == "X" && p.screen.row == 0 && p.screen.column == 0,
          "1x1 keeps the cursor-row leftmost cell");
    check(p.screen.history_lines().size() == 23 && p.screen.discarded == 0, "1x1 top-row history");
    check(line_text(p.screen.history_lines()[0]) == "A" && line_text(p.screen.history_lines()[22]) == "W",
          "1x1 history kept each row's leftmost cell");
    p.screen.resize(24, 80);
    invariant(p.screen);
    check(p.screen.visible_lines()[0][0].text == "X" && p.screen.visible_lines()[0][1].text == " ",
          "roundtrip restored the cropped second cell or lost the cursor row");
    check(p.screen.visible_lines()[1][0].text == " " && p.screen.visible_lines()[1].cropped == false,
          "growth pulled history back into the viewport or marked fresh rows cropped");
    check(line_text(p.screen.history_lines()[0]) == std::string("A") + std::string(79, ' '),
          "history widening restored cropped columns");
    check(p.screen.history_lines().size() == 23, "roundtrip changed history membership");
    check(p.screen.buffer_epoch() == epoch, "physical resize changed the buffer epoch");
    invariant(p.screen);
}

// Continuous alternating resize (60x12 <-> 100x30) with the cursor anchored at
// the top row: the top identity survives every transition, fresh rows mint fresh
// ids, and no structural invariant ever breaks.
static void alternating_resize_many_times() {
    Parser p(12, 60, 50);
    p.feed("TOP"); // top row carries a stable literal marker
    const auto anchor = p.screen.visible_lines()[0].id;
    for (int i = 0; i < 20; ++i) {
        p.screen.resize(30, 100);
        invariant(p.screen);
        check(p.screen.visible_lines()[0].id == anchor && p.screen.visible_lines()[0][0].text == "T" &&
              p.screen.visible_lines()[0][1].text == "O" && p.screen.visible_lines()[0][2].text == "P",
              "grow transition displaced the top identity");
        p.screen.resize(12, 60);
        invariant(p.screen);
        check(p.screen.visible_lines()[0].id == anchor && p.screen.visible_lines()[0][0].text == "T" &&
              p.screen.visible_lines()[0][1].text == "O" && p.screen.visible_lines()[0][2].text == "P",
              "shrink transition displaced the top identity");
    }
    check(p.screen.visible_lines()[0].id == anchor, "final anchor identity drifted");
}

// Resize clears a delayed wrap and resets DECSTBM margins to the full screen
// while the terminal-wide origin bit stays set. The DSR reply changes with the
// reset region, which is the literal crossing of pending-wrap x origin/margin.
static void resize_crosses_origin_margin_and_delayed_wrap() {
    Parser p(5, 5, 2);
    p.feed("\x1b[2;4r"); // DECSTBM rows 1..3 (0-based)
    p.feed("\x1b[?6h"); // DECOM origin on
    p.feed("\x1b[1;5H"); // home to region top (row1), last column
    p.feed("Z"); // delayed wrap armed at the margin edge
    check(p.screen.origin_mode() && p.screen.region_top() == 1 && p.screen.region_bottom() == 3 &&
          p.screen.wrap_pending && p.screen.row == 1 && p.screen.column == 4,
          "margin/origin/wrap fixture mis-set");
    p.feed("\x1b[6n");
    check(p.take_replies() == std::vector<std::string>{"\x1b[1;5R"}, "pre-resize origin reply wrong");
    p.screen.resize(3, 8);
    check(p.screen.origin_mode(), "resize cleared the terminal-wide origin bit");
    check(p.screen.region_top() == 0 && p.screen.region_bottom() == 2, "resize did not reset scrolling margins");
    check(!p.screen.wrap_pending, "resize kept a stale delayed wrap");
    check(p.screen.row == 1 && p.screen.column == 4, "resize mis-clamped the cursor");
    p.feed("\x1b[6n");
    check(p.take_replies() == std::vector<std::string>{"\x1b[2;5R"}, "post-resize origin reply wrong");
    p.feed("Q"); // delayed wrap cleared: Q overwrites the last-column cell in place
    check(p.screen.row == 1 && p.screen.column == 5 && p.screen.visible_lines()[1][4].text == "Q",
          "cleared delayed wrap still wrapped the next printable");
    invariant(p.screen);
}

// ADR-0005 L18: invalid sizes leave the extreme state untouched.
static void invalid_size_is_atomic_at_the_edge() {
    Parser p(24, 80, 100);
    p.screen.resize(1, 1);
    const auto before = signature(p.screen);
    for (const auto size : {std::pair<int, int>{0, 1}, {201, 1}, {1, 0}, {1, 501}}) {
        bool invalid = false;
        try { p.screen.resize(size.first, size.second); } catch (const std::invalid_argument&) { invalid = true; }
        check(invalid && signature(p.screen) == before, "invalid resize changed the extreme state");
    }
}

int main() {
    int failures = 0, checks = 0;
    const auto run = [&](const std::string& name, const auto& action) {
        ++checks;
        try { action(); std::cout << name << " passed\n"; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << name << " failed: " << error.what() << '\n';
        }
    };
    run("no-op size keeps pending state", no_op_size_preserves_pending_state);
    run("one-column soft wrap crop", one_column_soft_wrap_crop);
    run("one-row hard wrap and spent identities", one_row_hard_wrap_and_spent_identities);
    run("80x24 to 1x1 and back", eighty_by_twenty_four_roundtrip);
    run("alternating 60x12 to 100x30", alternating_resize_many_times);
    run("resize crosses origin/margin/delayed wrap", resize_crosses_origin_margin_and_delayed_wrap);
    run("invalid size at the edge", invalid_size_is_atomic_at_the_edge);
    std::cout << "resize edges checks: " << (checks - failures) << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
