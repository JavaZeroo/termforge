// Hand-authored rank/order expectations. A row identity is not a document
// position: a larger id may sit earlier in the retained sequence.
#include "history_view.hpp"
#include "screen.hpp"
#include "text_document.hpp"
#include "text_search.hpp"
#include "text_selection.hpp"
#include "terminal.hpp"
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace termforge;

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

static Line row(int columns, uint64_t identity, char text) {
    Line line(static_cast<size_t>(columns), Cell{});
    line.id = identity;
    line.version = 1;
    line[0] = {std::string(1, text), 1, Style{}, 0, true};
    return line;
}

static std::vector<Line> rows(int columns, std::initializer_list<std::pair<uint64_t, char>> spec) {
    std::vector<Line> result;
    for (const auto& item : spec) result.push_back(row(columns, item.first, item.second));
    return result;
}

// The [1,3,4,5,7,6] shape: two live rows whose ids are not in document order.
static Screen reordered_screen() {
    Screen screen(2, 4, 4);
    screen.publish(rows(4, {{1, 'a'}, {3, 'b'}, {4, 'c'}, {5, 'd'}}),
                   rows(4, {{7, 'x'}, {6, 'x'}}));
    return screen;
}

static void published_order_is_exact() {
    auto screen = reordered_screen();
    check(screen.retained_size() == 6, "published frame keeps every retained row");
    check(screen.locate(7) == std::optional<size_t>(4), "live identity 7 holds rank 4");
    check(screen.locate(6) == std::optional<size_t>(5), "live identity 6 holds rank 5");
    check(screen.locate(5) == std::optional<size_t>(3), "history identity 5 holds rank 3");
    check(!screen.locate(2), "a missing identity is never replaced by a neighbour");
    check(!screen.locate(0), "zero is not an identity");
    // The evicted-anchor fallback stays explicit and separate from exact lookup.
    check(screen.lower_bound_line(2) == 1, "lower bound still resolves an insertion point");
    check(screen.retained_line(*screen.locate(6)).id == 6, "rank resolves back to the identity");
}

static void document_rank_and_compare() {
    auto screen = reordered_screen();
    TextDocument doc(screen);
    check(doc.index_of(7) == std::optional<size_t>(4), "document rank follows the retained order");
    check(doc.index_of(6) == std::optional<size_t>(5), "second reordered row keeps its own rank");
    check(!doc.index_of(2), "document reports a removed identity as missing");
    const auto earlier = doc.position(4, 0), later = doc.position(5, 0);
    check(earlier && later, "positions exist for both live rows");
    check(doc.valid(*earlier) && doc.valid(*later), "published rows validate");
    check(doc.compare(*earlier, *later) == std::optional<int>(-1), "7 precedes 6 in document order");
    check(doc.compare(*later, *earlier) == std::optional<int>(1), "comparison is antisymmetric");
    check(doc.compare(*earlier, *earlier) == std::optional<int>(0), "identical positions compare equal");
}

static void order_revision_tracks_structure_only() {
    Screen screen(2, 4, 4);
    const auto initial = screen.order_revision();
    screen.put('a', "a");
    check(screen.order_revision() == initial, "content writes are not structure changes");
    screen.newline();
    check(screen.order_revision() == initial, "a newline that only moves the cursor is not structural");
    screen.newline();
    check(screen.order_revision() > initial, "scrolling the bottom row changes structure");
    const auto scrolled = screen.order_revision();
    screen.resize(3, 4);
    check(screen.order_revision() > scrolled, "physical resize changes structure");
    const auto resized = screen.order_revision();
    screen.resize(3, 4);
    check(screen.order_revision() == resized, "an identical resize publishes nothing");
    screen.activate_buffer(ScreenBuffer::alternate);
    check(screen.order_revision() > resized, "activating a frame changes the document owner");
    const auto published = screen.order_revision();
    screen.publish({}, {row(4, 900, 'z'), row(4, 901, 'w')});
    check(screen.order_revision() > published, "publishing different rows changes structure");
    const auto republished = screen.order_revision();
    screen.publish({}, {row(4, 900, 'z'), row(4, 901, 'w')});
    check(screen.order_revision() == republished, "republishing the same structure publishes nothing");
}

