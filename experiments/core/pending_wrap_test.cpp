
// Bounded-slice literal oracle for issue #27 pending-wrap: the delayed edge
// left after a last-column printable, crossed with DECAWM, DECOM and the 1049
// buffer switch. Contract baseline is the existing core (screen.hpp wrap_pending
// / FrameState save-restore, terminal.hpp DECAWM/DECOM/1049 dispatch). Literal
// expectations come from two fixed original observations, none authored by this
// parser:
//   * #123 alternate-style phases 8-9: (3,91) 'W' -> 1049h -> alt -> 1049l ->
//     'X' replies (4,2); independent control (6,91) 'VZ' replies (7,2).
//   * #151 phases 14-18: the five last-column DECAWM on/off edge sequences and
//     their bare DSR replies.
// DECOM-on x last-column-wrap cells are composed model checks: #151 observed the
// wrap phases with origin off, so the original marks that crossing unobserved.
// Both strict and recovery modes replay the literal trace; recovery additionally
// consumes abnormal sequences without contaminating the pending edge, while
// strict rejects unsupported private forms atomically.
#include "terminal.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

// Canonical state signature for cross-chunk equality only, never a contract
// oracle. Captures cursor, pending edge, discarded count and per-row text plus
// wrap metadata so a chunking difference cannot hide behind equal replies.
static std::string signature(const Screen& s) {
    std::ostringstream out;
    out << s.row << ',' << s.column << ',' << (s.wrap_pending ? 1 : 0) << ',' << s.discarded << ';';
    const auto emit = [&](const Line& line) {
        out << line.id << ':' << static_cast<int>(line.ending) << ':' << line.next_id << ':'
            << line.wrap_columns << '|' << line_text(line) << ';';
    };
    for (const auto& line : s.history_lines()) emit(line);
    out << '#';
    for (const auto& line : s.visible_lines()) emit(line);
    return out.str();
}

// Literal cumulative trace: the five #151 DECAWM edge phases plus the #123
// pending-wrap 1049 roundtrip and its control. Replies are the original bare
// DSR 1-based coordinates; each phase depends on all earlier phases.
struct Phase {
    const char* label;
    const char* source;
    const char* reply; // "ESC[row;colR"
};

static const std::vector<Phase> phases{
    {"wrap-enabled-edge", "\x1b[?7h\x1b[8;91HAB", "\x1b[9;2R"},
    {"wrap-disabled-edge", "\x1b[?7l\x1b[10;91HCD", "\x1b[10;91R"},
    {"wrap-enabled-again-edge", "\x1b[?7h\x1b[12;91HEF", "\x1b[13;2R"},
    {"pending-edge-disable-before-next", "\x1b[14;91HG\x1b[?7lH", "\x1b[14;91R"},
    {"disabled-edge-enable-before-next", "\x1b[?7l\x1b[16;91HI\x1b[?7hJ", "\x1b[17;2R"},
    {"alternate-pending-wrap-roundtrip", "\x1b[?7h\x1b[3;91HW\x1b[?1049hTFA4\x1b[?1049lX", "\x1b[4;2R"},
    {"pending-wrap-control", "\x1b[6;91HVZ", "\x1b[7;2R"},
};

static std::string full_source() {
    std::string source;
    for (const auto& phase : phases) { source += phase.source; source += "\x1b[6n"; }
    return source;
}

static std::vector<std::string> expected_replies() {
    std::vector<std::string> replies;
    replies.reserve(phases.size());
    for (const auto& phase : phases) replies.emplace_back(phase.reply);
    return replies;
}

// Cumulative feed; after each phase a DSR request whose reply must equal the
// literal reply the original reported at that phase.
static void literal_replay(ParseMode mode) {
    Parser parser(25, 91, 100, mode);
    for (const auto& phase : phases) {
        parser.feed(phase.source);
        parser.feed("\x1b[6n");
        const auto replies = parser.take_replies();
        if (replies.size() != 1 || replies[0] != phase.reply)
            throw std::runtime_error(std::string("phase ") + phase.label + " reply mismatch");
    }
    parser.finish();
    check(parser.screen.active_buffer() == ScreenBuffer::primary && parser.screen.row == 6 &&
          parser.screen.column == 1 && !parser.screen.wrap_pending && parser.screen.history_lines().empty() &&
          parser.screen.discarded == 0,
          "literal trace ended outside the final pending-wrap control state");
    // The 1049 roundtrip must have preserved the real continuation: 'W' keeps a
    // soft-wrap link to the row that 'X' started.
    check(line_text(parser.screen.visible_lines()[2]) == std::string(90, ' ') + "W" &&
          line_text(parser.screen.visible_lines()[3]).substr(0, 1) == "X" &&
          parser.screen.visible_lines()[2].ending == LineEnding::soft_wrap &&
          parser.screen.visible_lines()[2].next_id == parser.screen.visible_lines()[3].id &&
          parser.screen.visible_lines()[2].wrap_columns == 91u,
          "1049 roundtrip lost the literal pending-wrap continuation");
}

