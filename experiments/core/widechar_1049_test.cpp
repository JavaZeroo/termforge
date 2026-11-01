// Bounded-slice literal-oracle CTest for issue #27 wide/combining x 1049 x
// DECAWM-off wrap: wide (CJK double-width) scalars and combining-mark sequences
// crossing the 1049 buffer switch, with the insufficient-space pre-wrap policy,
// lead/trail non-splitting, hide/restore, resize edges and chunk/recovery
// boundaries. Contract baseline is the existing core (screen.hpp put/resize/
// activate_buffer, terminal.hpp DECAWM/1049 dispatch) plus ADR-0005 physical-row
// crop. No new WindTerm run produced these expectations: the original never
// observed wide or combining text across 1049, so every crossing below is a
// composed model check that locks the current implementation rather than an
// independent literal oracle. The unobserved combinations stay recorded as
// unknown (README/validation.json), not asserted as original parity.
#include "screen.hpp"
#include "terminal.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

static const std::string CJK = "\xe4\xb8\xad";            // U+4E2D, width 2
static const std::string COMB_ACUTE = "\xcc\x81";         // U+0301, width 0
static const std::string COMB_DIAERESIS = "\xcc\x88";     // U+0308, width 0
static const std::string UFFFD = "\xef\xbf\xbd";          // replacement scalar

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string line_text(const Line& line) {
    std::string value;
    for (const auto& cell : line) value += cell.text;
    return value;
}

// Structural invariant: every width-2 lead has a width-0 trail immediately
// after it, and no width-0 cell is orphaned. Rows always match the screen width.
static void invariant(const Screen& s) {
    check(s.row >= 0 && s.row < s.height() && s.column >= 0 && s.column < s.width(), "cursor bounds");
    const auto rows = [&](const auto& collection) {
        for (const auto& line : collection) {
            check(int(line.size()) == s.width(), "row width");
            for (int c = 0; c < s.width(); ++c) {
                if (line[c].width == 0) check(c > 0 && line[c - 1].width == 2, "orphan trail");
                if (line[c].width == 2) check(c + 1 < s.width() && line[c + 1].width == 0, "orphan lead");
            }
        }
    };
    rows(s.history_lines());
    rows(s.visible_lines());
}

// Canonical state signature for cross-chunk equality only, never a contract
// oracle. Captures cursor, pending edge, discarded count, per-cell width/marks
// and per-row text plus wrap metadata so a chunking difference cannot hide.
static std::string signature(const Screen& s) {
    std::ostringstream out;
    out << s.row << ',' << s.column << ',' << (s.wrap_pending ? 1 : 0) << ',' << s.discarded << ';';
    const auto emit = [&](const Line& line) {
        out << line.id << ':' << static_cast<int>(line.ending) << ':' << line.next_id << ':'
            << line.wrap_columns << ':' << (line.cropped ? 1 : 0) << '|';
        for (const auto& cell : line) out << cell.width << '/' << cell.marks << ',';
        out << '|' << line_text(line) << ';';
    };
    for (const auto& line : s.history_lines()) emit(line);
    out << '#';
    for (const auto& line : s.visible_lines()) emit(line);
    return out.str();
}

// Wide scalar at the last column has insufficient room, so it pre-wraps to the
// next row as a whole glyph. The permission bit only gates consuming a delayed
// narrow edge, so DECAWM on and off produce the identical pre-wrap result.
static void wide_insufficient_space_prewrap(ParseMode mode) {
    for (const auto& arm : {std::string("\x1b[?7h"), std::string("\x1b[?7l")}) {
        Parser p(3, 4, 2, mode);
        p.feed(arm + "\x1b[1;4H" + CJK);
        check(p.screen.row == 1 && p.screen.column == 2 && !p.screen.wrap_pending,
              "wide last-column prewrap cursor or pending edge");
        const auto& r0 = p.screen.visible_lines()[0];
        check(line_text(r0) == "    " && r0.ending == LineEnding::soft_wrap && r0.wrap_columns == 3u,
              "wide last-column prewrap source-row metadata");
        const auto& r1 = p.screen.visible_lines()[1];
        check(r1[0].width == 2 && r1[1].width == 0 && r1[0].text == CJK && r1[1].text.empty() &&
              line_text(r1) == CJK + "  ", "wide last-column prewrap split the lead/trail");
        invariant(p.screen);
        p.finish();
    }
}

