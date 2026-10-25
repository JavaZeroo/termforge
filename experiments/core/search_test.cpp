// Hand-authored literal text and cell-coordinate expectations; no second search engine.
#include "terminal.hpp"
#include "text_search.hpp"
#include <iostream>
#include <stdexcept>
using namespace termforge;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void finish(TextSearch& search, const TextDocument& doc, uint64_t generation, size_t budget = 3) {
    size_t turns = 0;
    while (search.status() == SearchStatus::running) {
        const auto step = search.step(doc, generation, budget);
        check(step.accepted && step.work_used <= budget, "step respects current generation and budget");
        check(++turns < 1000000, "bounded search eventually completes");
    }
}
static void basic_matches() {
    Parser parser(3, 24, 10); parser.feed("one needle\r\nneedle end\r\nnone");
    const TextDocument doc(parser.screen); TextSearch search;
    const auto generation = search.start(doc, "needle"); finish(search, doc, generation);
    const auto& matches = search.matches();
    check(search.status() == SearchStatus::complete && matches.size() == 2, "two literal matches");
    check(matches[0].range.begin.line_id == parser.screen.lines[0].id &&
          matches[0].range.begin.column == 4 && matches[0].range.end.column == 10,
          "first literal match maps to known cells");
    check(matches[1].range.begin.line_id == parser.screen.lines[1].id &&
          matches[1].range.begin.column == 0 && matches[1].range.end.column == 6,
          "second literal match maps to known cells");
}
static void wraps_newlines_and_unicode() {
    Parser soft(3, 4, 2); soft.feed("LEFTRIGHT");
    const TextDocument soft_doc(soft.screen); TextSearch search;
    finish(search, soft_doc, search.start(soft_doc, "LEFTRIGHT"), 1);
    check(search.matches().size() == 1 && search.matches()[0].range.begin.column == 0 &&
          search.matches()[0].range.end.line_id == soft.screen.lines[2].id &&
          search.matches()[0].range.end.column == 1, "literal search crosses actual soft wraps");
    Parser hard(3, 4, 2); hard.feed("LEFT\r\nRIGHT");
    const TextDocument hard_doc(hard.screen);
    finish(search, hard_doc, search.start(hard_doc, "LEFTRIGHT"));
    check(search.matches().empty(), "hard line break never silently joins text");
    finish(search, hard_doc, search.start(hard_doc, "LEFT\nRIGHT"));
    check(search.matches().size() == 1 && search.matches()[0].range.end.column == 1,
          "explicit LF query crosses hard break and following soft wrap");
    Parser unicode(2, 30, 2); unicode.feed("two  \r\n\xe4\xb8\xad\xe6\x96\x87 e\xcc\x81");
    const TextDocument doc(unicode.screen);
    finish(search, doc, search.start(doc, "two  \n\xe4\xb8\xad\xe6\x96\x87 e\xcc\x81"), 1);
    check(search.matches().size() == 1 && search.matches()[0].range.end.column == 6,
          "trailing spaces, CJK and NFD map to exact known cells");
    finish(search, doc, search.start(doc, "\xe4\xb8\xad"));
    check(search.matches().size() == 1 && search.matches()[0].range.begin.column == 0 &&
          search.matches()[0].range.end.column == 2, "CJK match covers both cells");
    finish(search, doc, search.start(doc, "\xcc\x81"));
    check(search.matches().size() == 1 && search.matches()[0].range.begin.column == 5 &&
          search.matches()[0].range.end.column == 6, "combining component maps to its complete base cell");
    finish(search, doc, search.start(doc, "\xc3\xa9"));
    check(search.matches().empty(), "search does not invent Unicode normalization");
}
static void options_and_navigation() {
    Parser parser(2, 40, 2); parser.feed("Needle needle NEEDLE \xc3\x84 \xc3\xa4");
    const TextDocument doc(parser.screen); TextSearch search;
    finish(search, doc, search.start(doc, "needle"));
    check(search.matches().size() == 1 && search.matches()[0].range.begin.column == 7,
          "default matching is case-sensitive");
    finish(search, doc, search.start(doc, "needle", {false, 1024}));
    check(search.matches().size() == 3, "optional ASCII case folding finds known matches");
    auto hit = search.navigate(doc, SearchDirection::previous);
    check(hit && hit->index == 2 && !hit->wrapped, "initial previous chooses last match");
    hit = search.navigate(doc, SearchDirection::next, hit->match.range.begin);
    check(hit && hit->index == 0 && hit->wrapped, "next past last wraps to first");
    hit = search.navigate(doc, SearchDirection::next, hit->match.range.begin);
    check(hit && hit->index == 1 && !hit->wrapped, "next advances strictly by match start");
    hit = search.navigate(doc, SearchDirection::previous, search.matches()[0].range.begin);
    check(hit && hit->index == 2 && hit->wrapped, "previous before first wraps to last");
    finish(search, doc, search.start(doc, "\xc3\xa4", {false, 1024}));
    check(search.matches().size() == 1 && search.matches()[0].range.begin.column == 23,
          "ASCII case option does not silently case-fold other Unicode");
    Parser repeated(1, 10, 2); repeated.feed("aaaaa"); const TextDocument repeated_doc(repeated.screen);
    finish(search, repeated_doc, search.start(repeated_doc, "aa"));
    check(search.matches().size() == 2 && search.matches()[1].range.begin.column == 2,
          "non-overlap policy is deterministic");
}
static void generations_and_bounds() {
    Parser parser(3, 40, 3); parser.feed("a a a a\r\nlast");
    const TextDocument doc(parser.screen); TextSearch search;
    const auto old = search.start(doc, "a");
    const auto zero = search.step(doc, old, 0);
    check(zero.accepted && zero.work_used == 0 && search.matches().empty(), "zero budget does no work");
    const auto replacement = search.start(doc, "last");
    check(!search.step(doc, old, 100).accepted && search.matches().empty(), "replaced query rejects old work");
    finish(search, doc, replacement, 1); check(search.matches().size() == 1, "replacement returns only new results");
    search.cancel(); check(search.matches().empty() && search.status() == SearchStatus::cancelled &&
                          !search.step(doc, replacement, 100).accepted, "cancel clears results and old work");
    const auto limited = search.start(doc, "a", {true, 2}); finish(search, doc, limited);
    check(search.status() == SearchStatus::capped && search.matches().size() == 2,
          "configured result cap stops scanning explicitly");
    parser.feed("\x1b[1;1Hb b b b"); finish(search, doc, search.notify_change(doc), 1);
    check(search.status() == SearchStatus::complete && search.matches().size() == 1 && search.match(doc, 0),
          "capped partial row is revalidated, replaced matches disappear, tail scan resumes");
    search.close(); check(search.retained_rows() == 0 && search.matches().empty() &&
                         search.status() == SearchStatus::closed, "close releases task state");
    finish(search, doc, search.start(doc, ""));
    check(search.matches().empty() && !search.navigate(doc, SearchDirection::next), "empty query returns no hits");
    for (const auto& query : {std::string(4097, 'x'), std::string("\x80"), std::string("\xc0\x80"),
                             std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8")}) {
        bool rejected = false; try { search.start(doc, query); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "oversized or malformed UTF-8 query is rejected");
    }
    for (const auto limit : {size_t{0}, size_t{4097}}) {
        bool rejected = false; try { search.start(doc, "x", {true, limit}); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "result limits must remain within explicit hard bound");
    }
}
static size_t visible_hits(const TextSearch& search, const TextDocument& doc) {
    size_t count = 0;
    for (size_t i = 0; i < search.matches().size(); ++i) if (search.match(doc, i)) ++count;
    return count;
}
static void changing_text_and_eviction() {
    Parser parser(2, 20, 2); parser.feed("needle\r\nlast");
    const TextDocument doc(parser.screen); TextSearch search;
    finish(search, doc, search.start(doc, "needle"), 1);
    check(visible_hits(search, doc) == 1, "initial live text has one match");
    const auto old_generation = search.generation();
    parser.feed("\x1b[1;1Hxxxxxx");
    const auto changed = search.notify_change(doc);
    check(!search.match(doc, 0) && !search.navigate(doc, SearchDirection::next),
          "overwritten hit is hidden immediately, before the metadata audit");
    check(!search.step(doc, old_generation, 100).accepted, "mutation rejects old-generation scheduled work");
    finish(search, doc, changed, 1); check(search.matches().empty(), "overwrite removes stale match");
    parser.feed("\x1b[1;1Hneedle"); finish(search, doc, search.notify_change(doc), 1);
    check(visible_hits(search, doc) == 1, "changed live row is rescanned for new match");
    parser.feed("\x1b[2;5H\r\nnew needle"); finish(search, doc, search.notify_change(doc), 1);
    check(visible_hits(search, doc) == 2, "new output retains prefix hit and adds tail match");
    parser.feed("\r\nB\r\nC\r\nD\r\nE"); search.notify_change(doc);
    check(!search.match(doc, 0), "history eviction hides removed identity immediately");
    finish(search, doc, search.generation(), 1);
    check(search.matches().empty() && search.retained_rows() <= doc.size(), "evicted metadata and matches are reclaimed");
    parser.feed("\r\nneedle"); finish(search, doc, search.notify_change(doc), 1);
    check(visible_hits(search, doc) == 1, "search resumes after rolling history rather than reusing stale identity");

    Parser across(3, 3, 4); across.feed("ABCDEFghi"); const TextDocument across_doc(across.screen);
    finish(search, across_doc, search.start(across_doc, "ABCDEFghi"));
    check(visible_hits(search, across_doc) == 1, "initial three-row literal match");
    across.feed("\x1b[2;1HXYZ"); search.notify_change(across_doc);
    check(across_doc.valid(search.matches()[0].range.begin) && across_doc.valid(search.matches()[0].range.end) &&
          !search.match(across_doc, 0), "interior row versions invalidate hit even when both endpoint versions survive");
    finish(search, across_doc, search.generation(), 1);
    check(search.matches().empty(), "interior rewrite clears cross-row match");
    across.screen.resize(3, 2); finish(search, across_doc, search.notify_change(across_doc), 1);
    check(search.matches().empty(), "physical clipping cannot preserve old soft-wrap search match");

    Parser wider(2, 8, 4); wider.feed("needle\r\nother"); const TextDocument wider_doc(wider.screen);
    finish(search, wider_doc, search.start(wider_doc, "needle"), 1);
    check(visible_hits(search, wider_doc) == 1, "initial hard-row hit before widening");
    wider.screen.resize(2, 16); search.notify_change(wider_doc);
    check(!search.match(wider_doc, 0), "default physical resize invalidates old position versions");
    finish(search, wider_doc, search.generation(), 1);
    check(visible_hits(search, wider_doc) == 1, "incremental search refreshes a physically resized hard-row hit");

    Parser moved(3, 8, 2); moved.feed("needle\r\nlast\r\nthird"); const TextDocument moved_doc(moved.screen);
    finish(search, moved_doc, search.start(moved_doc, "needle"), 1);
    const auto retained_id = moved.screen.lines[0].id, removed_id = moved.screen.lines[1].id;
    moved.feed("\x1b[1;1H"); moved.screen.resize(1, 8);
    moved.screen.resize(2, 8); moved.feed("\x1b[2;1H"); moved.screen.resize(1, 8);
    search.notify_change(moved_doc);
    check(moved_doc.index_of(retained_id) == std::optional<size_t>(0) && !moved_doc.index_of(removed_id) &&
          moved_doc.line(0).projection.text == "needle" && moved_doc.size() == 2,
          "height clipping removes the successor but retains the target in history");
    check(search.match(moved_doc, 0) && search.match(moved_doc, 0)->range.begin.line_id == retained_id,
          "successor removal does not invalidate a retained hard-row hit");
    finish(search, moved_doc, search.generation(), 1);
    check(visible_hits(search, moved_doc) == 1, "successor removal retains the audited target hit");
}
static void continuous_output_keeps_progress() {
    Parser parser(2, 40, 100); parser.feed("needle stable\r\n");
    for (int i = 0; i < 25; ++i) parser.feed("log body\r\n");
    const TextDocument doc(parser.screen); TextSearch search;
    search.start(doc, "needle");
    bool found_while_outputting = false;
    for (int tick = 0; tick < 2000; ++tick) {
        parser.feed(tick % 2 ? "\rtail-B" : "\rtail-A");
        const auto generation = search.notify_change(doc);
        const auto progress = search.step(doc, generation, 1);
        check(progress.accepted && progress.work_used <= 1, "tiny budget survives continuous output notifications");
        found_while_outputting = found_while_outputting || visible_hits(search, doc) == 1;
    }
    check(found_while_outputting, "stable history match becomes visible while every tick mutates live output");
    finish(search, doc, search.generation(), 1);
    check(search.status() == SearchStatus::complete && visible_hits(search, doc) == 1,
          "search converges once output stops, without duplicate prefix hits");
    // Now the history is full and rolls over repeatedly; successful progress
    // must not depend on starting a fresh whole-history scan on every append.
    Parser rolling(2, 20, 10); const TextDocument rolling_doc(rolling.screen);
    search.start(rolling_doc, "needle");
    for (int tick = 0; tick < 100; ++tick) {
        rolling.feed(tick % 5 == 0 ? "needle\r\n" : "log\r\n");
        search.step(rolling_doc, search.notify_change(rolling_doc), 128);
        check(search.retained_rows() <= 12, "metadata storage remains bounded while history rolls");
    }
    finish(search, rolling_doc, search.generation(), 2);
    check(visible_hits(search, rolling_doc) == 2, "rolling retained window has independently expected two matches");
}
static void incremental_non_overlap_phase() {
    Parser parser(3, 3, 3); parser.feed("aaaaaaaaa"); const TextDocument doc(parser.screen); TextSearch search;
    finish(search, doc, search.start(doc, "aa"), 1);
    check(search.matches().size() == 4, "nine repeated bytes produce four non-overlapping matches");
    parser.feed("\x1b[3;1Haab"); finish(search, doc, search.notify_change(doc), 1);
    check(search.matches().size() == 4, "incremental rewind preserves original non-overlap phase");
    const auto& matches = search.matches();
    check(matches[2].range.begin.line_id == parser.screen.lines[1].id && matches[2].range.begin.column == 1 &&
          matches[3].range.begin.line_id == parser.screen.lines[2].id && matches[3].range.begin.column == 0,
          "tail mutation does not shift valid earlier or later matches by a byte");
    Parser rolling(2, 3, 1); rolling.feed("aaaaaaaaaa"); const TextDocument rolling_doc(rolling.screen);
    finish(search, rolling_doc, search.start(rolling_doc, "aa"), 1);
    rolling.feed("aaa"); finish(search, rolling_doc, search.notify_change(rolling_doc), 1);
    check(search.matches().size() == 3 && search.matches()[0].range.begin.line_id == rolling.screen.history[0].id &&
          search.matches()[0].range.begin.column == 0,
          "eviction restarts a partial matching prefix at the new retained document boundary");
}
static void long_query_and_partial_row_replacement() {
    Parser parser(10, 500, 3); parser.feed(std::string(4096, 'x'));
    const TextDocument doc(parser.screen); TextSearch search;
    const auto long_generation = search.start(doc, std::string(4096, 'x'));
    size_t bytes = 0;
    while (search.status() == SearchStatus::running) {
        const auto progress = search.step(doc, long_generation, 7);
        check(progress.work_used <= 7 && progress.bytes_scanned <= progress.work_used,
              "maximum query obeys byte work budget across many soft rows");
        bytes += progress.bytes_scanned;
    }
    check(bytes == 4097 && search.matches().size() == 1 &&
          search.matches()[0].range.end.line_id == parser.screen.lines[8].id &&
          search.matches()[0].range.end.column == 96, "maximum query matches 4096 known bytes across nine rows");
    // Cancel after loading just one row; no residual UTF-8 or KMP origin from
    // that partial query may become a result of the replacement generation.
    const auto old = search.start(doc, "xx"); search.step(doc, old, 2);
    const auto replacement = search.start(doc, "not-present");
    check(!search.step(doc, old, 10000).accepted, "partial row of replaced query cannot publish");
    finish(search, doc, replacement, 5);
    check(search.matches().empty(), "query replacement discards partial prefix and mapped origins");
}
static void growing_retained_document_adds_separator() {
    Parser parser(1, 8, 4); parser.feed("ONE"); const TextDocument doc(parser.screen); TextSearch search;
    finish(search, doc, search.start(doc, "ONETWO"), 1);
    check(search.matches().empty(), "initial one-row document has no combined match");
    parser.screen.resize(2, 8); parser.feed("\x1b[2;1HTWO");
    finish(search, doc, search.notify_change(doc), 1);
    check(search.matches().empty(), "height growth does not join former last row with unrelated new row");
    finish(search, doc, search.start(doc, "ONE\nTWO"), 1);
    check(search.matches().size() == 1, "grown document has the explicit physical-row LF boundary");
}
static void explicit_partial_navigation_during_continuous_append() {
    Parser parser(7, 112, 100); const TextDocument doc(parser.screen); TextSearch search;
    search.start(doc, "needle", {true, 1024});
    for (int tick = 0; tick < 250; ++tick) {
        std::string bytes;
        for (int row = 0; row < 64; ++row) bytes += "TF_FLOOD needle " + std::string(75, 'x') + "\r\n";
        parser.feed(bytes); search.notify_change(doc);
        const auto progress = search.step(doc, search.generation(), 4096);
        check(progress.status == SearchStatus::running && progress.work_used <= 4096,
              "continuous append remains an explicitly incomplete scan within the work budget");
        const auto first = search.navigate_no_wrap(doc, SearchDirection::next);
        const auto last = search.navigate_no_wrap(doc, SearchDirection::previous);
        check(first && last && !first->wrapped && !last->wrapped,
              "explicit navigation can use live partial hits before continuous output stops");
        check(first->match.range.begin.column == 9 && first->match.range.end.column == 15 &&
              doc.valid(first->match.range.begin), "partial hit maps to the literal known live cells");
        check(!search.navigate_no_wrap(doc, SearchDirection::previous, first->match.range.begin) &&
              !search.navigate_no_wrap(doc, SearchDirection::next, last->match.range.begin),
              "partial prefix boundaries do not masquerade as a full-document wrap");
    }
    finish(search, doc, search.generation(), 4096);
    const auto last = search.navigate(doc, SearchDirection::previous);
    const auto wrapped = last ? search.navigate(doc, SearchDirection::next, last->match.range.begin) : std::nullopt;
    check(last && wrapped && wrapped->wrapped,
          "completed search keeps the separately verified normal wrapping contract");
}
int main() {
    try { basic_matches(); wraps_newlines_and_unicode(); options_and_navigation(); generations_and_bounds();
          changing_text_and_eviction(); continuous_output_keeps_progress(); incremental_non_overlap_phase();
          long_query_and_partial_row_replacement(); growing_retained_document_adds_separator();
          explicit_partial_navigation_during_continuous_append();
          std::cout << "search core checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
