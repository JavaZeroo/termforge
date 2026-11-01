#include "terminal.hpp"
#include "text_search.hpp"
#include "text_selection.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The 25 phases of the fixed 91x25 WindTerm origin/wrap observation (#151),
// literal source bytes and the matching bare DSR reply for each phase. Every
// phase's reply depends on all preceding phases, so this is a cumulative trace.
struct Phase {
    const char* label;
    const char* source;
    const char* reply; // "ESC[row;colR"
};

static const std::vector<Phase> phases{
    {"fresh-session-cup", "\x1b[2J\x1b[HFT", "\x1b[1;3R"},
    {"explicit-origin-wrap-reset", "\x1b[?6l\x1b[?7h\x1b[r\x1b[2J\x1b[H", "\x1b[1;1R"},
    {"main-margin-set", "\x1b[2;5r", "\x1b[1;1R"},
    {"origin-reset-cup", "\x1b[1;3HD", "\x1b[1;4R"},
    {"origin-set", "\x1b[?6h", "\x1b[1;1R"},
    {"origin-set-cup", "\x1b[1;3HO", "\x1b[1;4R"},
    {"origin-set-upper-relative-bound", "\x1b[99A\x1b[99D", "\x1b[1;1R"},
    {"origin-set-lower-relative-bound", "\x1b[99B", "\x1b[4;1R"},
    {"origin-reset-after-margin", "\x1b[?6l", "\x1b[1;1R"},
    {"region-sentinels", "\x1b[r\x1b[2J\x1b[1;1HTP\x1b[25;1HBT\x1b[2;1HR0\x1b[3;1HR1\x1b[4;1HR2\x1b[5;1HR3\x1b[2;5r", "\x1b[1;1R"},
    {"region-bottom-newline", "\x1b[5;1H\r\nNW", "\x1b[5;3R"},
    {"above-region-newline", "\x1b[1;1H\nUP", "\x1b[2;3R"},
    {"below-region-newline", "\x1b[25;1H\nDN", "\x1b[25;3R"},
    {"main-margin-reset", "\x1b[r", "\x1b[1;1R"},
    {"wrap-enabled-edge", "\x1b[?7h\x1b[8;91HAB", "\x1b[9;2R"},
    {"wrap-disabled-edge", "\x1b[?7l\x1b[10;91HCD", "\x1b[10;91R"},
    {"wrap-enabled-again-edge", "\x1b[?7h\x1b[12;91HEF", "\x1b[13;2R"},
    {"pending-edge-disable-before-next", "\x1b[14;91HG\x1b[?7lH", "\x1b[14;91R"},
    {"disabled-edge-enable-before-next", "\x1b[?7l\x1b[16;91HI\x1b[?7hJ", "\x1b[17;2R"},
    {"main-margin-origin-before-alt", "\x1b[?7h\x1b[2;5r\x1b[?6h\x1b[2;4HM", "\x1b[2;5R"},
    {"alternate-enter", "\x1b[?1049h", "\x1b[1;1R"},
    {"alternate-margin-origin", "\x1b[3;6r\x1b[?6h\x1b[2;4HA", "\x1b[2;5R"},
    {"alternate-exit", "\x1b[?1049l", "\x1b[2;5R"},
    {"main-cup-after-alternate", "\x1b[1;6HR", "\x1b[1;7R"},
    {"final-explicit-reset", "\x1b[?6l\x1b[?7h\x1b[r\x1b[1;1HDONE", "\x1b[1;5R"},
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

// Cumulative feed: after each phase's source a DSR request, its reply must be
// the literal reply WindTerm reported at that phase.
static void literal_replay(ParseMode mode) {
    Parser parser(25, 91, 100, mode);
    for (const auto& phase : phases) {
        parser.feed(phase.source);
        parser.feed("\x1b[6n");
        const auto replies = parser.take_replies();
        if (replies.empty())
            throw std::runtime_error(std::string("phase ") + phase.label + " produced no reply");
        if (replies.size() != 1 || replies[0] != phase.reply)
            throw std::runtime_error(std::string("phase ") + phase.label + " reply mismatch");
    }
    parser.finish();
    check(parser.screen.active_buffer() == ScreenBuffer::primary && parser.screen.row == 0 &&
          parser.screen.column == 4 && !parser.screen.origin_mode() &&
          parser.screen.region_bottom() == 24 && parser.screen.region_top() == 0,
          "trace ended outside the final explicit-reset state");
}

// The observed partial-region LF (#151 phases 9-12) removes one row and
// inserts a new identity before retained rows below the region. Text readers
// must still see that real document order; ids are not sortable coordinates.
static void observed_region_text_readers(ParseMode mode, bool bytewise) {
    Parser parser(25, 91, 100, mode);
    const auto feed = [&](const char* source) {
        if (bytewise) for (const char byte : std::string_view(source))
            parser.feed(std::string_view(&byte, 1));
        else parser.feed(source);
    };
    const auto phase = [&](size_t index) {
        feed(phases[index].source); feed("\x1b[6n");
        check(parser.take_replies() == std::vector<std::string>{phases[index].reply},
              "region reader replay changed the observed DSR");
    };
    for (size_t index = 0; index <= 9; ++index) phase(index);
    const TextDocument document(parser.screen);
    TextSearch search;
    auto generation = search.start(document, "NW");
    const auto finish_search = [&] {
        for (int step = 0; step < 100 && search.status() == SearchStatus::running; ++step)
            search.step(document, generation, 128);
        check(search.status() == SearchStatus::complete, "region search did not finish within its work bound");
    };
    finish_search();
    check(!search.match(document, 0), "region test began with an unexpected NW match");
    const auto retired = document.meta(1).id;
    const auto top = *document.position(0, 0), bottom = *document.position(24, 0);
    const auto before_scroll = document.order_revision();
    phase(10);
    const std::string expected[] = {"TP", "R1", "R2", "R3", "NW"};
    for (size_t row = 0; row < 5; ++row)
        check(document.line(row).projection.text == expected[row], "observed region text differs from the literal source");
    check(document.line(24).projection.text == "BT" && document.valid(top) && document.valid(bottom),
          "partial-region scrolling changed an outside sentinel");
    check(parser.screen.history_lines().empty() && document.order_revision() > before_scroll,
          "partial-region scroll missed its structure change or leaked into history");
    check(!document.index_of(retired), "scrolling retained the retired top-region identity");
    for (size_t row = 0; row < document.size(); ++row) {
        const auto position = *document.position(row, 0);
        check(document.index_of(position.line_id) == std::optional<size_t>(row) && document.valid(position),
              "region scroll made a retained row unreachable to text readers");
    }
    TextSelection selection;
    check(selection.set(document, {*document.position(4, 0), *document.position(4, 2)}),
          "the new bottom-region text could not be selected");
    const auto copied = selection.copy(document, 16);
    check(copied.status == SelectionCopyStatus::ok && copied.text == "NW",
          "selection did not copy the new bottom-region text");
    generation = search.notify_change(document);
    finish_search();
    const auto match = search.match(document, 0);
    check(match && document.index_of(match->range.begin.line_id) == std::optional<size_t>(4) &&
          !search.match(document, 1), "incremental search missed or duplicated the new region text");
    const auto after_scroll = document.order_revision();
    phase(11);
    check(document.line(0).projection.text == "TP" && document.line(1).projection.text == "UP" &&
          document.line(24).projection.text == "BT" && parser.screen.history_lines().empty() &&
          parser.screen.discarded == 0 && document.order_revision() == after_scroll,
          "observed above-region LF moved an outside row or leaked into history");
    phase(12);
    check(document.line(1).projection.text == "UP" && document.line(2).projection.text == "R2" &&
          document.line(3).projection.text == "R3" && document.line(4).projection.text == "NW" &&
          document.line(24).projection.text == "DN" && document.order_revision() == after_scroll,
          "observed outside-region LF scrolled the region or changed its membership");
    check(document.line(0).projection.text == "TP" && parser.screen.history_lines().empty() &&
          parser.screen.discarded == 0 && document.size() == 25,
          "observed below-region LF scrolled the viewport or changed retained history");
    for (size_t row = 5; row < 24; ++row)
        check(document.line(row).projection.text.empty(),
              "observed outside-region LF copied text into an untouched row");
    // The same lookup state must survive the observed later 1049 roundtrip.
    for (size_t index = 13; index < phases.size(); ++index) phase(index);
    for (size_t row = 0; row < document.size(); ++row)
        check(document.index_of(document.meta(row).id) == std::optional<size_t>(row),
              "the observed 1049 roundtrip lost exact row lookup");
    parser.finish();
}

// The same 25 replies must not depend on how the byte stream is chunked.
static void chunk_invariance(ParseMode mode, size_t* cases) {
    const std::string source = full_source();
    const auto expected = expected_replies();
    {
        Parser parser(25, 91, 100, mode);
        parser.feed(source);
        check(parser.take_replies() == expected, "single-shot replies differ from the literal trace");
        parser.finish();
    }
    {
        Parser parser(25, 91, 100, mode);
        for (const auto byte : source) parser.feed(std::string_view(&byte, 1));
        check(parser.take_replies() == expected, "byte-wise replies differ from the literal trace");
        parser.finish();
    }
    for (size_t split = 0; split <= source.size(); ++split) {
        Parser parser(25, 91, 100, mode);
        parser.feed(std::string_view(source).substr(0, split));
        parser.feed(std::string_view(source).substr(split));
        check(parser.take_replies() == expected, "two-way split replies differ from the literal trace");
        parser.finish();
        ++*cases;
    }
}

// DECOM and DECAWM are independent: toggling one must not disturb the other.
// Returning through 1049 restores primary origin without changing wrap policy.
static void origin_autowrap_independence(ParseMode mode) {
    Parser parser(3, 5, 2, mode);
    parser.feed("\x1b[2;3r\x1b[?6h");
    check(parser.screen.origin_mode(), "origin did not turn on");
    parser.feed("\x1b[?7l");
    check(parser.screen.origin_mode(), "?7l cleared origin mode");
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[1;1R"},
          "origin-relative reply lost after an autowrap toggle");
    parser.feed("\x1b[?6l");
    check(!parser.screen.origin_mode(), "?6l did not clear origin mode");
    // Autowrap is still off: a last-column write keeps the cursor on the cell.
    parser.feed("\x1b[3;5HZ");
    check(parser.screen.row == 2 && parser.screen.column == 4,
          "origin toggle re-enabled autowrap and moved the last-column cursor");
    // Origin and autowrap both persist across a 1049 roundtrip.
    Parser buffers(3, 5, 2, mode);
    buffers.feed("\x1b[2;3r\x1b[?6h\x1b[?7l");
    buffers.feed("\x1b[?1049h\x1b[?1049l");
    check(buffers.screen.active_buffer() == ScreenBuffer::primary && buffers.screen.origin_mode() &&
          buffers.screen.region_top() == 1 && buffers.screen.region_bottom() == 2,
          "origin/region state was not preserved across 1049");
}

// Composed model check, not a literal-oracle phase: the autowrap newline stays
// region-bounded and the DSR stays origin-relative when both modes are on.
static void origin_autowrap_newline_is_region_bounded(ParseMode mode) {
    Parser parser(5, 5, 2, mode);
    parser.feed("\x1b[2;4r\x1b[?6h\x1b[1;5HXY");
    check(parser.screen.origin_mode() && parser.screen.row == 2 && parser.screen.column == 1,
          "wrap under origin did not land on the next region row");
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[2;2R"},
          "wrap under origin reported an absolute rather than relative reply");
    parser.finish();
}

// Composed engineering invariant, not a WindTerm oracle: a soft wrap at the
// region bottom must link the moved predecessor to the newly created row,
// never to the untouched physical row below the region. Wide/combining text
// also lets the independent copy oracle detect a missing continuation.
static void region_bottom_wrap_keeps_its_successor(ParseMode mode, bool bytewise) {
    Parser parser(5, 4, 2, mode);
    const std::string source =
        "TOP\r\nA\r\nB\r\nC\r\nBOT\x1b[2;4r\x1b[?6h\x1b[3;3H\xe4\xb8\xad\xcc\x81X\x1b[6n";
    if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
    else parser.feed(source);
    check(parser.take_replies() == std::vector<std::string>{"\x1b[3;2R"},
          "region-bottom autowrap did not retain the origin-relative cursor");
    const TextDocument document(parser.screen);
    const std::string expected[]{"TOP", "B", "C \xe4\xb8\xad\xcc\x81", "X", "BOT"};
    for (size_t row = 0; row < 5; ++row)
        check(document.line(row).projection.text == expected[row],
              "region-bottom wrap changed literal text or an outside sentinel");
    check(parser.screen.history_lines().empty() && parser.screen.discarded == 0 &&
          parser.screen.visible_lines()[2][2].width == 2 &&
          parser.screen.visible_lines()[2][2].marks == 1 &&
          parser.screen.visible_lines()[2][3].width == 0,
          "region-bottom wrap leaked history or split a wide combining glyph");
    TextSelection selection;
    check(selection.set(document, {*document.position(2, 0), *document.position(3, 1)}),
          "region-bottom wrapped text was not selectable");
    const auto copy = selection.copy(document, 32);
    check(copy.status == SelectionCopyStatus::ok && copy.text == "C \xe4\xb8\xad\xcc\x81X",
          "region-bottom wrap inserted a hard break into copied logical text");
    check(document.meta(2).joins_next &&
          parser.screen.visible_lines()[2].next_id == parser.screen.visible_lines()[3].id &&
          parser.screen.visible_lines()[2].next_id != parser.screen.visible_lines()[4].id,
          "region-bottom wrap linked its predecessor to the outside sentinel instead of its successor");
    parser.finish();
}

// Existing physical-row resize policy, not an observed WindTerm resize trace.
// Both independently owned frames must share geometry, cropped
// text must stay discarded on growth, and only primary rows enter history.
static void origin_margin_resize_both_frames(ParseMode mode, bool bytewise) {
    Parser parser(6, 6, 3, mode);
    const auto feed = [&](std::string_view source) {
        if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
        else parser.feed(source);
    };
    feed("P0\r\nP1\r\nP2\r\nP3\r\nP4\r\nP5\x1b[2;5r\x1b[?6h\x1b[4;6HW"
         "\x1b[?1049hA0\r\nA1\r\nA2\r\nA3\r\nA4\r\nA5\x1b[3;5r\x1b[?6h\x1b[3;6HQ");
    parser.screen.resize(3, 4);
    feed("\x1b[6n");
    const auto& screen = parser.screen;
    check(parser.take_replies() == std::vector<std::string>{"\x1b[3;4R"} &&
          screen.origin_mode() && screen.region_top() == 0 && screen.region_bottom() == 2 &&
          !screen.wrap_pending && screen.history_lines().empty() && screen.discarded == 2,
          "alternate shrink retained stale margins/edge or placed discarded rows in history");
    const TextDocument alternate(screen);
    check(alternate.line(0).projection.text == "A2" && alternate.line(1).projection.text == "A3" &&
          alternate.line(2).projection.text == "A4" && alternate.size() == 3,
          "alternate shrink did not follow the literal physical-row crop");
    feed("X");
    parser.screen.resize(5, 6);
    feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[3;4R"} &&
          screen.region_top() == 0 && screen.region_bottom() == 4 && screen.origin_mode() &&
          !screen.wrap_pending && alternate.line(0).projection.text == "A2" &&
          alternate.line(1).projection.text == "A3" && alternate.line(2).projection.text == "A4 X" &&
          alternate.line(3).projection.text.empty() && alternate.line(4).projection.text.empty() &&
          screen.history_lines().empty() && screen.discarded == 2,
          "alternate growth revived cropped rows/text or changed cursor/origin policy");
    feed("\x1b[?1049l\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[3;4R"} &&
          screen.active_buffer() == ScreenBuffer::primary && screen.origin_mode() &&
          screen.region_top() == 0 && screen.region_bottom() == 4 && !screen.wrap_pending &&
          screen.history_lines().size() == 2 && screen.discarded == 0,
          "return restored stale primary geometry, margins, cursor or history accounting");
    const TextDocument primary(screen);
    const std::string expected[]{"P0", "P1", "P2", "P3", "P4", "", ""};
    for (size_t row = 0; row < 7; ++row)
        check(primary.line(row).projection.text == expected[row],
              "hidden primary resize imported alternate text or revived cropped text");
    feed("Z");
    check(primary.line(4).projection.text == "P4 Z" && screen.row == 2 && screen.column == 4,
          "primary output after resize consumed an obsolete pending wrap");
    parser.finish();
}