// A wide scalar that fits the last two columns exactly arms the delayed edge
// instead of pre-wrapping. DECAWM on lets the next narrow printable wrap; off
// keeps it on the last column, where it overwrites the trail and blanks the
// lead so no half glyph survives (non-splitting).
static void wide_exact_fit_non_splitting(ParseMode mode) {
    {
        Parser p(3, 4, 2, mode);
        p.feed("\x1b[?7h\x1b[1;3H" + CJK + "X");
        check(p.screen.row == 1 && p.screen.column == 1, "decawm on did not wrap after exact-fit wide char");
        check(p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][3].width == 0 &&
              p.screen.visible_lines()[0].ending == LineEnding::soft_wrap &&
              p.screen.visible_lines()[1][0].text == "X",
              "decawm on split or clobbered the wide glyph");
        invariant(p.screen);
        p.finish();
    }
    {
        Parser p(3, 4, 2, mode);
        p.feed("\x1b[?7l\x1b[1;3H" + CJK + "X");
        check(p.screen.row == 0 && p.screen.column == 3 && p.screen.wrap_pending,
              "decawm off did not stay on the last column after exact-fit wide char");
        check(line_text(p.screen.visible_lines()[0]) == "   X",
              "decawm off did not overwrite the trail and blank the lead");
        check(p.screen.visible_lines()[0][2].width == 1 && p.screen.visible_lines()[0][3].text == "X",
              "decawm off left an orphaned wide half-cell");
        invariant(p.screen);
        p.finish();
    }
}

// 1049 hides the primary wide glyph and its armed edge, then restores both on
// exit with the lead/trail pairing intact.
static void wide_hidden_restored_across_1049(ParseMode mode) {
    Parser p(3, 4, 2, mode);
    p.feed("\x1b[1;3H" + CJK);
    check(p.screen.row == 0 && p.screen.column == 3 && p.screen.wrap_pending &&
          p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][3].width == 0 &&
          p.screen.visible_lines()[0][2].text == CJK && line_text(p.screen.visible_lines()[0]) == "  " + CJK,
          "wide exact-fit fixture not armed with a paired lead/trail");
    p.feed("\x1b[?1049h");
    check(p.screen.active_buffer() == ScreenBuffer::alternate && !p.screen.wrap_pending &&
          p.screen.row == 0 && p.screen.column == 0 && line_text(p.screen.visible_lines()[0]) == "    ",
          "alternate entry kept the primary pending edge or failed to clear the alternate");
    p.feed("\x1b[?1049l");
    check(p.screen.active_buffer() == ScreenBuffer::primary && p.screen.wrap_pending &&
          p.screen.row == 0 && p.screen.column == 3,
          "1049 exit lost the saved primary pending edge");
    check(p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][3].width == 0 &&
          p.screen.visible_lines()[0][2].text == CJK && line_text(p.screen.visible_lines()[0]) == "  " + CJK,
          "1049 roundtrip split the wide lead/trail");
    invariant(p.screen);
    p.finish();
}

// The primary combining cluster (base + marks) survives a 1049 roundtrip and
// keeps accepting further marks on the restored base. The row text must still
// round-trip the original UTF-8 bytes.
static void combining_cluster_survives_1049(ParseMode mode) {
    Parser p(3, 4, 2, mode);
    p.feed("a" + COMB_ACUTE);
    check(p.screen.visible_lines()[0][0].marks == 1 &&
          p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE,
          "fixture did not attach the first combining mark");
    p.feed("\x1b[?1049h");
    check(p.screen.active_buffer() == ScreenBuffer::alternate, "1049 entry failed");
    p.feed("\x1b[?1049l");
    check(p.screen.active_buffer() == ScreenBuffer::primary &&
          p.screen.visible_lines()[0][0].marks == 1 &&
          p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE,
          "1049 roundtrip lost the primary combining cluster");
    p.feed(COMB_DIAERESIS);
    check(p.screen.visible_lines()[0][0].marks == 2 &&
          p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE + COMB_DIAERESIS &&
          line_text(p.screen.visible_lines()[0]) == "a" + COMB_ACUTE + COMB_DIAERESIS + "   ",
          "restored attachment did not extend the original cluster");
    p.finish();
}

