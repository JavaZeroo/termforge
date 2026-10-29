#include "terminal.hpp"
#include "text_projection.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// WindTerm phase 15: the two source letters have no intervening DSR.
static void disabled_edge_overwrites(ParseMode mode) {
    Parser parser(25, 91, 2, mode);
    parser.feed("\x1b[?7l\x1b[10;91HCD");
    parser.finish();
    const auto& screen = parser.screen;
    check(screen.row == 9 && screen.column == 90,
          "disabled edge CD must end at (10,91)");
    check(project_line(screen.lines[9]).text == std::string(90, ' ') + "D" &&
          project_line(screen.lines[10]).text.empty() && screen.history.empty(),
          "disabled edge CD must retain only D without a wrapped successor");
}

struct ObservedPhase {
    const char* source;
    int cursor_row, cursor_column, first_row;
    const char* first_text;
    const char* second_text;
    bool wrapped;
};

// Extracted phases 14..18, excluding DSR queries and the unsupported phase-13
// margin reset. Coordinates and logical text are independent WindTerm facts;
// physical rows/continuation links below are this Screen's engineering model.
static const std::vector<ObservedPhase> phases{
    {"\x1b[?7h\x1b[8;91HAB", 9, 2, 7, "A", "B", true},
    {"\x1b[?7l\x1b[10;91HCD", 10, 91, 9, "D", "", false},
    {"\x1b[?7h\x1b[12;91HEF", 13, 2, 11, "E", "F", true},
    {"\x1b[14;91HG\x1b[?7lH", 14, 91, 13, "H", "", false},
    {"\x1b[?7l\x1b[16;91HI\x1b[?7hJ", 17, 2, 15, "I", "J", true},
};

static void observed_prefix(ParseMode mode, size_t count, size_t split, bool bytewise) {
    std::string source;
    for (size_t index = 0; index < count; ++index) source += phases[index].source;
    Parser parser(25, 91, 2, mode);
    if (bytewise) for (const auto byte : source) parser.feed(std::string_view(&byte, 1));
    else {
        parser.feed(std::string_view(source).substr(0, split));
        parser.feed(std::string_view(source).substr(split));
    }
    parser.finish();
    const auto& screen = parser.screen;
    const auto& last = phases[count - 1];
    check(screen.row + 1 == last.cursor_row && screen.column + 1 == last.cursor_column,
          "observed prefix cursor differs from its literal DSR coordinates");
    check(screen.active_buffer() == ScreenBuffer::primary && screen.history.empty() &&
          screen.discarded == 0 && parser.diagnostics().ignored_sequences == 0,
          "a supported main-screen source changed buffers/history or was ignored");
    for (size_t index = 0; index < count; ++index) {
        const auto& phase = phases[index];
        const auto& first = screen.lines[phase.first_row];
        const auto& second = screen.lines[phase.first_row + 1];
        const auto first_projection = project_line(first).text;
        const auto second_projection = project_line(second).text;
        check(first_projection == std::string(90, ' ') + phase.first_text &&
              second_projection == phase.second_text,
              "observed prefix text differs from independent literal text");
        check(continues_into(first, second) == phase.wrapped &&
              first.ending == (phase.wrapped ? LineEnding::soft_wrap : LineEnding::none) &&
              first.wrap_columns == (phase.wrapped ? 91u : 0u),
              "the model recorded a wrap without an actual continuation, or lost a real wrap");
    }
}

static void observed_sources_across_chunks(ParseMode mode) {
    size_t prefix_bytes = 0, cases = 0;
    for (size_t count = 1; count <= phases.size(); ++count) {
        prefix_bytes += std::string_view(phases[count - 1].source).size();
        for (size_t split = 0; split <= prefix_bytes; ++split) {
            observed_prefix(mode, count, split, false); ++cases;
        }
        observed_prefix(mode, count, 0, true); ++cases;
    }
    std::cout << "observed extracted source: " << prefix_bytes << " bytes, " << cases
              << " literal-prefix chunk cases\n";
}