// Recovery consumes malformed/incomplete DECOM without changing state or
// contaminating the next CSI; strict rejects unsupported forms atomically.
static void abnormal_origin_sequences() {
    for (const auto cancellation : {char(0x18), char(0x1a), char(0x1b)}) {
        Parser parser(3, 10, 2, ParseMode::recovery);
        parser.feed(std::string("\x1b[?6") + cancellation + "\x1b[2;2HA");
        parser.finish();
        check(!parser.screen.origin_mode() && parser.screen.row == 1 && parser.screen.column == 2,
              "cancellation applied an incomplete DECOM or contaminated the next CSI");
    }
    Parser truncated(3, 10, 2, ParseMode::recovery);
    truncated.feed("\x1b[?6");
    truncated.finish();
    truncated.feed("\x1b[2;2HB");
    truncated.finish();
    check(!truncated.screen.origin_mode() && truncated.screen.row == 1 && truncated.screen.column == 2 &&
          truncated.diagnostics().truncated_sequences == 1,
          "EOF truncation applied DECOM or contaminated the next CSI");
    Parser multi(3, 10, 2, ParseMode::recovery);
    multi.feed("\x1b[?6;6h\x1b[2;2HC");
    multi.finish();
    check(!multi.screen.origin_mode() && multi.diagnostics().ignored_sequences == 1 &&
          multi.screen.row == 1 && multi.screen.column == 2,
          "recovery partially applied an unsupported DECOM form");
    // Strict rejects the same unsupported private forms atomically, leaving the
    // cursor and origin state untouched.
    for (const auto* input : {"\x1b[?6;6h", "\x1b[?6m", "\x1b[?6 h"}) {
        Parser parser(3, 10, 2);
        parser.feed("\x1b[2;2HD");
        bool rejected = false;
        try { parser.feed(input); }
        catch (const InputError&) { rejected = true; }
        check(rejected && !parser.screen.origin_mode() && parser.screen.row == 1 && parser.screen.column == 2,
              "strict accepted an unsupported DECOM form or partially changed state");
    }
}