// A combining mark after a wide glyph lands on the lead cell and stays there
// across a 1049 roundtrip, so the whole cluster is copied as one unit.
static void combining_after_wide_survives_1049(ParseMode mode) {
    Parser p(3, 4, 2, mode);
    p.feed("\x1b[1;3H" + CJK + COMB_ACUTE);
    check(p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][2].marks == 1 &&
          p.screen.visible_lines()[0][3].width == 0 && p.screen.visible_lines()[0][3].text.empty(),
          "combining mark did not land on the wide lead cell");
    p.feed("\x1b[?1049h\x1b[?1049l" + COMB_DIAERESIS);
    check(p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][2].marks == 2 &&
          p.screen.visible_lines()[0][2].text == CJK + COMB_ACUTE + COMB_DIAERESIS &&
          p.screen.visible_lines()[0][3].width == 0 && p.screen.visible_lines()[0][3].text.empty(),
          "wide cluster did not survive and extend across 1049");
    check(line_text(p.screen.visible_lines()[0]) == "  " + CJK + COMB_ACUTE + COMB_DIAERESIS,
          "wide cluster text did not round-trip");
    invariant(p.screen);
    p.finish();
}

// The primary and alternate buffers keep independent combining attachments;
// the primary cluster survives exit, while a 1049 re-entry clears the alternate
// frame (and with it any pending combining base) per the existing 1049 contract.
static void combining_both_frames_independent(ParseMode mode) {
    Parser p(3, 4, 2, mode);
    p.feed("a" + COMB_ACUTE);
    p.feed("\x1b[?1049h");
    p.feed("b" + COMB_ACUTE);
    check(p.screen.visible_lines()[0][0].text == "b" + COMB_ACUTE &&
          p.screen.visible_lines()[0][0].marks == 1, "alternate combining cluster not formed");
    p.feed("\x1b[?1049l");
    check(p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE &&
          p.screen.visible_lines()[0][0].marks == 1, "exit did not restore the primary cluster");
    p.feed(COMB_DIAERESIS);
    check(p.screen.visible_lines()[0][0].marks == 2, "primary cluster not extended after exit");
    p.feed("\x1b[?1049h");
    check(line_text(p.screen.visible_lines()[0]) == "    ", "1049 re-entry did not clear the alternate frame");
    p.finish();
}

// Strict rejects a combining mark without an adjacent base; recovery replaces it
// with U+FFFD and records the replacement. This also covers the cleared-alternate
// case after a 1049 entry.
static void combining_without_base_handling(ParseMode mode) {
    if (mode == ParseMode::strict) {
        Parser p(3, 4, 2, mode);
        p.feed("\x1b[?1049h");
        bool rejected = false;
        try { p.feed(COMB_ACUTE); }
        catch (const InputError&) { rejected = true; }
        check(rejected && p.screen.row == 0 && p.screen.column == 0,
              "strict combining without adjacent base did not reject atomically");
        p.finish();
    } else {
        Parser p(3, 4, 2, mode);
        p.feed("\x1b[?1049h");
        p.feed(COMB_ACUTE);
        check(p.diagnostics().replaced_scalars == 1 &&
              p.screen.visible_lines()[0][0].text == UFFFD &&
              p.screen.visible_lines()[0][0].width == 1,
              "recovery combining without adjacent base did not replace with U+FFFD");
        p.finish();
    }
}

