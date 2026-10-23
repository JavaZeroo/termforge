// Observable text/position contracts with independently authored literal oracles.
#include "terminal.hpp"
#include "text_document.hpp"
#include "text_selection.hpp"
#include <iostream>
#include <sstream>
using namespace termforge;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }

static void projected_rows_are_owned_and_join_only_real_wraps() {
    Parser p(2, 4, 4);
    p.feed("abc\xe4\xb8\xad" "e\xcc\x81 ");
    TextDocument document(p.screen);
    const auto first = document.line(0), second = document.line(1);
    check(first.projection.text == "abc" && first.columns == 4 && first.joins_next,
          "wide early-wrap preserves its real continuation");
    check(second.projection.text == "\xe4\xb8\xad" "e\xcc\x81 " && !second.joins_next,
          "projection preserves full wide, combining and explicit-space content");
    p.feed("\rX");
    check(second.projection.text == "\xe4\xb8\xad" "e\xcc\x81 ", "returned text owns its lifetime");
}
static TextPosition at(const TextDocument& document, size_t row, size_t column) {
    const auto value = document.position(row, column);
    check(value.has_value(), "fixture position must exist"); return *value;
}
static void forward_reverse_copy_and_glyph_boundaries() {
    Parser p(3, 18, 4);
    p.feed("ALPHA  tail  \r\nBETA \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81");
    TextDocument document(p.screen); TextSelection selection;
    check(selection.set(document, {at(document, 0, 0), at(document, 1, 11)}), "set forward selection");
    auto result = selection.copy(document, 128);
    check(result.status == SelectionCopyStatus::ok &&
          result.text == "ALPHA  tail  \r\nBETA \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81",
          "copy has independent CRLF, explicit-tail and Unicode oracle");
    check(selection.set(document, {at(document, 1, 11), at(document, 0, 0)}), "set reverse selection");
    check(selection.copy(document, 128).text == "ALPHA  tail  \r\nBETA \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81", "reverse copy has same text");
    check(selection.set(document, {at(document, 1, 6), at(document, 1, 8)}), "set partial wide cells");
    check(selection.copy(document, 128).text == "\xe4\xb8\xad\xe6\x96\x87", "partial wide endpoints expand whole scalars");
    const auto highlight = selection.columns(document, 1);
    check(highlight && highlight->begin == 5 && highlight->end == 9, "highlight covers whole wide cells");
    check(selection.set(document, {at(document, 1, 10), at(document, 1, 11)}), "set combining base");
    check(selection.copy(document, 128).text == "e\xcc\x81", "combining bytes stay attached to base");
    check(selection.set(document, {at(document, 1, 6), at(document, 1, 6)}) && selection.empty(),
          "equal trailing-cell endpoints stay empty");
}
static void wrap_boundaries_history_and_copy_budget() {
    Parser p(2, 4, 6); p.feed("abc\xe4\xb8\xad" "e\xcc\x81 \r\nHARD\r\nTAIL  ");
    TextDocument document(p.screen); TextSelection selection;
    check(selection.set(document, {at(document, 0, 0), at(document, 4, 2)}), "set retained cross-history selection");
    const std::string expected = "abc\xe4\xb8\xad" "e\xcc\x81 \r\nHARD\r\nTAIL  ";
    const auto copy = selection.copy(document, expected.size());
    check(copy.status == SelectionCopyStatus::ok && copy.text == expected,
          "real wide early-wrap and full-wrap concatenate, hard breaks stay CRLF");
    const auto short_copy = selection.copy(document, expected.size() - 1);
    check(short_copy.status == SelectionCopyStatus::limit_exceeded && short_copy.text.empty(),
          "over-budget copy returns no partial text");
    check(selection.copy(document, expected.size()).text == expected, "budget rejection preserves selection");
    Parser full(2, 4, 0); full.feed("FULL\r\nNEXT"); TextDocument hard(full.screen);
    check(selection.set(hard, {at(hard, 0, 0), at(hard, 1, 4)}), "select exactly-full hard rows");
    check(selection.copy(hard, 20).text == "FULL\r\nNEXT", "full columns do not imply a soft wrap");
}
static void invalidation_and_geometry() {
    Parser p(3, 8, 2); p.feed("ALPHA\r\nBETA\r\nGAMMA");
    TextDocument document(p.screen); TextSelection selection;
    check(selection.set(document, {at(document, 0, 0), at(document, 1, 4)}), "select before growth");
    const auto old = at(document, 0, 0);
    p.screen.resize(3, 12);
    check(!document.valid(old), "strict positions reject old resize version");
    check(selection.reconcile(document) && selection.copy(document, 20).text == "ALPHA\r\nBETA",
          "geometry-only growth refreshes unchanged text versions");
    p.screen.resize(3, 6);
    check(selection.reconcile(document) && selection.copy(document, 20).text == "ALPHA\r\nBETA",
          "removing only unused padding preserves text");
    p.feed("\x1b[3;1HX");
    check(selection.reconcile(document), "writes to an unrelated row preserve selection");
    p.feed("\x1b[2;1HZO");
    auto invalid = selection.copy(document, 20);
    check(invalid.status == SelectionCopyStatus::invalidated && invalid.text.empty() && selection.empty(),
          "overwritten selected text cannot be copied from old snapshot");
    check(selection.set(document, {at(document, 0, 0), at(document, 0, 5)}), "select before crop");
    p.screen.resize(3, 3);
    check(!selection.reconcile(document) && selection.empty(), "real crop invalidates range");
    Parser past(2, 4, 1); past.feed("ONE\r\nTWO"); TextDocument retained(past.screen);
    check(selection.set(retained, {at(retained, 0, 0), at(retained, 0, 3)}), "select a soon-historical row");
    past.feed("\r\nTHR");
    check(selection.reconcile(retained) && selection.copy(retained, 10).text == "ONE", "moving unchanged row into history keeps identity");
    past.feed("\r\nFOR");
    check(!selection.reconcile(retained) && selection.empty(), "history eviction invalidates without reused identity");
    check(!retained.index_of(999999) && !retained.position(retained.size(), 0) && !retained.position(0, 5),
          "nonexistent ids and columns are rejected");
}
static void word_line_policy() {
    Parser p(3, 32, 2); p.feed("TF-HISTORY-000  \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81,ok\r\nNEXT  ");
    TextDocument document(p.screen); TextSelection selection;
    check(selection.select_word(document, 0, 4) && selection.copy(document, 50).text == "TF-HISTORY-000",
          "observed hyphenated sample is one word");
    check(selection.select_word(document, 0, 14) && selection.copy(document, 50).text == "  ", "project whitespace run policy");
    check(selection.select_word(document, 0, 17) && selection.copy(document, 50).text == "\xe4\xb8\xad\xe6\x96\x87", "project non-ASCII word policy includes wide trail");
    check(selection.select_word(document, 0, 22) && selection.copy(document, 50).text == ",", "project punctuation is one scalar");
    check(selection.select_line(document, 1) && selection.copy(document, 50).text == "NEXT  ", "mouse line policy excludes final hard break and preserves explicit spaces");
    check(!selection.select_word(document, 2, 4) && selection.empty(), "unwritten padding does not expose stale selection");
    Parser soft(2, 4, 0); soft.feed("ABC-DEF"); TextDocument wrapped(soft.screen);
    check(selection.select_word(wrapped, 1, 1) && selection.copy(wrapped, 20).text == "ABC-DEF", "project word policy extends through real wrap only");
}
static void empty_stale_and_capacity_boundaries() {
    Parser p(3, 8, 0); p.feed("ONE\r\nTWO\r\nTHREE");
    TextDocument document(p.screen); TextSelection selection;
    check(selection.copy(document, 0).status == SelectionCopyStatus::empty, "initial empty selection copy");
    check(selection.set(document, {at(document, 0, 0), at(document, 2, 5)}), "select an interior live row");
    p.feed("\x1b[2;1HNEW");
    check(!selection.reconcile(document) && selection.copy(document, 100).status == SelectionCopyStatus::invalidated,
          "interior overwrite invalidates even though both endpoint versions stayed valid");
    const auto stale = at(document, 1, 0); p.feed("\x1b[2;1HX");
    check(!selection.set(document, {stale, at(document, 2, 5)}) && selection.empty(), "stale input position cannot create selection");
    check(!selection.set(document, {{999999, 1, 0}, at(document, 2, 5)}), "nonexistent position cannot create selection");
    check(selection.set(document, {at(document, 0, 3), at(document, 0, 7)}) &&
          selection.copy(document, 20).status == SelectionCopyStatus::empty,
          "unwritten trailing padding has no clipboard text");
    check(selection.select_word(document, 0, 0) && selection.copy(document, 20).text == "ONE", "word does not cross hard break");
    check(!selection.select_line(document, document.size()) && selection.empty(), "invalid row clears selection");
    Parser large(1, 500, 2000);
    const std::string row(500, 'X');
    for (int i = 0; i < 1800; ++i) large.feed(row + "\r\n");
    TextDocument bounded(large.screen);
    check(!selection.set(bounded, {at(bounded, 0, 0), at(bounded, bounded.size() - 1, 0)}) && selection.empty(),
          "selection snapshot budget rejects excessive retained rows without stale range");
}
int main() {
    try {
        std::ostringstream json;
        Screen::quoted(json,std::string("A\r\n\t\0\"\\",7));
        check(json.str()=="\"A\\u000d\\u000a\\u0009\\u0000\\\"\\\\\"",
              "copied multi-line/control strings remain valid JSON without changing text bytes");
        projected_rows_are_owned_and_join_only_real_wraps();
        forward_reverse_copy_and_glyph_boundaries();
        wrap_boundaries_history_and_copy_budget();
        invalidation_and_geometry();
        word_line_policy();
        empty_stale_and_capacity_boundaries();
        std::cout << "selection contracts passed (6 independent groups)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