// The same seven replies must not depend on how the byte stream is chunked,
// including the last-column character landing in a packet by itself.
static void chunk_invariance(ParseMode mode, size_t* cases) {
    const std::string source = full_source();
    const auto expected = expected_replies();
    std::string single;
    {
        Parser parser(25, 91, 100, mode);
        parser.feed(source);
        check(parser.take_replies() == expected, "single-shot replies differ from the literal trace");
        parser.finish();
        single = signature(parser.screen);
    }
    {
        Parser parser(25, 91, 100, mode);
        for (const auto byte : source) parser.feed(std::string_view(&byte, 1));
        check(parser.take_replies() == expected, "byte-wise replies differ from the literal trace");
        parser.finish();
        check(signature(parser.screen) == single, "byte-wise state differs from single-shot");
    }
    for (size_t split = 0; split <= source.size(); ++split) {
        Parser parser(25, 91, 100, mode);
        parser.feed(std::string_view(source).substr(0, split));
        parser.feed(std::string_view(source).substr(split));
        check(parser.take_replies() == expected, "two-way split replies differ from the literal trace");
        parser.finish();
        check(signature(parser.screen) == single, "two-way split state differs from single-shot");
        ++*cases;
    }
    // A last-column byte split in isolation: 'W' at (3,91) then 'X' after the
    // 1049 roundtrip must still wrap regardless of which feed carries 'W'.
    for (size_t cut = 0; cut <= 1; ++cut) {
        Parser parser(25, 91, 100, mode);
        const std::string arm = "\x1b[3;91HW";
        parser.feed(std::string_view(arm).substr(0, cut));
        parser.feed(std::string_view(arm).substr(cut));
        parser.feed("\x1b[?1049hTFA4\x1b[?1049lX\x1b[6n");
        check(parser.take_replies() == std::vector<std::string>{"\x1b[4;2R"},
              "split last-column arm changed the pending-wrap reply");
        parser.finish();
    }
}

// #123 phase 8-9: pending wrap armed in primary survives a 1049 roundtrip; the
// next primary printable continues onto the successor row. Repeated switching
// is a composed model check, not a literal original phase.
static void pending_wrap_1049_roundtrip(ParseMode mode) {
    Parser parser(25, 91, 2, mode);
    parser.feed("\x1b[3;91HW");
    check(parser.screen.wrap_pending && parser.screen.row == 2 && parser.screen.column == 90,
          "fixture did not arm the primary pending wrap");
    parser.feed("\x1b[?1049h");
    check(parser.screen.active_buffer() == ScreenBuffer::alternate && !parser.screen.wrap_pending,
          "alternate entry retained the primary pending edge");
    parser.feed("TFA4\x1b[?1049l");
    check(parser.screen.active_buffer() == ScreenBuffer::primary && parser.screen.wrap_pending &&
          parser.screen.row == 2 && parser.screen.column == 90,
          "first 1049 exit lost the saved primary pending wrap");
    // Re-enter and re-exit without consuming the edge; it must still be armed.
    parser.feed("\x1b[?1049hALT2\x1b[?1049l");
    check(parser.screen.wrap_pending && parser.screen.row == 2 && parser.screen.column == 90,
          "repeated 1049 switching dropped the primary pending wrap");
    parser.feed("X\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[4;2R"},
          "restored pending wrap did not place the next printable on the successor row");
    check(parser.screen.row == 3 && parser.screen.column == 1 && !parser.screen.wrap_pending &&
          line_text(parser.screen.visible_lines()[2]) == std::string(90, ' ') + "W" &&
          line_text(parser.screen.visible_lines()[3]).substr(0, 1) == "X" &&
          parser.screen.visible_lines()[2].ending == LineEnding::soft_wrap &&
          parser.screen.visible_lines()[2].next_id == parser.screen.visible_lines()[3].id,
          "repeated-switch pending wrap lost the real continuation");
    parser.finish();
}

