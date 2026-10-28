#include "terminal.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include "history_view.hpp"
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
    check(text(parser.screen.lines[0]) == "            ", "alternate entry retained primary text");
    parser.feed("ALT\x1b[?1049l");
    check(parser.screen.active_buffer() == ScreenBuffer::primary &&
          text(parser.screen.lines[0]) == "MAIN        " &&
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
    check(text(parser.screen.lines[0]) == "          " && text(parser.screen.lines[2]) == "          ",
          "repeated 1049 entry retained old alternate text");
    parser.feed("NEW\x1b[?1049l!\x1b[?1049l?");
    check(text(parser.screen.lines[0]) == "MAIN!?    " && parser.screen.column == 6,
          "repeated entry overwrote primary state or repeated exit rewound it");
    check(parser.screen.style.fg == 1 && parser.screen.style.bg == 4,
          "alternate colour writes changed saved primary colours");
    parser.feed("\x1b[?1049h");
    check(text(parser.screen.lines[0]) == "          " && parser.screen.column == 0,
          "1049 re-entry retained text from the earlier alternate visit");
    parser.feed("\x1b[?1049l");
    check(text(parser.screen.lines[0]) == "MAIN!?    " && parser.screen.column == 6,
          "later primary writes were not preserved across re-entry");
    parser.finish();
}
static void pending_wrap_roundtrip(ParseMode mode) {
    Parser parser(25, 91, 2, mode);
    parser.feed("\x1b[3;91HW\x1b[?1049hALT\x1b[?1049lX");
    check(parser.screen.row == 3 && parser.screen.column == 1 &&
          text(parser.screen.lines[2]) == std::string(90, ' ') + "W" &&
          text(parser.screen.lines[3]) == "X" + std::string(90, ' '),
          "return lost the primary pending wrap before the next printable byte");
    check(parser.screen.lines[2].ending == LineEnding::soft_wrap &&
          parser.screen.lines[2].next_id == parser.screen.lines[3].id,
          "restored pending wrap did not preserve the real continuation");
    parser.finish();
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
    check(text(screen.lines[1]) == "  D0 H H" + std::string(83, ' ') &&
          text(screen.lines[3]) == "  M0 H H M1 H M1S1 H M1S2 H M1D0 H" + std::string(57, ' '),
          "five-stage trace did not preserve the literal primary text");
    check(screen.lines[3][12].text == "H" && screen.lines[3][12].style.fg == 1 &&
          screen.lines[3][12].style.bg == 4 && screen.lines[3][33].style.fg == -1 &&
          screen.lines[3][33].style.bg == -1,
          "return output did not use restored main colours and the explicit default control");
}
static void history_and_resize(ParseMode mode) {
    Parser parser(2, 8, 3, mode);
    parser.feed("A\r\nB\r\nC\r\nD\x1b[?1049hX\r\nY\r\nZ\r\nT");
    check(parser.screen.history.empty(), "alternate output entered retained history");
    parser.screen.resize(3, 6);
    parser.feed("\x1b[?1049l");
    const auto& screen = parser.screen;
    check(screen.height() == 3 && screen.width() == 6 && screen.history.size() == 2 &&
          text(screen.history[0]) == "A     " && text(screen.history[1]) == "B     " &&
          text(screen.lines[0]) == "C     " && text(screen.lines[1]) == "D     " &&
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
        "\x1b[??1049h", "\x1b[?1049 h", "\x1b[?1049:1h", "\x1b[?25l",
        std::string("\x1b[?") + std::string(33, '0') + "1049h"};
    for (const auto& sequence : ignored) {
        Parser parser(2, 8, 1, ParseMode::recovery);
        parser.feed(sequence + "\x1b[31mR");
        parser.finish();
        check(parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.lines[0]) == "R       " && parser.screen.lines[0][0].style.fg == 1 &&
              parser.diagnostics().ignored_sequences == 1 && parser.diagnostics().max_sequence_bytes <= 33,
              "an unsupported private sequence mutated a buffer or contaminated the next CSI");
    }
    for (const auto cancellation : {char(0x18), char(0x1a), char(0x1b)}) {
        Parser parser(2, 8, 1, ParseMode::recovery);
        parser.feed(std::string("\x1b[?1049") + cancellation + "\x1b[31mR");
        parser.finish();
        check(parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.lines[0]) == "R       " && parser.screen.lines[0][0].style.fg == 1,
              "cancellation or ESC resynchronization retained the private prefix");
    }
    Parser truncated(2, 8, 1, ParseMode::recovery);
    truncated.feed("\x1b[?1049"); truncated.finish(); truncated.feed("\x1b[31mR"); truncated.finish();
    check(truncated.screen.active_buffer() == ScreenBuffer::primary && truncated.screen.lines[0][0].style.fg == 1,
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
    for (const auto* input : {"\x1b[?31m", "\x1b[?1049;25h", "\x1b[?1049;1049h", "\x1b[?25l"}) {
        Parser parser(2, 8, 1);
        parser.feed("\x1b[31;44mMAIN");
        bool rejected = false;
        try { parser.feed(input); }
        catch (const InputError&) { rejected = true; }
        check(rejected && parser.screen.active_buffer() == ScreenBuffer::primary &&
              text(parser.screen.lines[0]) == "MAIN    " &&
              parser.screen.style.fg == 1 && parser.screen.style.bg == 4,
              "strict private dispatch accepted an unsupported command or partially applied its modes");
    }
}
int main() {
    try {
        for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
            enter_and_restore(mode);
            repeated_entry_and_exit(mode);
            pending_wrap_roundtrip(mode);
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