// Original-text copy consistency: the visible row text must reproduce the exact
// input bytes for a mixed narrow base + marks + space + wide glyph + mark, with
// no duplication of the wide glyph (its trail cell is empty).
static void text_copy_consistency(ParseMode mode) {
    Parser p(2, 5, 2, mode);
    const std::string source = "a" + COMB_ACUTE + COMB_DIAERESIS + " " + CJK + COMB_ACUTE;
    p.feed(source);
    check(line_text(p.screen.visible_lines()[0]) == source + " ",
          "row text did not round-trip the original wide + combining bytes");
    check(p.screen.visible_lines()[0][2].width == 2 && p.screen.visible_lines()[0][2].marks == 1 &&
          p.screen.visible_lines()[0][3].width == 0 && p.screen.visible_lines()[0][3].text.empty(),
          "wide glyph lead/trail or mark placement wrong");
    invariant(p.screen);
    p.finish();
}

// Resize x wide glyph x 1049 boundary: shrinking width to one column blanks an
// orphaned lead (non-splitting, ADR-0005 crop), resizing while the primary is
// hidden resizes the hidden frame too, a one-row crop keeps a row-0 wide glyph
// intact, and a wide scalar in a one-column screen is rejected outright.
static void resize_wide_1049_boundary(ParseMode mode) {
    {
        Parser p(2, 4, 2, mode);
        p.feed(CJK);
        check(p.screen.visible_lines()[0][0].width == 2 && p.screen.visible_lines()[0][1].width == 0,
              "wide fixture lead/trail not paired");
        p.screen.resize(2, 1);
        check(line_text(p.screen.visible_lines()[0]) == " " && p.screen.visible_lines()[0][0].width == 1 &&
              p.screen.visible_lines()[0].cropped, "one-column crop left an orphaned wide lead");
        invariant(p.screen);
        p.finish();
    }
    {
        Parser p(2, 4, 2, mode);
        p.feed(CJK);
        p.feed("\x1b[?1049h");
        p.screen.resize(2, 3);
        p.feed("\x1b[?1049l");
        check(p.screen.visible_lines()[0][0].width == 2 && p.screen.visible_lines()[0][1].width == 0 &&
              p.screen.visible_lines()[0][0].text == CJK && p.screen.visible_lines()[0].cropped,
              "resize while hidden split or lost the primary wide glyph");
        invariant(p.screen);
        p.finish();
    }
    {
        Parser p(2, 4, 2, mode);
        p.feed(CJK);
        p.feed("\x1b[1;1H");
        p.screen.resize(1, 4);
        check(line_text(p.screen.visible_lines()[0]) == CJK + "  " &&
              p.screen.visible_lines()[0][0].width == 2 && p.screen.visible_lines()[0][1].width == 0,
              "one-row crop broke the row-0 wide glyph");
        p.screen.resize(2, 4);
        check(p.screen.visible_lines()[0][0].text == CJK, "grow-back displaced the surviving wide glyph");
        invariant(p.screen);
        p.finish();
    }
    {
        Parser p(2, 1, 2, mode);
        if (mode == ParseMode::strict) {
            bool rejected = false;
            try { p.feed(CJK); }
            catch (const InputError&) { rejected = true; }
            check(rejected, "one-column screen accepted a wide scalar it cannot hold");
        } else {
            p.feed(CJK);
            check(p.diagnostics().replaced_scalars == 1 &&
                  p.screen.visible_lines()[0][0].text == UFFFD && p.screen.visible_lines()[0][0].width == 1,
                  "recovery one-column wide scalar did not replace with U+FFFD");
        }
        p.finish();
    }
}

// A real size change clears the pending combining base; a further mark throws in
// strict. The base text itself is preserved, only the pending attachment dies.
static void real_resize_clears_combining_attachment(ParseMode mode) {
    Parser p(2, 4, 2, mode);
    p.feed("a" + COMB_ACUTE);
    p.screen.resize(2, 3);
    if (mode == ParseMode::strict) {
        bool rejected = false;
        try { p.feed(COMB_DIAERESIS); }
        catch (const InputError&) { rejected = true; }
        check(rejected && p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE &&
              p.screen.visible_lines()[0][0].marks == 1,
              "real resize did not clear the pending combining base but kept the cluster text");
    } else {
        p.feed(COMB_DIAERESIS);
        check(p.diagnostics().replaced_scalars == 1 &&
              p.screen.visible_lines()[0][0].text == "a" + COMB_ACUTE &&
              p.screen.visible_lines()[0][0].marks == 1,
              "recovery real resize did not replace the orphaned combining mark with U+FFFD");
    }
    p.finish();
}