// DECAWM on x origin off: literal #151 phases 14/18. A last-column write arms
// the delayed edge; the next printable wraps to the next row and starts there.
static void decawm_on_edge_wraps(ParseMode mode) {
    Parser parser(3, 4, 2, mode);
    parser.feed("\x1b[?7h\x1b[1;4HAB");
    check(parser.screen.row == 1 && parser.screen.column == 1 && !parser.screen.wrap_pending &&
          line_text(parser.screen.visible_lines()[0]) == "   A" &&
          line_text(parser.screen.visible_lines()[1]).substr(0, 1) == "B" &&
          parser.screen.visible_lines()[0].ending == LineEnding::soft_wrap &&
          parser.screen.visible_lines()[0].next_id == parser.screen.visible_lines()[1].id,
          "autowrap on did not wrap the delayed edge to the next row");
}

// DECAWM off x origin off: literal #151 phases 15-17. The last-column write
// stays put (cursor at the last column) and the next printable overwrites it.
static void decawm_off_edge_stays(ParseMode mode) {
    Parser parser(3, 4, 2, mode);
    parser.feed("\x1b[?7l\x1b[1;4HCD");
    check(parser.screen.row == 0 && parser.screen.column == 3 && parser.screen.wrap_pending &&
          line_text(parser.screen.visible_lines()[0]) == "   D",
          "autowrap off did not keep the last-column overwrite");
    // Disable-after-arm (#151 phase 17): armed edge stays but autowrap off
    // gates consuming it, so the next printable overwrites in place.
    Parser armed(3, 4, 2, mode);
    armed.feed("\x1b[1;4HG\x1b[?7lH");
    check(armed.screen.row == 0 && armed.screen.column == 3 &&
          line_text(armed.screen.visible_lines()[0]) == "   H",
          "armed edge consumed despite autowrap being off");
    // Enable-after-arm (#151 phase 18): enabling autowrap lets the next
    // printable start the continuation.
    Parser rearm(3, 4, 2, mode);
    rearm.feed("\x1b[?7l\x1b[1;4HI\x1b[?7hJ");
    check(rearm.screen.row == 1 && rearm.screen.column == 1 &&
          line_text(rearm.screen.visible_lines()[0]) == "   I" &&
          line_text(rearm.screen.visible_lines()[1]).substr(0, 1) == "J" &&
          rearm.screen.visible_lines()[0].ending == LineEnding::soft_wrap,
          "enabling autowrap did not start the delayed continuation");
}

// DECAWM on x DECOM on: composed model check. #151 observed the wrap phases
// with origin off, so this crossing is not a literal original phase. The wrap
// must stay region-bounded and the DSR stays origin-relative.
static void origin_on_edge_wraps_in_region(ParseMode mode) {
    Parser parser(5, 5, 2, mode);
    parser.feed("\x1b[2;4r\x1b[?6h\x1b[1;5HXY");
    check(parser.screen.origin_mode() && parser.screen.row == 2 && parser.screen.column == 1 &&
          !parser.screen.wrap_pending,
          "origin wrap did not land on the next region row");
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[2;2R"},
          "origin wrap reported an absolute rather than relative reply");
    parser.feed("Q");
    check(parser.screen.row == 2 && parser.screen.column == 2 &&
          line_text(parser.screen.visible_lines()[2]).substr(0, 3) == "YQ ",
          "origin wrap misplaced the next printable");
    parser.finish();
}

// DECAWM off x DECOM on: composed model check, unobserved in the original. The
// last-column write stays on the region edge and the next printable overwrites.
static void origin_on_edge_stays(ParseMode mode) {
    Parser parser(5, 5, 2, mode);
    parser.feed("\x1b[2;4r\x1b[?6h\x1b[?7l\x1b[1;5HAB");
    check(parser.screen.row == 1 && parser.screen.column == 4 && parser.screen.wrap_pending &&
          line_text(parser.screen.visible_lines()[1]).substr(4) == "B",
          "origin x autowrap-off did not keep the last-column overwrite");
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[1;5R"},
          "origin x autowrap-off reported the wrong relative reply");
    parser.finish();
}

