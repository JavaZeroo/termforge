// Hand-authored content/identity/viewport expectations; not a second parser.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_projection.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <set>
using namespace termforge;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::string text(const Line& line) { return project_line(line).text; }
static std::string metadata(const Screen& screen) {
    std::ostringstream out; screen.json(out);
    for (size_t i = 0; i < screen.retained_size(); ++i) {
        const auto& line = screen.retained_line(i);
        out << '/' << line.id << ':' << line.version << ':' << static_cast<int>(line.ending)
            << ':' << line.next_id << ':' << line.wrap_columns << ':' << line.cropped;
        for (const auto& cell : line) out << cell.written;
    }
    return out.str();
}
static void identity_and_versions() {
    Parser p(2, 6, 2);
    const auto first = p.screen.lines[0].id, second = p.screen.lines[1].id;
    check(first != 0 && second > first, "initial identities");
    auto version = p.screen.lines[0].version;
    p.feed("abc"); check(p.screen.lines[0].version > version, "writes update version");
    version = p.screen.lines[0].version;
    p.feed("\rX"); check(text(p.screen.lines[0]) == "Xbc" && p.screen.lines[0].version > version,
                           "CR overwrite invalidates old text without replacing row identity");
    check(p.screen.lines[0].id == first, "overwrite preserves identity");
    version = p.screen.lines[0].version;
    p.feed("\x1b[K"); check(text(p.screen.lines[0]) == "X" && p.screen.lines[0].version > version,
                           "erase updates text version");
    p.feed("e"); version = p.screen.lines[0].version;
    p.feed("\xcc\x81"); check(p.screen.lines[0].version > version, "combining mark updates base row");
    p.feed("\r\nB\r\nC");
    check(p.screen.history[0].id == first && p.screen.lines[0].id == second,
          "scroll keeps existing row identities");
    const auto moved_version = p.screen.history[0].version;
    p.feed("\r\nD");
    check(p.screen.history[0].version == moved_version, "move into history never edits content");
    p.feed("\r\nE");
    check(p.screen.find_line(first) == nullptr && p.screen.history[0].id == second,
          "evicted ids are absent and not recycled");
    const auto last = p.screen.lines.back().id;
    p.screen.move(0, 0); p.screen.resize(1, 6); p.screen.resize(3, 6);
    check(p.screen.lines[1].id > last && p.screen.lines[2].id > p.screen.lines[1].id,
          "bottom-row clipping and growth never reuse identities");
}
static void endings_and_projection() {
    Parser full(2, 4, 2); full.feed("abcd");
    check(full.screen.wrap_pending && full.screen.lines[0].ending == LineEnding::none,
          "full row with delayed wrap is not a soft line ending");
    full.feed("\r\n");
    check(full.screen.lines[0].ending == LineEnding::hard_break && !continues_into(full.screen.lines[0], full.screen.lines[1]),
          "explicit CRLF after full row is hard newline");
    Parser soft(3, 4, 2); soft.feed("abcdE");
    check(continues_into(soft.screen.lines[0], soft.screen.lines[1]) && soft.screen.lines[0].wrap_columns == 4,
          "actual delayed wrap records successor and used cells");
    check(text(soft.screen.lines[0]) + text(soft.screen.lines[1]) == "abcdE", "soft row projection");
    Parser wide(2, 4, 2); wide.feed("abc\xe4\xb8\xad" "e\xcc\x81 ");
    check(wide.screen.lines[0].ending == LineEnding::soft_wrap && wide.screen.lines[0].wrap_columns == 3,
          "wide scalar wraps before unused final cell");
    auto projected = project_line(wide.screen.lines[1]);
    check(text(wide.screen.lines[0]) == "abc" && projected.text == "\xe4\xb8\xad" "e\xcc\x81 ",
          "explicit trailing spaces survive while wide-wrap gap is omitted");
    check(projected.spans.size() == 3 && projected.spans[0].column_begin == 0 && projected.spans[0].column_end == 2 &&
          projected.spans[0].byte_end == 3 && projected.spans[1].byte_begin == 3 && projected.spans[1].byte_end == 6,
          "UTF8 offsets map wide and combining text to whole cells");
    Parser erased(2, 4); erased.feed("abcdE\x1b[1;1H\x1b[2K");
    check(erased.screen.lines[0].ending == LineEnding::none && text(erased.screen.lines[0]).empty(),
          "erasing row end invalidates continuation");
    const auto identity = soft.screen.lines[0].id, version = soft.screen.lines[0].version;
    soft.screen.resize(3, 3);
    check(soft.screen.lines[0].cropped && soft.screen.lines[0].id == identity && soft.screen.lines[0].version > version &&
          !continues_into(soft.screen.lines[0], soft.screen.lines[1]), "resize clipping explicitly breaks logical continuity");
    soft.screen.resize(3, 8);
    check(text(soft.screen.lines[0]) == "abc" && soft.screen.lines[0].cropped, "growth cannot restore cropped bytes");
    Line unrelated = soft.screen.lines[1]; unrelated.id += 1000;
    check(!continues_into(soft.screen.lines[0], unrelated), "continuation requires exact successor");
}
static void overwritten_wrap_gap() {
    Parser p(2, 4, 2);
    p.feed("abc\xe4\xb8\xad");
    const auto identity = p.screen.lines[0].id, version = p.screen.lines[0].version;
    p.feed("\x1b[1;4HX");
    check(text(p.screen.lines[0]) == "abcX" && text(p.screen.lines[1]) == "\xe4\xb8\xad",
          "writing the former wide-wrap gap must remain visible in text projection");
    check(p.screen.lines[0].id == identity && p.screen.lines[0].version > version &&
          continues_into(p.screen.lines[0], p.screen.lines[1]),
          "cursor-addressed edits change content without inventing or removing a wrap event");
    const auto projected = project_line(p.screen.lines[0]);
    check(projected.spans.size() == 4 && projected.spans[3].column_begin == 3 &&
          projected.spans[3].column_end == 4 && projected.spans[3].byte_begin == 3 &&
          projected.spans[3].byte_end == 4, "rewritten gap retains its byte-to-cell mapping");
}
static void wrap_projection_across_chunks() {
    struct Case {
        const char* input;
        const char* first;
        const char* untrimmed;
        bool continues;
    };
    // Literal expectations include spaces that are visible or explicitly
    // written. They do not reuse another parser/projection as an oracle.
    const Case cases[] = {
        {"abc\xe4\xb8\xad", "abc", "abc", true},
        {"abc\xe4\xb8\xad\x1b[1;4HX", "abcX", "abcX", true},
        {"abc\xe4\xb8\xad\x1b[1;4H ", "abc ", "abc ", true},
        {"abc\xe4\xb8\xad\x1b[1;3H\xe7\x95\x8c\xcc\x81",
         "ab\xe7\x95\x8c\xcc\x81", "ab\xe7\x95\x8c\xcc\x81", true},
        {"abcd\x1b[1;4H\xe4\xb8\xad", "abcd", "abcd", true},
        {"abc\xe4\xb8\xad\x1b[1;4HX\x1b[1;2H\x1b[1K", "  cX", "  cX", true},
        {"abc\xe4\xb8\xad\x1b[1;4HX\x1b[1;4H\x1b[K", "abc", "abc ", false},
        {"abc\xe4\xb8\xad\x1b[1;4HX\x1b[2K", "", "    ", false},
        {"abc\xe4\xb8\xad\x1b[1;4HX\r\n", "abcX", "abcX", false},
    };
    for (const auto& item : cases) for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
        const std::string_view input(item.input);
        for (size_t chunk = 1; chunk <= input.size(); ++chunk) {
            Parser p(2, 4, 2, mode);
            for (size_t i = 0; i < input.size(); i += chunk) p.feed(input.substr(i, chunk));
            p.finish();
            check(text(p.screen.lines[0]) == item.first &&
                  project_line(p.screen.lines[0], false).text == item.untrimmed &&
                  text(p.screen.lines[1]) == "\xe4\xb8\xad",
                  "wrap/edit/erase projection differs from literal text across input chunks");
            check(continues_into(p.screen.lines[0], p.screen.lines[1]) == item.continues,
                  "partial erase preserves continuation; end erase or explicit LF removes it");
        }
    }
}
static void resized_wrap_projection() {
    Parser grown(2, 4, 2); grown.feed("abc\xe4\xb8\xad");
    const auto before = metadata(grown.screen);
    grown.screen.resize(2, 4);
    check(metadata(grown.screen) == before, "no-op resize preserves wrap metadata");
    grown.screen.resize(2, 7);
    check(project_line(grown.screen.lines[0], false).text == "abc" &&
          continues_into(grown.screen.lines[0], grown.screen.lines[1]),
          "growth adds padding without changing the observed wrap boundary");
    grown.feed("\x1b[1;7HZ");
    check(text(grown.screen.lines[0]) == "abc   Z" && text(grown.screen.lines[1]) == "\xe4\xb8\xad" &&
          continues_into(grown.screen.lines[0], grown.screen.lines[1]),
          "writing beyond a former row width stays visible without reflow");
    grown.feed("\x1b[1;7H ");
    check(text(grown.screen.lines[0]) == "abc    ", "explicit spaces in grown columns survive projection");
    grown.feed("\x1b[1;7H\x1b[K");
    check(text(grown.screen.lines[0]) == "abc" && !continues_into(grown.screen.lines[0], grown.screen.lines[1]),
          "erasing the grown row end drops the continuation and unused padding");

    Parser clipped(2, 4, 2); clipped.feed("abc\xe4\xb8\xad\x1b[1;3H\xe7\x95\x8c");
    clipped.screen.resize(2, 3);
    check(text(clipped.screen.lines[0]) == "ab" && project_line(clipped.screen.lines[0], false).text == "ab " &&
          !continues_into(clipped.screen.lines[0], clipped.screen.lines[1]),
          "physical clipping removes the whole wide glyph and invalidates continuation");
    clipped.screen.resize(2, 6); clipped.feed("\x1b[1;6HY");
    check(text(clipped.screen.lines[0]) == "ab   Y" && text(clipped.screen.lines[1]) == "\xe4\xb8\xad" &&
          !continues_into(clipped.screen.lines[0], clipped.screen.lines[1]),
          "growth and later writes neither restore cropped text nor infer continuity");

    Parser partial(2, 4, 2); partial.feed("abc\xe4\xb8\xad\x1b[1;");
    partial.screen.resize(2, 6); partial.feed("5H\xe7");
    partial.screen.resize(2, 7); partial.feed("\x95\x8c"); partial.finish();
    const auto projected = project_line(partial.screen.lines[0]);
    check(projected.text == "abc \xe7\x95\x8c" && projected.spans.size() == 5 &&
          projected.spans.back().column_begin == 4 && projected.spans.back().column_end == 6 &&
          projected.spans.back().byte_begin == 4 && projected.spans.back().byte_end == 7 &&
          continues_into(partial.screen.lines[0], partial.screen.lines[1]),
          "resize during partial CSI/UTF8 preserves edited wide text and coordinate spans");
}
static void viewport_content() {
    Parser p(3, 6, 4); HistoryViewport view, hidden;
    for (int i = 0; i < 6; ++i) p.feed("L" + std::to_string(i) + "\r\n");
    auto live = view.snapshot(p.screen);
    check(live.viewport.top == 4 && live.viewport.total == 7 && live.viewport.page == 3 &&
          live.viewport.following && text(live.lines[0]) == "L4" && text(live.lines[1]) == "L5" &&
          text(live.lines[2]).empty() && live.cursor_row == 2, "live snapshot content and scroll range");
    view.scroll_by(p.screen, -2); hidden.scroll_to(p.screen, 3);
    const auto anchor = view.anchor(), hidden_anchor = hidden.anchor();
    auto reading = view.snapshot(p.screen);
    check(text(reading.lines[0]) == "L2" && text(reading.lines[2]) == "L4" && reading.cursor_row == -1,
          "historical viewport has correct lines and no unrelated caret");
    p.feed("L6\r\n");
    reading = view.snapshot(p.screen);
    check(reading.viewport.top == 1 && reading.viewport.anchor == anchor && text(reading.lines[0]) == "L2",
          "new output preserves reading anchor as deque indices shift");
    check(hidden.reconcile(p.screen).anchor == hidden_anchor && text(hidden.snapshot(p.screen).lines[0]) == "L3",
          "hidden session view reconciles from independent saved identity");
    p.feed("L7\r\nL8\r\n");
    reading = view.snapshot(p.screen);
    check(reading.viewport.top == 0 && reading.viewport.anchor != anchor && text(reading.lines[0]) == "L3" &&
          !reading.viewport.following, "evicted reading anchor clamps oldest retained line and stays paused");
    check(text(live.lines[0]) == "L4", "snapshot owns immutable copy across output and eviction");
    auto state = view.scroll_by(p.screen, (std::numeric_limits<int64_t>::max)());
    check(state.following && state.top == state.maximum_top, "scroll down saturates and resumes follow");
    state = view.scroll_by(p.screen, (std::numeric_limits<int64_t>::min)());
    check(!state.following && state.top == 0, "scroll up saturates without signed overflow");
    const auto resize_anchor = state.anchor;
    p.screen.resize(2, 4); reading = view.snapshot(p.screen);
    check(reading.lines.size() == 2 && reading.lines.front().size() == 4 && text(reading.lines[0]) == "L4" &&
          reading.viewport.anchor != resize_anchor, "resize scroll and eviction choose surviving anchor");
    p.screen.resize(5, 9); reading = view.snapshot(p.screen);
    check(reading.lines.size() == 5 && text(reading.lines[0]) == "L4", "row and column growth preserve surviving anchor");
    view.bottom(p.screen); reading = view.snapshot(p.screen);
    check(reading.viewport.following && reading.viewport.top == p.screen.history.size() && reading.cursor_row == p.screen.row,
          "explicit bottom returns live cursor and latest viewport");
    Parser no_history(1, 3, 0); HistoryViewport empty;
    no_history.feed("A\r\nB");
    check(empty.scroll_by(no_history.screen, -100).following && text(empty.snapshot(no_history.screen).lines[0]) == "B",
          "empty history always has valid live page");
}
static void boundaries() {
    const std::string bytes = "ABCDE\xe4\xb8\xad" "e\xcc\x81\rX\x1b[K\r\nrow2\r\nrow3\r\nrow4\r\nlast";
    Parser expected(3, 5, 2); expected.feed(bytes); expected.finish();
    for (size_t chunk = 1; chunk <= bytes.size() + 1; ++chunk) {
        Parser actual(3, 5, 2);
        for (size_t i = 0; i < bytes.size(); i += chunk) actual.feed(std::string_view(bytes).substr(i, chunk));
        actual.finish(); check(metadata(actual.screen) == metadata(expected.screen), "metadata depends on byte chunk boundaries");
    }
    Parser recovery(3, 5, 2, ParseMode::recovery); recovery.feed(bytes); recovery.finish();
    check(metadata(recovery.screen) == metadata(expected.screen), "common subset recovery changes history metadata");
    Parser bounded(2, 12, 7); HistoryViewport view;
    for (int i = 0; i < 5000; ++i) {
        bounded.feed(std::to_string(i) + "\r\n");
        if (i == 5) view.scroll_to(bounded.screen, 0);
        const auto state = view.reconcile(bounded.screen);
        check(bounded.screen.history.size() <= 7 && state.total <= 9 && state.top <= state.maximum_top,
              "bounded retention and viewport range under continuous output");
    }
    std::set<uint64_t> identities;
    for (size_t i = 0; i < bounded.screen.retained_size(); ++i) {
        const auto& line = bounded.screen.retained_line(i);
        check(identities.insert(line.id).second && line.id > 4990, "no identity reuse after sustained eviction");
    }
    check(text(bounded.screen.history.front()) == "4992" && text(bounded.screen.lines.front()) == "4999",
          "independent retained-number oracle");
    const auto before = metadata(bounded.screen);
    bool rejected = false; try { bounded.screen.resize(0, 1); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && before == metadata(bounded.screen), "invalid resize leaves metadata unchanged");
}
int main() {
    try {
        identity_and_versions(); endings_and_projection(); overwritten_wrap_gap();
        wrap_projection_across_chunks(); resized_wrap_projection(); viewport_content(); boundaries();
        std::cout << "history identity, versions, line endings, projection, anchors and bounds passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