// A fixed wide+combining+1049+DECAWM source must replay identically when fed in
// one shot, byte by byte, and every two-way split (including mid-UTF-8 cuts).
static std::string invariance_source() {
    return "\x1b[?7h\x1b[1;3H" + CJK + COMB_ACUTE + "\x1b[?1049h" + "x" + COMB_ACUTE +
           "\x1b[?1049l" + COMB_DIAERESIS;
}

static void chunk_invariance(ParseMode mode, size_t* cases) {
    const std::string source = invariance_source();
    std::string single;
    {
        Parser p(3, 4, 2, mode);
        p.feed(source);
        p.finish();
        single = signature(p.screen);
    }
    {
        Parser p(3, 4, 2, mode);
        for (const auto byte : source) p.feed(std::string_view(&byte, 1));
        p.finish();
        check(signature(p.screen) == single, "byte-wise state differs from single-shot");
    }
    for (size_t split = 0; split <= source.size(); ++split) {
        Parser p(3, 4, 2, mode);
        p.feed(std::string_view(source).substr(0, split));
        p.feed(std::string_view(source).substr(split));
        p.finish();
        check(signature(p.screen) == single, "two-way split state differs from single-shot");
        ++*cases;
    }
}

// Recovery replaces a malformed UTF-8 byte with U+FFFD without disturbing a
// preceding wide glyph; the diagnostic split (malformed_utf8 vs replaced_scalars)
// stays observable.
static void malformed_utf8_recovery_replaces() {
    Parser p(2, 4, 2, ParseMode::recovery);
    p.feed("a" + CJK);
    p.feed("\xff");
    check(p.diagnostics().malformed_utf8 == 1 && p.diagnostics().replaced_scalars == 0,
          "malformed UTF-8 was not counted as malformed_utf8");
    check(p.screen.visible_lines()[0][0].text == "a" &&
          p.screen.visible_lines()[0][1].width == 2 && p.screen.visible_lines()[0][1].text == CJK &&
          p.screen.visible_lines()[0][3].text == UFFFD,
          "malformed byte replaced the wrong cell or disturbed the wide glyph");
    invariant(p.screen);
    p.finish();
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
    for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
        const std::string name = mode == ParseMode::strict ? "strict" : "recovery";
        run(name + " wide insufficient-space prewrap", [&] { wide_insufficient_space_prewrap(mode); });
        run(name + " wide exact-fit non-splitting", [&] { wide_exact_fit_non_splitting(mode); });
        run(name + " wide hidden/restored across 1049", [&] { wide_hidden_restored_across_1049(mode); });
        run(name + " combining cluster survives 1049", [&] { combining_cluster_survives_1049(mode); });
        run(name + " combining after wide survives 1049", [&] { combining_after_wide_survives_1049(mode); });
        run(name + " combining both frames independent", [&] { combining_both_frames_independent(mode); });
        run(name + " combining without base handling", [&] { combining_without_base_handling(mode); });
        run(name + " text copy consistency", [&] { text_copy_consistency(mode); });
        run(name + " resize wide 1049 boundary", [&] { resize_wide_1049_boundary(mode); });
        run(name + " real resize clears combining attachment", [&] { real_resize_clears_combining_attachment(mode); });
        size_t cases = 0;
        run(name + " chunk invariance", [&] { chunk_invariance(mode, &cases); });
        std::cout << "  " << name << " chunk cases: " << cases << "\n";
    }
    run("recovery malformed UTF-8 replacement", malformed_utf8_recovery_replaces);
    std::cout << "widechar-1049 checks: " << (checks - failures) << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