// Recovery consumes abnormal sequences without touching the armed edge or
// contaminating the next printable; strict rejects unsupported forms atomically.
static void abnormal_sequences_keep_pending_edge() {
    // CAN/SUB/ESC resynchronization mid-private-mode: the armed edge and cursor
    // survive, then 'X' still wraps onto the successor row.
    for (const auto cancellation : {char(0x18), char(0x1a), char(0x1b)}) {
        Parser parser(25, 91, 2, ParseMode::recovery);
        parser.feed("\x1b[3;91HW");
        parser.feed(std::string("\x1b[?1049") + cancellation);
        parser.feed("\x1b[6n");
        check(parser.take_replies() == std::vector<std::string>{"\x1b[3;91R"} &&
              parser.screen.wrap_pending && parser.screen.row == 2 && parser.screen.column == 90,
              "abnormal private prefix moved the cursor or cleared the pending edge");
        parser.feed("X"); parser.finish();
        check(parser.screen.row == 3 && parser.screen.column == 1 &&
              line_text(parser.screen.visible_lines()[3]).substr(0, 1) == "X",
              "abnormal private prefix contaminated the next printable");
    }
    // EOF truncation resets the recovery sequence without clearing the edge.
    Parser truncated(25, 91, 2, ParseMode::recovery);
    truncated.feed("\x1b[3;91HW");
    truncated.feed("\x1b[?1049");
    truncated.finish();
    check(truncated.diagnostics().truncated_sequences == 1 && truncated.screen.wrap_pending &&
          truncated.screen.row == 2 && truncated.screen.column == 90,
          "EOF truncation cleared the pending edge");
    truncated.feed("X"); truncated.finish();
    check(truncated.screen.row == 3 && truncated.screen.column == 1 &&
          line_text(truncated.screen.visible_lines()[3]).substr(0, 1) == "X",
          "truncated private input contaminated the next printable");
    // Unsupported private form is ignored without clearing the edge.
    Parser ignored(25, 91, 2, ParseMode::recovery);
    ignored.feed("\x1b[3;91HW");
    ignored.feed("\x1b[?1049;25h");
    check(ignored.diagnostics().ignored_sequences == 1 && ignored.screen.wrap_pending &&
          ignored.screen.row == 2 && ignored.screen.column == 90 &&
          ignored.screen.active_buffer() == ScreenBuffer::primary,
          "unsupported private form mutated a buffer or cleared the pending edge");
    ignored.feed("X"); ignored.finish();
    check(ignored.screen.row == 3 && ignored.screen.column == 1 &&
          line_text(ignored.screen.visible_lines()[3]).substr(0, 1) == "X",
          "unsupported private form contaminated the next printable");
    // Strict rejects unsupported private forms atomically, leaving the armed
    // edge and cursor untouched (the parser is left mid-CSI, so no continuation).
    Parser strict(25, 91, 2);
    strict.feed("\x1b[3;91HW");
    bool rejected = false;
    try { strict.feed("\x1b[?1049;25h"); }
    catch (const InputError&) { rejected = true; }
    check(rejected && strict.screen.wrap_pending && strict.screen.row == 2 &&
          strict.screen.column == 90 && strict.screen.active_buffer() == ScreenBuffer::primary,
          "strict accepted an unsupported private form or partially applied its modes");
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
        run(name + " literal replay", [&] { literal_replay(mode); });
        run(name + " 1049 pending-wrap roundtrip", [&] { pending_wrap_1049_roundtrip(mode); });
        run(name + " decawm on edge", [&] { decawm_on_edge_wraps(mode); });
        run(name + " decawm off edge", [&] { decawm_off_edge_stays(mode); });
        run(name + " origin on edge", [&] { origin_on_edge_wraps_in_region(mode); });
        run(name + " origin off edge", [&] { origin_on_edge_stays(mode); });
        size_t cases = 0;
        run(name + " chunk invariance", [&] { chunk_invariance(mode, &cases); });
        std::cout << "  " << name << " chunk cases: " << cases << "\n";
    }
    run("recovery abnormal sequences", abnormal_sequences_keep_pending_edge);
    std::cout << "pending-wrap checks: " << (checks - failures) << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