static void ris_resets_origin(ParseMode mode) {
    Parser parser(3, 10, 2, mode);
    parser.feed("\x1b[2;3r\x1b[?6h");
    check(parser.screen.origin_mode(), "test setup did not enable origin");
    parser.feed("\x1b" "c");
    check(!parser.screen.origin_mode() && parser.screen.row == 0 && parser.screen.column == 0,
          "RIS did not reset origin mode and home the cursor");
    parser.finish();
    // Preserve the pre-existing origin-off reset policy when the former shared
    // bit becomes frame-owned. This is not an original alternate-RIS oracle.
    Parser alternate(4, 6, 2, mode);
    alternate.feed("\x1b[?6h\x1b[?1049h\x1b[?6h\x1b" "c");
    check(alternate.screen.active_buffer() == ScreenBuffer::primary && !alternate.screen.origin_mode(),
          "RIS restored a saved primary origin-on state");
    alternate.feed("\x1b[?1049h");
    check(!alternate.screen.origin_mode(), "RIS left a saved alternate origin-on state");
    alternate.finish();
}

// Two fresh 91x20 WindTerm 2.7.0 observations distinguish main ON/alternate
// OFF from main OFF/alternate ON. Replies and marker cell positions are literal
// output oracles; the original document's combined primary/alternate line
// storage is not imported into this core's frame model.
static void observed_independent_origins(ParseMode mode, bool main_on, bool bytewise) {
    static constexpr const char* sentinels =
        "\x1b[1;1HR01\x1b[2;1HR02\x1b[3;1HR03\x1b[4;1HR04"
        "\x1b[5;1HR05\x1b[6;1HR06\x1b[7;1HR07\x1b[8;1HR08\x1b[20;1HSB";
    const std::string initial = std::string("\x1b[?6l\x1b[?7h\x1b[r\x1b[2J") + sentinels;
    const std::string lower_bound = std::string(sentinels) + "\x1b[?6h\x1b[99;1H";
    const std::vector<Phase> trace{
        {"fresh-primary-sentinels", initial.c_str(), "\x1b[20;3R"},
        {"primary-known-origin", main_on ? "\x1b[2;5r\x1b[?6h\x1b[2;10HMP" : "\x1b[2;5r\x1b[?6l\x1b[2;10HMP", "\x1b[2;12R"},
        {"first-alternate-enter", "\x1b[?1049h", "\x1b[1;1R"},
        {"alternate-origin-sample", "\x1b[3;7r\x1b[1;1HAS\x1b[99;1H", "\x1b[20;1R"},
        {"alternate-origin-on-control", "\x1b[?6h\x1b[3;7r\x1b[1;1HAI\x1b[99;1H", "\x1b[5;1R"},
        {"alternate-origin-off-control", "\x1b[?6l\x1b[3;7r\x1b[1;1HAO\x1b[99;1H", "\x1b[20;1R"},
        {"alternate-opposite-origin", main_on ? "\x1b[?6l\x1b[2;10HAP" : "\x1b[?6h\x1b[2;10HAP", "\x1b[2;12R"},
        {"primary-return-untouched", "\x1b[?1049l", "\x1b[2;12R"},
        {"primary-origin-sample", "\x1b[1;1HPS\x1b[99;1H", main_on ? "\x1b[4;1R" : "\x1b[20;1R"},
        {"primary-origin-on-control", "\x1b[?6h\x1b[1;1HPI\x1b[99;1H", "\x1b[4;1R"},
        {"primary-origin-off-control", "\x1b[?6l\x1b[1;1HPO\x1b[99;1H", "\x1b[20;1R"},
        {"primary-bottom-sentinels", lower_bound.c_str(), "\x1b[4;1R"},
        {"primary-bottom-newline", "\r\nNW", "\x1b[4;3R"},
        {"final-explicit-reset", "\x1b[?6l\x1b[?7h\x1b[r\x1b[1;1HDONE", "\x1b[1;5R"},
    };
    Parser parser(20, 91, 100, mode);
    const auto feed = [&](std::string_view source) {
        if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
        else parser.feed(source);
    };
    const auto marker_at = [&](size_t row, size_t column, const std::string& marker) {
        const auto text = project_line(parser.screen.visible_lines()[row]).text;
        return text.size() >= column + marker.size() && text.substr(column, marker.size()) == marker;
    };
    std::string failures;
    for (size_t phase = 0; phase < trace.size(); ++phase) {
        feed(trace[phase].source); feed("\x1b[6n");
        if (parser.take_replies() != std::vector<std::string>{trace[phase].reply})
            failures += std::string(trace[phase].label) + " literal DSR differs; ";
        bool markers_match = true;
        switch (phase) {
        case 1: markers_match = marker_at(main_on ? 2 : 1, 9, "MP"); break;
        case 3: markers_match = marker_at(0, 0, "AS"); break;
        case 4: markers_match = marker_at(2, 0, "AI"); break;
        case 5: markers_match = marker_at(0, 0, "AO"); break;
        case 6: markers_match = marker_at(main_on ? 1 : 3, 9, "AP"); break;
        case 8: markers_match = marker_at(main_on ? 1 : 0, 0, "PS"); break;
        case 9: markers_match = marker_at(1, 0, "PI"); break;
        case 10: markers_match = marker_at(0, 0, "PO"); break;
        case 12:
            markers_match = marker_at(4, 0, "NW") && marker_at(0, 0, "R01") && marker_at(5, 0, "R06") &&
                            marker_at(6, 0, "R07") && marker_at(7, 0, "R08") && marker_at(19, 0, "SB");
            break;
        default: break;
        }
        if (!markers_match) failures += std::string(trace[phase].label) + " literal marker position differs; ";
        const bool alternate = phase >= 2 && phase <= 6;
        if ((parser.screen.active_buffer() == ScreenBuffer::alternate) != alternate)
            failures += std::string(trace[phase].label) + " active buffer differs; ";
    }
    parser.finish();
    if (!failures.empty()) throw std::runtime_error(failures);
}