static void publication_keeps_the_cursor_inside_the_frame() {
    Screen screen(3, 4, 4);
    screen.move(2, 3);
    screen.publish({}, {row(4, 40, 'a')});
    check(screen.row == 0 && screen.column == 3, "a shorter published frame clamps only the lost row");
    screen.put('b', "b");
    check(screen.visible_lines()[0][3].text == "b", "writes after a shrinking publication stay in range");
}

static void publication_respects_the_active_history_budget() {
    Screen screen(2, 4, 1);
    bool rejected = false;
    try { screen.publish(rows(4, {{1, 'a'}, {2, 'b'}}), rows(4, {{3, 'c'}, {4, 'd'}})); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "a publication above the active history budget is rejected");
    screen.activate_buffer(ScreenBuffer::alternate);
    bool alternate = false;
    try { screen.publish(rows(4, {{5, 'e'}}), rows(4, {{6, 'f'}, {7, 'g'}})); }
    catch (const std::invalid_argument&) { alternate = true; }
    check(alternate, "the alternate frame accepts no imported history");
}

static void eviction_never_revives_rank() {
    Screen screen(2, 4, 2);
    screen.publish(rows(4, {{10, 'a'}, {11, 'b'}}), rows(4, {{12, 'c'}, {13, 'd'}}));
    check(screen.locate(10) == std::optional<size_t>(0), "evictable history is located before eviction");
    screen.move(1, 0);
    screen.newline();
    screen.newline();
    check(!screen.locate(10), "an evicted identity is not located any more");
    check(screen.locate(13).has_value(), "a surviving identity keeps an exact rank");
    check(screen.lower_bound_line(10) == 0, "the evicted-anchor fallback stays an explicit insertion point");
}

static void copies_keep_their_own_order() {
    auto original = reordered_screen();
    Screen copy = original;
    check(copy.locate(7) == std::optional<size_t>(4) && copy.locate(6) == std::optional<size_t>(5),
          "a copy keeps the published order");
    copy.publish({}, {row(4, 1, 'q'), row(4, 2, 'r')});
    check(original.locate(7) == std::optional<size_t>(4), "changing a copy leaves the original order intact");
    check(copy.locate(7) == std::nullopt, "a republished copy reports its own identities only");
}