static void private_commands_are_atomic(ParseMode mode) {
    const std::vector<std::string> unsupported{
        "\x1b[?7;1049h", "\x1b[?1049;7h", "\x1b[?7;7l", "\x1b[?7;l",
        "\x1b[?7;9999l", "\x1b[?9999h", "\x1b[?7m", "\x1b[?7 h",
        "\x1b[?7:1h", "\x1b[??7l", "\x1b[0?7l",
        std::string("\x1b[?") + std::string(33, '0') + "7l"};
    for (const auto& sequence : unsupported) {
        Parser parser(25, 91, 2, mode);
        parser.feed("\x1b[10;91H");
        bool rejected = false;
        try { parser.feed(sequence); }
        catch (const std::exception&) { rejected = true; }
        if (mode == ParseMode::strict) {
            check(rejected && parser.screen.active_buffer() == ScreenBuffer::primary &&
                  parser.screen.row == 9 && parser.screen.column == 90 &&
                  project_line(parser.screen.lines[9]).text.empty(),
                  "strict private input was accepted or partially changed visible state");
        } else {
            check(!rejected, "recovery failed instead of consuming unsupported private input");
            parser.feed("CD"); parser.finish();
            check(parser.screen.active_buffer() == ScreenBuffer::primary &&
                  parser.screen.row == 10 && parser.screen.column == 1 &&
                  project_line(parser.screen.lines[9]).text == std::string(90, ' ') + "C" &&
                  project_line(parser.screen.lines[10]).text == "D" &&
                  parser.diagnostics().ignored_sequences == 1,
                  "recovery partially applied private modes or contaminated the next printable bytes");
        }
    }
}

static void recovery_drops_incomplete_mode_commands() {
    for (const auto cancellation : {char(0x18), char(0x1a), char(0x1b)}) {
        Parser parser(25, 91, 2, ParseMode::recovery);
        parser.feed(std::string("\x1b[?7") + cancellation + "\x1b[10;91HCD");
        parser.finish();
        check(parser.screen.row == 10 && parser.screen.column == 1 &&
              project_line(parser.screen.lines[10]).text == "D",
              "cancellation or ESC resynchronization applied an incomplete private mode");
    }
    Parser parser(25, 91, 2, ParseMode::recovery);
    parser.feed("\x1b[?7"); parser.finish();
    parser.feed("\x1b[10;91HCD"); parser.finish();
    check(parser.screen.row == 10 && parser.screen.column == 1 &&
          parser.diagnostics().truncated_sequences == 1 &&
          project_line(parser.screen.lines[10]).text == "D",
          "EOF truncation applied a private mode or contaminated the next CSI");
}

// Safety/compatibility only: off x wide text was not observed in WindTerm.
// Preserve the pre-existing insufficient-space wrap without replacing content.
static void wide_fit_policy_is_preserved(ParseMode mode) {
    Parser parser(3, 3, 2, mode);
    parser.feed("\x1b[?7l\x1b[1;3H\xe4\xb8\xad");
    parser.finish();
    check(parser.screen.row == 1 && parser.screen.column == 2 &&
          parser.screen.lines[1].size() == 3 &&
          parser.screen.lines[1][0].text == "\xe4\xb8\xad" &&
          parser.screen.lines[1][0].width == 2 && parser.screen.lines[1][1].width == 0 &&
          parser.screen.lines[1][2].text == " " &&
          project_line(parser.screen.lines[1]).text == "\xe4\xb8\xad" &&
          parser.diagnostics().replaced_scalars == 0,
          "wide scalar fit fallback replaced text, overflowed a row, or lost its trailing cell");
}

int main() {
    int failures = 0, checks = 0;
    const auto run = [&](const std::string& name, const auto& action) {
        ++checks;
        try { action(); std::cout << name << " passed\n"; }
        catch (const std::exception& error) {
            ++failures; std::cerr << name << " failed: " << error.what() << '\n';
        }
    };
    for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
        const std::string name = mode == ParseMode::strict ? "strict" : "recovery";
        run(name + " disabled edge", [&] { disabled_edge_overwrites(mode); });
        run(name + " observed chunks", [&] { observed_sources_across_chunks(mode); });
        run(name + " private guards", [&] { private_commands_are_atomic(mode); });
        run(name + " wide fit safety", [&] { wide_fit_policy_is_preserved(mode); });
    }
    run("recovery cancellation/truncation", recovery_drops_incomplete_mode_commands);
    std::cout << "DECAWM checks: " << (checks - failures) << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