// Fresh original re-entry observation: both duplicate and later 1049h match
// the explicit origin-OFF control; the untouched primary retains origin ON.
static void observed_origin_reentry(ParseMode mode, bool bytewise) {
    const Phase trace[]{
        {"primary", "\x1b[?6l\x1b[?7h\x1b[r\x1b[2J\x1b[1;1HR01\x1b[2;1HR02\x1b[3;1HR03\x1b[4;1HR04\x1b[5;1HR05\x1b[6;1HR06\x1b[7;1HR07\x1b[8;1HR08\x1b[2;5r\x1b[?6h\x1b[2;10HMP", "\x1b[2;12R"},
        {"first-enter", "\x1b[?1049h\x1b[3;7r\x1b[?6h\x1b[2;10HA0", "\x1b[2;12R"},
        {"duplicate", "\x1b[?1049h", "\x1b[1;1R"},
        {"duplicate-sample", "\x1b[3;7r\x1b[1;1HDS\x1b[99;1H", "\x1b[20;1R"},
        {"duplicate-on", "\x1b[?6h\x1b[3;7r\x1b[1;1HDI\x1b[99;1H", "\x1b[5;1R"},
        {"duplicate-off", "\x1b[?6l\x1b[3;7r\x1b[1;1HDO\x1b[99;1H", "\x1b[20;1R"},
        {"before-exit", "\x1b[?6h\x1b[2;10HA1", "\x1b[2;12R"},
        {"return", "\x1b[?1049l", "\x1b[2;12R"},
        {"later-enter", "\x1b[?1049h", "\x1b[1;1R"},
        {"later-sample", "\x1b[3;7r\x1b[1;1HES\x1b[99;1H", "\x1b[20;1R"},
        {"later-on", "\x1b[?6h\x1b[3;7r\x1b[1;1HEI\x1b[99;1H", "\x1b[5;1R"},
        {"later-off", "\x1b[?6l\x1b[3;7r\x1b[1;1HEO\x1b[99;1H", "\x1b[20;1R"},
        {"final-return", "\x1b[?1049l", "\x1b[2;12R"},
        {"reset", "\x1b[?6l\x1b[?7h\x1b[r\x1b[1;1HDONE", "\x1b[1;5R"},
    };
    Parser parser(20, 91, 100, mode);
    const auto feed = [&](std::string_view source) {
        if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
        else parser.feed(source);
    };
    std::string failures;
    for (size_t phase = 0; phase < std::size(trace); ++phase) {
        feed(trace[phase].source); feed("\x1b[6n");
        if (parser.take_replies() != std::vector<std::string>{trace[phase].reply})
            failures += std::string(trace[phase].label) + " DSR differs; ";
        if (phase == 3 || phase == 4 || phase == 5 || phase == 9 || phase == 10 || phase == 11) {
            const auto row = phase == 4 || phase == 10 ? 2u : 0u;
            const char* marker = phase == 3 ? "DS" : phase == 4 ? "DI" : phase == 5 ? "DO" :
                                 phase == 9 ? "ES" : phase == 10 ? "EI" : "EO";
            if (project_line(parser.screen.visible_lines()[row]).text.substr(0, 2) != marker)
                failures += std::string(trace[phase].label) + " marker differs; ";
        }
    }
    parser.finish();
    if (!failures.empty()) throw std::runtime_error(failures);
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
        run(name + " observed region text readers", [&] { observed_region_text_readers(mode, false); });
        run(name + " bytewise region text readers", [&] { observed_region_text_readers(mode, true); });
        size_t cases = 0;
        run(name + " chunk invariance", [&] { chunk_invariance(mode, &cases); });
        std::cout << "  " << name << " chunk cases: " << cases << "\n";
        run(name + " origin/autowrap independence", [&] { origin_autowrap_independence(mode); });
        run(name + " origin/autowrap region wrap", [&] { origin_autowrap_newline_is_region_bounded(mode); });
        run(name + " region-bottom wrap successor", [&] { region_bottom_wrap_keeps_its_successor(mode, false); });
        run(name + " bytewise region-bottom wrap successor", [&] { region_bottom_wrap_keeps_its_successor(mode, true); });
        run(name + " origin/margin resize policy", [&] { origin_margin_resize_both_frames(mode, false); });
        run(name + " bytewise origin/margin resize policy", [&] { origin_margin_resize_both_frames(mode, true); });
        run(name + " ris origin reset", [&] { ris_resets_origin(mode); });
        run(name + " observed re-entry", [&] { observed_origin_reentry(mode, false); });
        run(name + " bytewise observed re-entry", [&] { observed_origin_reentry(mode, true); });
        for (const bool main_on : {false, true}) {
            const auto trace = name + (main_on ? " observed main-on/alt-off" : " observed main-off/alt-on");
            run(trace, [&] { observed_independent_origins(mode, main_on, false); });
            run(trace + " bytewise", [&] { observed_independent_origins(mode, main_on, true); });
        }
    }
    run("recovery abnormal sequences", abnormal_origin_sequences);
    std::cout << "DECOM origin checks: " << (checks - failures) << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