static void rejected_publication_commits_nothing() {
    Screen screen(2, 4, 4);
    const auto revision = screen.order_revision();
    const auto retained = screen.retained_size();
    const auto rejected = [&](auto&& call) {
        try { call(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    check(rejected([&] { screen.publish({}, rows(4, {{0, 'a'}, {1, 'b'}})); }), "zero identity rejected");
    check(rejected([&] { screen.publish(rows(4, {{3, 'a'}}), rows(4, {{3, 'b'}, {4, 'c'}})); }),
          "duplicate identity rejected across history and live rows");
    check(rejected([&] { screen.publish({}, rows(3, {{9, 'a'}, {10, 'b'}})); }), "width mismatch rejected");
    check(rejected([&] { screen.publish({}, {}); }), "a frame without a live row is rejected");
    check(screen.order_revision() == revision && screen.retained_size() == retained &&
              screen.locate(screen.retained_line(0).id) == std::optional<size_t>(0),
          "a rejected publication leaves the published structure untouched");
}

static void search_navigation_follows_document_order() {
    auto screen = reordered_screen();
    // Row 4 (id 7) holds "x"; row 5 (id 6) also holds "x". Document order says 7
    // comes first even though 6 < 7.
    TextDocument doc(screen);
    TextSearch search;
    search.start(doc, "x");
    for (int guard = 0; guard < 256 && search.status() == SearchStatus::running; ++guard)
        search.step(doc, search.generation(), 64);
    check(search.matches().size() == 2, "both reordered rows produce a hit");
    const auto anchor = doc.position(5, 0);
    check(anchor.has_value(), "anchor position exists");
    const auto previous = search.navigate(doc, SearchDirection::previous, anchor);
    check(previous && previous->match.range.begin.line_id == 7,
          "previous navigation uses document order, not identity order");
    const auto earlier = doc.position(4, 0);
    const auto next = search.navigate(doc, SearchDirection::next, earlier);
    check(next && next->match.range.begin.line_id == 6, "next navigation reaches the later live row");
    const auto no_wrap = search.navigate_no_wrap(doc, SearchDirection::previous, anchor);
    check(no_wrap && no_wrap->match.range.begin.line_id == 7,
          "no-wrap previous navigation also follows document order");
}

static void viewport_uses_exact_rank() {
    auto screen = reordered_screen();
    HistoryViewport view;
    view.scroll_to(screen, 1);
    check(view.reconcile(screen).anchor == 3, "paused viewport anchors on the row it shows");
    const auto state = view.reveal(screen, 5);
    check(state.top == 3 && state.anchor == 5, "reveal scrolls to the exact published rank");
    check(view.reconcile(screen).top == 3, "the anchor survives reconciliation");
}

// IL/DL already edit physical rows inside a configured region. The owner must
// publish their membership/order change so existing text readers follow the
// surviving rows instead of bisecting newly non-monotonic identities.
static void editing_rows_publish_order() {
    std::string failures;
    for (const bool insert : {true, false}) {
        try {
            Parser parser(6, 4, 2, ParseMode::recovery);
            parser.feed("a\r\nb\r\nc\r\nd\r\ne\r\nf\x1b[2;5r\x1b[3;1H");
            TextDocument document(parser.screen);
            const size_t old_rank = insert ? 2 : 3, new_rank = insert ? 3 : 2;
            const auto old_position = *document.position(old_rank, 0);
            const auto retired = document.meta(insert ? 4 : 2).id;
            TextSelection selection;
            check(selection.set(document, {old_position, *document.position(old_rank, 1)}),
                  "row-edit selection setup failed");
            TextSearch search;
            auto generation = search.start(document, insert ? "c" : "d");
            const auto finish_search = [&] {
                for (int guard = 0; guard < 100 && search.status() == SearchStatus::running; ++guard)
                    search.step(document, generation, 16);
                check(search.status() == SearchStatus::complete, "row-edit search exceeded its work bound");
            };
            finish_search();
            const auto revision = document.order_revision();
            parser.feed(insert ? "\x1b[L" : "\x1b[M");
            check(document.order_revision() > revision, "row edit did not publish its structure revision");
            const std::vector<std::string> expected = insert
                ? std::vector<std::string>{"a", "b", "", "c", "d", "f"}
                : std::vector<std::string>{"a", "b", "d", "e", "", "f"};
            check(document.size() == expected.size() && parser.screen.history_lines().empty(),
                  "row edit changed the retained size or leaked into history");
            for (size_t index = 0; index < document.size(); ++index) {
                check(document.line(index).projection.text == expected[index], "row edit changed the literal row order");
                check(document.index_of(document.meta(index).id) == std::optional<size_t>(index),
                      "row edit made a retained identity unreachable");
            }
            check(!document.index_of(retired) && document.valid(old_position) &&
                  document.index_of(old_position.line_id) == std::optional<size_t>(new_rank),
                  "row edit confused removed and surviving identities");
            const auto copied = selection.copy(document, 8);
            check(copied.status == SelectionCopyStatus::ok && copied.text == (insert ? "c" : "d"),
                  "row edit lost an unchanged selected row");
            generation = search.notify_change(document);
            finish_search();
            const auto match = search.match(document, 0);
            check(match && document.index_of(match->range.begin.line_id) == std::optional<size_t>(new_rank) &&
                  !search.match(document, 1), "row edit left search at the old rank");
            parser.finish();
        } catch (const std::exception& error) {
            failures += std::string(insert ? "IL: " : "DL: ") + error.what() + "; ";
        }
    }
    if (!failures.empty()) throw std::runtime_error(failures);
}

int main() {
    try {
        published_order_is_exact();
        document_rank_and_compare();
        order_revision_tracks_structure_only();
        publication_keeps_the_cursor_inside_the_frame();
        publication_respects_the_active_history_budget();
        eviction_never_revives_rank();
        copies_keep_their_own_order();
        rejected_publication_commits_nothing();
        search_navigation_follows_document_order();
        viewport_uses_exact_rank();
        editing_rows_publish_order();
        std::cout << "exact identity rank, publication and document order passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
