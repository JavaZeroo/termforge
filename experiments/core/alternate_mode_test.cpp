#include "terminal.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include "history_view.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string text(const Line& line) {
    std::string result;
    for (const auto& cell : line) result += cell.text;
    return result;
}
static void enter_and_restore(ParseMode mode) {
    Parser parser(4, 12, 2, mode);
    auto* stable = &parser.screen;
    parser.feed("\x1b[31;44mMAIN\x1b[?1049h");
    check(&parser.screen == stable && parser.screen.active_buffer() == ScreenBuffer::alternate,
          "1049 entry did not activate the same Screen's alternate frame");
    check(parser.screen.row == 0 && parser.screen.column == 0 &&
          parser.screen.style.fg == -1 && parser.screen.style.bg == -1,
          "1049 entry did not home the cursor with default writing colours");
    check(text(parser.screen.visible_lines()[0]) == "            ", "alternate entry retained primary text");
    parser.feed("ALT\x1b[?1049l");
    check(parser.screen.active_buffer() == ScreenBuffer::primary &&
          text(parser.screen.visible_lines()[0]) == "MAIN        " &&
          parser.screen.row == 0 && parser.screen.column == 4,
          "1049 exit did not restore primary text and cursor");
    check(parser.screen.style.fg == 1 && parser.screen.style.bg == 4,
          "1049 exit did not restore primary writing colours");
    parser.finish();
}
static void repeated_entry_and_exit(ParseMode mode) {
    Parser parser(3, 10, 2, mode);
    parser.feed("\x1b[31;44mMAIN\x1b[?1049hALT\x1b[32;45m\x1b[3;4HZ\x1b[?1049h");
    check(parser.screen.row == 0 && parser.screen.column == 0 &&
          parser.screen.style.fg == -1 && parser.screen.style.bg == -1,
          "repeated 1049 entry did not home with default writing colours");
    check(text(parser.screen.visible_lines()[0]) == "          " && text(parser.screen.visible_lines()[2]) == "          ",
          "repeated 1049 entry retained old alternate text");
    parser.feed("NEW\x1b[?1049l!\x1b[?1049l?");
    check(text(parser.screen.visible_lines()[0]) == "MAIN!?    " && parser.screen.column == 6,
          "repeated entry overwrote primary state or repeated exit rewound it");
    check(parser.screen.style.fg == 1 && parser.screen.style.bg == 4,
          "alternate colour writes changed saved primary colours");
    parser.feed("\x1b[?1049h");
    check(text(parser.screen.visible_lines()[0]) == "          " && parser.screen.column == 0,
          "1049 re-entry retained text from the earlier alternate visit");
    parser.feed("\x1b[?1049l");
    check(text(parser.screen.visible_lines()[0]) == "MAIN!?    " && parser.screen.column == 6,
          "later primary writes were not preserved across re-entry");
    parser.finish();
}
static void pending_wrap_roundtrip(ParseMode mode) {
    Parser parser(25, 91, 2, mode);
    parser.feed("\x1b[3;91HW\x1b[?1049hALT\x1b[?1049lX");
    check(parser.screen.row == 3 && parser.screen.column == 1 &&
          text(parser.screen.visible_lines()[2]) == std::string(90, ' ') + "W" &&
          text(parser.screen.visible_lines()[3]) == "X" + std::string(90, ' '),
          "return lost the primary pending wrap before the next printable byte");
    check(parser.screen.visible_lines()[2].ending == LineEnding::soft_wrap &&
          parser.screen.visible_lines()[2].next_id == parser.screen.visible_lines()[3].id,
          "restored pending wrap did not preserve the real continuation");
    parser.finish();
}
// Composed Screen policy, not an additional WindTerm observation: the fixed
// ASCII pending-wrap oracle does not cover wide cells or combining attachment.
// Protocol entry/re-entry must leave the saved primary glyph and attachment
// intact, including when a UTF-8 scalar or private command crosses a feed.
static void unicode_pending_roundtrip(ParseMode mode, std::string_view source, size_t split, bool bytewise) {
    Parser parser(3, 4, 2, mode);
    if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
    else {
        parser.feed(std::string_view(source).substr(0, split));
        parser.feed(std::string_view(source).substr(split));
    }
    parser.finish();
    const auto& screen = parser.screen;
    const auto& first = screen.visible_lines()[0];
    const auto& second = screen.visible_lines()[1];
    check(screen.active_buffer() == ScreenBuffer::primary && screen.row == 1 && screen.column == 1 &&
          text(first) == "AB\xe4\xb8\xad\xcc\x81\xcc\x80" && text(second) == "Z   " &&
          text(screen.visible_lines()[2]) == "    ",
          "1049 Unicode roundtrip changed literal text or consumed pending wrap at a combining mark");
    check(first[2].width == 2 && first[2].marks == 2 && first[3].width == 0 && first[3].text.empty() &&
          first[2].style.fg == 1 && first[2].style.bg == 4 &&
          first[3].style.fg == 1 && first[3].style.bg == 4 &&
          second[0].style.fg == 1 && second[0].style.bg == 4 &&
          first.ending == LineEnding::soft_wrap && first.wrap_columns == 4 && first.next_id == second.id,
          "1049 Unicode roundtrip lost a wide tail, saved colour or actual continuation");
    check(screen.history_lines().empty() && parser.diagnostics().replaced_scalars == 0 &&
          parser.diagnostics().ignored_sequences == 0,
          "supported Unicode roundtrip replaced input, ignored a command or leaked alternate history");
}
static void unicode_roundtrip_chunks(ParseMode mode) {
    const std::string source =
        "\x1b[31;44mAB\xe4\xb8\xad\xcc\x81"
        "\x1b[?1049h\x1b[32;45mXY\xe6\x96\x87\xcc\x88"
        "\x1b[?1049hALT\x1b[?1049l\xcc\x80Z";
    for (size_t split = 0; split <= source.size(); ++split) unicode_pending_roundtrip(mode, source, split, false);
    unicode_pending_roundtrip(mode, source, 0, true);
}
// Full-cell styles and blank-cell attributes remain engineering policy. The
// original colour-output controls do not establish a complete styled grid.
static void styled_grid_roundtrip(ParseMode mode) {
    Parser parser(3, 6, 2, mode);
    parser.feed("\x1b[31;44;1;2;4;7;8;9;53m A\x1b[?1049h");
    for (const auto& line : parser.screen.visible_lines()) for (const auto& cell : line)
        check(cell.text == " " && cell.width == 1 && cell.style.plain(),
              "first alternate frame inherited primary grid attributes");
    // Keep the unclassified bold policy separate from the observed strike reset.
    parser.feed("\x1b[0;32;45;1;9m\x1b[2JZ\x1b[?1049h");
    for (const auto& line : parser.screen.visible_lines()) for (const auto& cell : line)
        check(cell.text == " " && cell.width == 1 && cell.style.fg == -1 && cell.style.bg == -1 &&
              cell.style.bold && !cell.style.dim && !cell.style.underline && !cell.style.reverse &&
              !cell.style.strikethrough && !cell.style.hidden && !cell.style.overline,
              "repeated alternate entry retained styled text or applied stale writing attributes to blanks");
    parser.feed("\x1b[0;32;45mALT\x1b[?1049lB");
    const auto& screen = parser.screen;
    check(text(screen.visible_lines()[0]) == " AB   " && screen.row == 0 && screen.column == 3,
          "styled alternate output changed the saved primary text or insertion point");
    for (int column = 0; column < 3; ++column) {
        const auto& cell = screen.visible_lines()[0][column];
        const auto& style = cell.style;
        check(cell.written && style.fg == 1 && style.bg == 4 && style.bold && style.dim && style.underline &&
              style.reverse && style.strikethrough && style.hidden && style.overline,
              "primary written space/text or restored writing style lost an attribute");
    }
    for (int row = 0; row < 3; ++row) for (int column = row == 0 ? 3 : 0; column < 6; ++column) {
        const auto& cell = screen.visible_lines()[row][column];
        check(cell.text == " " && !cell.written && cell.style.plain(),
              "alternate erase/style leaked into an unwritten primary cell");
    }
    parser.finish();
}
struct ObservedAttribute {
    const char* name;
    const char* sgr;
    bool Style::* flag;
};
// The five literal phases are the public attribute_state_reference.py sources.
// Same-frame H/space controls distinguish these attributes at entry/re-entry
// and return. This does not claim original internal flags or full blank grids.
static std::array<std::string, 5> attribute_sources(const ObservedAttribute& attribute) {
    const std::string active = attribute.sgr;
    return {{
        "\x1b[0m\x1b[2J\x1b[H\x1b[2;3HN0 H H " + active + "S0 H H",
        "\x1b[?1049hE0 H H \x1b[0mEN H H " + active + "ES H H",
        "\x1b[4;3H\x1b[0mA0 H H AN H H " + active + "AS H H",
        "\x1b[?1049hR0 H H \x1b[0mRN H H " + active + "RS H H\x1b[0m",
        "\x1b[?1049l M1 H H \x1b[0mMN H H " + active + "MS H H"
    }};
}
static void observed_attribute_trace(ParseMode mode, const ObservedAttribute& attribute,
                                     size_t split, bool bytewise) {
    Parser parser(25, 91, 2, mode);
    const auto sources = attribute_sources(attribute);
    const std::array<int, 5> rows{{1, 0, 3, 0, 1}}, columns{{15, 20, 22, 20, 36}};
    const std::array<std::string, 5> expected{{
        "  N0 H H S0 H H", "E0 H H EN H H ES H H", "  A0 H H AN H H AS H H",
        "R0 H H RN H H RS H H", "  N0 H H S0 H H M1 H H MN H H MS H H"
    }};
    for (size_t phase = 0; phase < sources.size(); ++phase) {
        const auto& source = sources[phase];
        if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
        else {
            const auto boundary = (std::min)(split, source.size());
            parser.feed(std::string_view(source).substr(0, boundary));
            parser.feed(std::string_view(source).substr(boundary));
        }
        const auto& screen = parser.screen;
        const auto& line = screen.visible_lines()[static_cast<size_t>(rows[phase])];
        check(screen.row == rows[phase] && screen.column == columns[phase] &&
              screen.active_buffer() == (phase == 0 || phase == 4 ? ScreenBuffer::primary : ScreenBuffer::alternate) &&
              text(line) == expected[phase] + std::string(91 - expected[phase].size(), ' ') &&
              screen.history_lines().empty(), "attribute trace changed literal text, cursor, buffer or history");
        const auto pair = [&](int first, bool enabled) {
            for (const int column : {first, first + 2}) {
                const auto& glyph = line[static_cast<size_t>(column)];
                const auto& space = line[static_cast<size_t>(column - 1)];
                check(glyph.text == "H" && space.text == " " && glyph.written && space.written,
                      "attribute trace H/space source coordinates differ");
                if ((glyph.style.*attribute.flag) != enabled || (space.style.*attribute.flag) != enabled)
                    throw std::runtime_error(std::string(attribute.name) + " phase " + std::to_string(phase) +
                                             " H/space attribute differs at column " + std::to_string(column));
            }
        };
        if (phase == 0) { pair(5, false); pair(12, true); }
        else if (phase == 1 || phase == 3) { pair(3, false); pair(10, false); pair(17, true); }
        else if (phase == 2) { pair(5, false); pair(12, false); pair(19, true); }
        else {
            pair(19, true); pair(26, false); pair(33, true);
            pair(5, false); pair(12, true); // Saved primary controls survive alternate writes.
        }
    }
    parser.finish();
    check(parser.diagnostics().ignored_sequences == 0 && parser.diagnostics().malformed_utf8 == 0 &&
          parser.diagnostics().replaced_scalars == 0, "supported attribute source was ignored or replaced");
}
static void observed_attribute_chunks() {
    const std::array<ObservedAttribute, 6> attributes{{
        {"underline", "\x1b[4m", &Style::underline}, {"reverse", "\x1b[7m", &Style::reverse},
        {"strikethrough", "\x1b[9m", &Style::strikethrough}, {"hidden", "\x1b[8m", &Style::hidden},
        {"overline", "\x1b[53m", &Style::overline}, {"dim", "\x1b[2m", &Style::dim}
    }};
    size_t failures = 0;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery}) for (const auto& attribute : attributes) {
        try {
            const auto sources = attribute_sources(attribute);
            size_t maximum = 0;
            for (const auto& source : sources) maximum = (std::max)(maximum, source.size());
            // Zero is whole-phase input; all other boundaries include every
            // two-part split in each phase, with observations between phases.
            for (size_t split = 0; split <= maximum; ++split) observed_attribute_trace(mode, attribute, split, false);
            observed_attribute_trace(mode, attribute, 0, true);
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << (mode == ParseMode::strict ? "strict: " : "recovery: ") << error.what() << '\n';
        }
    }
    check(!failures, "observed attribute trace failed");
}
static void observed_colour_trace(ParseMode mode, size_t split, bool bytewise) {
    const std::string input =
        "\x1b[0m\x1b[2J\x1b[H\x1b[2;3HD0 H H\x1b[4;3H\x1b[31;44mM0 H H"
        "\x1b[?1049hA0 H\x1b[31;44m A0S1 H\x1b[0m A0D0 H"
        "\x1b[4;5H\x1b[32;45mA1 H H"
        "\x1b[?1049hA2 H\x1b[0m A2D0 H\x1b[31;44m A2S1 H\x1b[32;45m A2S2 H"
        "\x1b[?1049l M1 H\x1b[31;44m M1S1 H\x1b[32;45m M1S2 H\x1b[0m M1D0 H";
    Parser parser(25, 91, 2, mode);
    if (bytewise) for (const auto byte : input) parser.feed(std::string_view(&byte, 1));
    else {
        parser.feed(std::string_view(input).substr(0, split));
        parser.feed(std::string_view(input).substr(split));
    }
    parser.finish();
    const auto& screen = parser.screen;
    check(screen.active_buffer() == ScreenBuffer::primary && screen.row == 3 && screen.column == 34,
          "five-stage trace ended in the wrong primary cursor state");
    check(text(screen.visible_lines()[1]) == "  D0 H H" + std::string(83, ' ') &&
          text(screen.visible_lines()[3]) == "  M0 H H M1 H M1S1 H M1S2 H M1D0 H" + std::string(57, ' '),
          "five-stage trace did not preserve the literal primary text");
    check(screen.visible_lines()[3][12].text == "H" && screen.visible_lines()[3][12].style.fg == 1 &&
          screen.visible_lines()[3][12].style.bg == 4 && screen.visible_lines()[3][33].style.fg == -1 &&
          screen.visible_lines()[3][33].style.bg == -1,
          "return output did not use restored main colours and the explicit default control");
}
static void history_and_resize(ParseMode mode) {
    Parser parser(2, 8, 3, mode);
    parser.feed("A\r\nB\r\nC\r\nD\x1b[?1049hX\r\nY\r\nZ\r\nT");
    check(parser.screen.history_lines().empty(), "alternate output entered retained history");
    parser.screen.resize(3, 6);
    parser.feed("\x1b[?1049l");
    const auto& screen = parser.screen;
    check(screen.height() == 3 && screen.width() == 6 && screen.history_lines().size() == 2 &&
          text(screen.history_lines()[0]) == "A     " && text(screen.history_lines()[1]) == "B     " &&
          text(screen.visible_lines()[0]) == "C     " && text(screen.visible_lines()[1]) == "D     " &&
          screen.row == 1 && screen.column == 1,
          "1049 output or resize changed the saved primary history and writing position");
    parser.finish();
}
static void readers_follow_protocol_transitions(ParseMode mode) {
    Parser parser(2, 8, 2, mode);
    parser.feed("ONE\r\nTWO\r\nMAIN");
    const TextDocument document(parser.screen);
    const auto old_position = *document.position(2, 0);
    TextSelection selection;
    check(selection.set(document, {old_position, *document.position(2, 4)}), "selection setup failed");
    TextSearch search;
    const auto generation = search.start(document, "MAIN");
    for (int step = 0; step < 20 && search.status() == SearchStatus::running; ++step)
        search.step(document, generation, 64);
    check(search.match(document, 0).has_value(), "search setup failed");
    HistoryViewport viewport;
    viewport.scroll_to(parser.screen, 0);
    parser.feed("\x1b[?1049hALT");
    check(!document.valid(old_position) && !search.match(document, 0) &&
          selection.copy(document, 1024).status == SelectionCopyStatus::invalidated,
          "a main text reader crossed into alternate output");
    check(selection.set(document, {*document.position(0, 0), *document.position(0, 3)}) &&
          selection.copy(document, 1024).text == "ALT", "fresh alternate selection failed");
    const auto old_alternate = *document.position(0, 0);
    parser.feed("\x1b[?1049hNEW");
    check(!document.valid(old_alternate) &&
          selection.copy(document, 1024).status == SelectionCopyStatus::invalidated,
          "repeated 1049 entry revived an erased alternate reader");
    parser.feed("\x1b[?1049l");
    check(!document.valid(old_position) && !search.match(document, 0),
          "return revived an old primary reader");
    check(viewport.snapshot(parser.screen).viewport.following,
          "a prior historical viewport survived a protocol buffer roundtrip");
    check(selection.set(document, {*document.position(2, 0), *document.position(2, 4)}) &&
          selection.copy(document, 1024).text == "MAIN", "fresh returned primary selection failed");
    parser.finish();
}
static void recovery_keeps_private_sequences_separate() {
    const std::vector<std::string> ignored{
        "\x1b[?31m", "\x1b[?1049;25h", "\x1b[?1049;h", "\x1b[0?1049h",
        "\x1b[??1049h", "\x1b[?1049 h", "\x1b[?1049:1h", "\x1b[?12l",
        std::string("\x1b[?") + std::string(33, '0') + "1049h"};
    for (const auto& sequence : ignored) {
        Parser parser(2, 8, 1, ParseMode::recovery);
        parser.feed(sequence + "\x1b[31mR");
        parser.finish();
        check(parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.visible_lines()[0]) == "R       " && parser.screen.visible_lines()[0][0].style.fg == 1 &&
              parser.diagnostics().ignored_sequences == 1 && parser.diagnostics().max_sequence_bytes <= 33,
              "an unsupported private sequence mutated a buffer or contaminated the next CSI");
    }
    for (const auto cancellation : {char(0x18), char(0x1a), char(0x1b)}) {
        Parser parser(2, 8, 1, ParseMode::recovery);
        parser.feed(std::string("\x1b[?1049") + cancellation + "\x1b[31mR");
        parser.finish();
        check(parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.visible_lines()[0]) == "R       " && parser.screen.visible_lines()[0][0].style.fg == 1,
              "cancellation or ESC resynchronization retained the private prefix");
    }
    Parser truncated(2, 8, 1, ParseMode::recovery);
    truncated.feed("\x1b[?1049"); truncated.finish(); truncated.feed("\x1b[31mR"); truncated.finish();
    check(truncated.screen.active_buffer() == ScreenBuffer::primary && truncated.screen.visible_lines()[0][0].style.fg == 1,
          "truncated private input contaminated the next normal CSI");
}
static void identical_repeated_text_is_a_new_document(ParseMode mode) {
    Parser parser(2, 8, 2, mode);
    parser.feed("\x1b[?1049hALT");
    const TextDocument document(parser.screen);
    TextSelection selection;
    check(selection.set(document, {*document.position(0, 0), *document.position(0, 3)}),
          "alternate selection setup failed");
    parser.feed("\x1b[?1049hALT");
    check(selection.copy(document, 1024).status == SelectionCopyStatus::invalidated,
          "identical text after repeated 1049 entry revived a prior alternate selection");
    parser.finish();
}
static void strict_rejects_unsupported_private_modes_atomically() {
    for (const auto* input : {"\x1b[?31m", "\x1b[?1049;25h", "\x1b[?1049;1049h", "\x1b[?12l"}) {
        Parser parser(2, 8, 1);
        parser.feed("\x1b[31;44mMAIN");
        bool rejected = false;
        try { parser.feed(input); }
        catch (const InputError&) { rejected = true; }
        check(rejected && parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.visible_lines()[0]) == "MAIN    " &&
              parser.screen.style.fg == 1 && parser.screen.style.bg == 4,
              "strict private dispatch accepted an unsupported command or partially applied its modes");
    }
}
int main() {
    try {
        observed_attribute_chunks();
        for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
            enter_and_restore(mode);
            repeated_entry_and_exit(mode);
            pending_wrap_roundtrip(mode);
            unicode_roundtrip_chunks(mode);
            styled_grid_roundtrip(mode);
            history_and_resize(mode);
            readers_follow_protocol_transitions(mode);
            identical_repeated_text_is_a_new_document(mode);
            // The fixed five sources contain 208 bytes, excluding DSR queries.
            for (size_t split = 0; split <= 208; ++split) observed_colour_trace(mode, split, false);
            observed_colour_trace(mode, 0, true);
        }
        recovery_keeps_private_sequences_separate();
        strict_rejects_unsupported_private_modes_atomically();
        std::cout << "alternate mode checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
