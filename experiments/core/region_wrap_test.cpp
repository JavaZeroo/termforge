// Literal copy/search oracles for the core's existing physical region scrolling.
// Automatic wrapping must keep a logical line joined to its surviving successor.
#include "terminal.hpp"
#include "text_search.hpp"
#include "text_selection.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
using namespace termforge;

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

static void feed(Parser& parser, std::string_view input, size_t chunk) {
    for (size_t offset = 0; offset < input.size(); offset += chunk)
        parser.feed(input.substr(offset, (std::min)(chunk, input.size() - offset)));
}

static void search_literal(const TextDocument& document, const std::string& query,
                           size_t first, size_t last, bool expected) {
    TextSearch search;
    const auto generation = search.start(document, query);
    for (int guard = 0; guard < 1000 && search.status() == SearchStatus::running; ++guard)
        search.step(document, generation, 2);
    check(search.status() == SearchStatus::complete, "literal search exceeded its work bound");
    const auto match = search.match(document, 0);
    check(match.has_value() == expected, "literal search lost a logical line or crossed a hard break");
    if (match) {
        check(document.index_of(match->range.begin.line_id) == std::optional<size_t>(first) &&
              document.index_of(match->range.end.line_id) == std::optional<size_t>(last),
              "literal search returned the wrong physical rows");
    }
    check(!search.match(document, 1), "literal search duplicated a match");
}

struct TextCase {
    const char* name;
    std::string input, first, last;
    size_t end_column;
};

static void wrapped_text(ParseMode mode, int region, size_t history_limit, size_t chunk,
                         const TextCase& text, bool automatic) {
    Parser parser(4, 4, history_limit, mode);
    feed(parser, "H0\r\nH1\r\nH2\r\nH3\r\nH4\x1b[2J\x1b[H", chunk);
    const auto history_before = parser.screen.history_lines().size();
    check(history_before == (history_limit ? 1u : 0u), "history fixture was not populated");
    // Regions: whole screen; partial ending before the last row; partial ending
    // on the last row. Outside sentinels must survive both kinds of partial scroll.
    const int bottom = region == 1 ? 2 : 3;
    if (region) feed(parser, "\x1b[1;1HT", chunk);
    if (region == 1) feed(parser, "\x1b[4;1HB", chunk);
    feed(parser, region == 0 ? "\x1b[r\x1b[4;1H" :
                 region == 1 ? "\x1b[2;3r\x1b[3;1H" : "\x1b[2;4r\x1b[4;1H", chunk);
    feed(parser, text.input, chunk);
    parser.finish();

    const TextDocument document(parser.screen);
    const auto history_after = parser.screen.history_lines().size();
    check(history_after == history_before + (!region && history_limit ? 1u : 0u),
          "region scrolling changed the wrong history membership");
    if (history_limit) check(document.line(0).projection.text == "H0", "scrolling changed retained history");
    if (region) check(document.line(history_after).projection.text == "T", "scrolling changed the row above the region");
    if (region == 1) check(document.line(history_after + 3).projection.text == "B", "scrolling changed the row below the region");
    const auto first = history_after + static_cast<size_t>(bottom - 1), last = first + 1;
    check(document.line(first).projection.text == text.first &&
          document.line(last).projection.text == text.last, "scrolling changed the literal visible text");
    for (size_t rank = 0; rank < document.size(); ++rank)
        check(document.index_of(document.meta(rank).id) == std::optional<size_t>(rank),
              "scrolling made a retained row unreachable");

    TextSelection selection;
    check(selection.set(document, {*document.position(first, 0), *document.position(last, text.end_column)}),
          "could not select the wrapped text");
    const auto copy = selection.copy(document, 64);
    check(copy.status == SelectionCopyStatus::ok && copy.text == text.input,
          "copy changed the literal logical text across the scroll");
    check(document.meta(first).joins_next == automatic, "logical continuation has the wrong boundary");
    const std::string query = automatic ? text.input : "ABCD\nE";
    search_literal(document, query, first, last, true);
    if (!automatic) search_literal(document, "ABCDE", first, last, false);
}

int main() {
    const TextCase automatic_cases[] = {
        {"ascii", "ABCDE", "ABCD", "E", 1},
        {"wide-combining-at-margin", "A\xe4\xb8\xad\xcc\x81" "BC", "A\xe4\xb8\xad\xcc\x81" "B", "C", 1},
        {"wide-combining-early-wrap", "ABC\xe4\xb8\xad\xcc\x81" "D", "ABC", "\xe4\xb8\xad\xcc\x81" "D", 3},
    };
    const TextCase hard_break{"hard-break", "ABCD\r\nE", "ABCD", "E", 1};
    size_t passed = 0, failed = 0;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (int region = 0; region < 3; ++region)
            for (const size_t history : {0u, 5u})
                for (const size_t chunk : {1u, 3u, 256u}) {
                    const auto run = [&](const TextCase& text, bool automatic) {
                        try { wrapped_text(mode, region, history, chunk, text, automatic); ++passed; }
                        catch (const std::exception& error) {
                            ++failed;
                            std::cerr << (mode == ParseMode::strict ? "strict" : "recovery")
                                      << " region=" << region << " history=" << history << " chunk=" << chunk
                                      << " text=" << text.name << ": " << error.what() << '\n';
                        }
                    };
                    for (const auto& text : automatic_cases) run(text, true);
                    run(hard_break, false);
                }
    std::cout << "region logical text: passed=" << passed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
