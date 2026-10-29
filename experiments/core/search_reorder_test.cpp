// Reordered documents must not corrupt incremental search: a row identity is
// not a document position, so a structure change has to repair from rank.
#include "screen.hpp"
#include "text_document.hpp"
#include "text_search.hpp"
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace termforge;

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

static Line row(int columns, uint64_t identity, const std::string& text) {
    Line line(static_cast<size_t>(columns), Cell{});
    line.id = identity; line.version = 1;
    for (size_t i = 0; i < text.size() && i < line.size(); ++i)
        line[i] = {std::string(1, text[i]), 1, Style{}, 0, true};
    return line;
}
static std::vector<Line> rows(int columns, std::initializer_list<std::pair<uint64_t, std::string>> spec) {
    std::vector<Line> result;
    for (const auto& item : spec) result.push_back(row(columns, item.first, item.second));
    return result;
}
static void drain(TextSearch& search, const TextDocument& doc) {
    for (int guard = 0; guard < 20000 && search.status() == SearchStatus::running; ++guard)
        search.step(doc, search.generation(), 7);
    check(search.status() != SearchStatus::running, "bounded scan terminates");
}
static std::vector<size_t> ranks(const TextSearch& search, const TextDocument& doc) {
    std::vector<size_t> result;
    for (const auto& hit : search.matches()) {
        const auto rank = doc.index_of(hit.range.begin.line_id);
        check(rank.has_value(), "every hit stays on a live row");
        result.push_back(*rank);
    }
    return result;
}

// A reorder after the scan started must neither duplicate nor drop hits, and
// results must stay in document order.
static void reorder_mid_scan_keeps_rank_order() {
    Screen screen(6, 8, 4);
    screen.publish({}, rows(8, {{1, "needle0"}, {2, "needle1"}, {3, "needle2"},
                                {4, "needle3"}, {5, "needle4"}, {6, "needle5"}}));
    TextDocument document(screen);
    TextSearch search;
    search.start(document, "needle");
    for (int index = 0; index < 6 && search.status() == SearchStatus::running; ++index)
        search.step(document, search.generation(), 5);
    // Same identities, different document order: 6 now precedes 5.
    screen.publish({}, rows(8, {{1, "needle0"}, {2, "needle1"}, {3, "needle2"},
                                {4, "needle3"}, {6, "needle5"}, {5, "needle4"}}));
    search.notify_change(document);
    drain(search, document);
    check(search.matches().size() == 6, "six rows keep exactly one hit each");
    const auto order = ranks(search, document);
    for (size_t index = 1; index < order.size(); ++index)
        check(order[index - 1] < order[index], "hits are ordered by document rank");
    std::vector<uint64_t> ids;
    for (const auto& hit : search.matches()) ids.push_back(hit.range.begin.line_id);
    std::sort(ids.begin(), ids.end());
    check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "no hit is emitted twice");
    for (size_t index = 0; index < search.matches().size(); ++index)
        check(search.match(document, index).has_value(), "live validation accepts every hit");
}

// Navigation compares document ranks, so a reorder changes next/previous.
static void navigation_follows_the_new_order() {
    Screen screen(6, 8, 4);
    screen.publish({}, rows(8, {{1, "needle0"}, {2, "needle1"}, {3, "needle2"},
                                {4, "needle3"}, {5, "needle4"}, {6, "needle5"}}));
    TextDocument document(screen);
    TextSearch search;
    search.start(document, "needle");
    drain(search, document);
    screen.publish({}, rows(8, {{1, "needle0"}, {2, "needle1"}, {4, "needle3"},
                                {5, "needle4"}, {6, "needle5"}, {3, "needle2"}}));
    search.notify_change(document);
    drain(search, document);
    const auto anchor = document.position(2, 0);
    check(anchor.has_value(), "anchor position exists");
    const auto next = search.navigate(document, SearchDirection::next, anchor);
    check(next.has_value(), "a later hit exists");
    const auto next_rank = document.index_of(next->match.range.begin.line_id);
    check(next_rank.has_value() && *next_rank > 2, "next moves forward in document order");
    const auto previous = search.navigate(document, SearchDirection::previous, next->match.range.begin);
    check(previous.has_value(), "previous returns a hit");
    const auto previous_rank = document.index_of(previous->match.range.begin.line_id);
    check(previous_rank.has_value() && *previous_rank < *document.index_of(next->match.range.begin.line_id),
          "previous moves back in document order");
}

// Eviction removes rows; hits on them must disappear without a full restart.
static void eviction_removes_only_dead_hits() {
    Screen screen(2, 8, 2);
    screen.publish(rows(8, {{10, "needleA"}, {11, "needleB"}}),
                   rows(8, {{12, "needleC"}, {13, "needleD"}}));
    TextDocument document(screen);
    TextSearch search;
    search.start(document, "needle");
    drain(search, document);
    check(search.matches().size() == 4, "four hits before eviction");
    screen.move(1, 0);
    screen.newline();
    screen.newline();
    search.notify_change(document);
    drain(search, document);
    for (const auto& hit : search.matches()) {
        check(document.index_of(hit.range.begin.line_id).has_value(), "evicted hit removed");
        check(document.index_of(hit.range.end.line_id).has_value(), "evicted hit end removed");
    }
}

// The ordered case keeps its previous behaviour and result order.
static void ordered_document_is_unchanged() {
    Screen screen(3, 8, 2);
    screen.publish({}, rows(8, {{1, "needleA"}, {2, "plain"}, {3, "needleB"}}));
    TextDocument document(screen);
    TextSearch search;
    search.start(document, "needle");
    drain(search, document);
    check(search.status() == SearchStatus::complete, "ordered scan completes");
    const auto order = ranks(search, document);
    check(order == (std::vector<size_t>{0, 2}), "ordered hits keep their rows");
}

int main() {
    try {
        reorder_mid_scan_keeps_rank_order();
        navigation_follows_the_new_order();
        eviction_removes_only_dead_hits();
        ordered_document_is_unchanged();
        std::cout << "reordered incremental search passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
