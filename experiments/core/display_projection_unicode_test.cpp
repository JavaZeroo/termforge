// Engineering contracts for bounded, read-only Cell projection. These are not
// WindTerm resize observations or a complete grapheme/shaping implementation.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace termforge;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static bool same_style(const Style& a, const Style& b) {
    return a.fg == b.fg && a.bg == b.bg && a.bold == b.bold && a.dim == b.dim &&
        a.underline == b.underline && a.reverse == b.reverse &&
        a.strikethrough == b.strikethrough && a.hidden == b.hidden && a.overline == b.overline;
}

static bool same_cell(const Cell& a, const Cell& b) {
    return a.text == b.text && a.width == b.width && same_style(a.style, b.style) &&
        a.marks == b.marks && a.written == b.written && a.clipped == b.clipped &&
        a.grapheme_fragment == b.grapheme_fragment && a.leading_spacer == b.leading_spacer;
}

static void same_source(const Screen& before, const Screen& after) {
    check(before.width() == after.width() && before.height() == after.height() &&
          before.retained_size() == after.retained_size() && before.history_size() == after.history_size() &&
          before.row == after.row && before.column == after.column &&
          before.wrap_pending == after.wrap_pending && before.discarded == after.discarded &&
          before.buffer_epoch() == after.buffer_epoch() && before.order_revision() == after.order_revision() &&
          same_style(before.style, after.style), "projection changed source membership or protocol state");
    for (size_t r = 0; r < before.retained_size(); ++r) {
        const auto& a = before.retained_line(r);
        const auto& b = after.retained_line(r);
        check(a.id == b.id && a.version == b.version && a.size() == b.size() &&
              a.ending == b.ending && a.next_id == b.next_id && a.wrap_columns == b.wrap_columns &&
              a.content_columns == b.content_columns && a.cropped == b.cropped,
              "projection changed a source row's identity, rank or content metadata");
        for (size_t c = 0; c < a.size(); ++c)
            check(same_cell(a[c], b[c]), "projection changed a source Cell");
    }
}

static void feed(Parser& parser, const std::string& source, bool bytewise) {
    if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
    else parser.feed(source);
}

static void dsr(Parser& parser, const std::string& expected) {
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{expected}, "projection changed literal DSR");
}

static void one_span(const std::vector<ViewCellSpan>& spans, size_t row, size_t first, size_t last) {
    check(spans.size() == 1 && spans[0].row == row && spans[0].column_begin == first &&
          spans[0].column_end == last, "mapped range differs from its literal display footprint");
}

static void complete_atoms_and_mapping(ParseMode mode, bool bytewise) {
    Parser parser(3, 12, 0, mode);
    // Source cells: e-acute [0,1), e+acute [1,2), CJK [2,4), Z [4,5).
    const std::string body = "\xc3\xa9" "e\xcc\x81" "\xe7\x95\x8c" "Z";
    feed(parser, "\x1b[31;44;1;2;4;7;9;53m" + body + "\x1b[0m", bytewise);
    dsr(parser, "\x1b[1;6R");
    const Screen before = parser.screen;
    const TextDocument document(parser.screen);
    HistoryViewport viewport;
    const auto page = viewport.project(parser.screen, 8, 3, {0, 0}, 3);
    const auto revision = viewport.layout_revision();
    check(page.stop == ProjectionStop::source_end && page.source_rows_read == 3 &&
          page.snapshot.lines.size() == 3, "bounded Unicode page changed hard-row membership");
    const auto& row = page.snapshot.lines[0];
    check(row[0].text == "\xc3\xa9" && row[0].width == 1 && row[0].marks == 0 &&
          row[1].text == "e\xcc\x81" && row[1].width == 1 && row[1].marks == 1 &&
          row[2].text == "\xe7\x95\x8c" && row[2].width == 2 &&
          row[3].text.empty() && row[3].width == 0 && row[4].text == "Z",
          "projection split or rewrote a stored Unicode atom");
    for (size_t c = 0; c < 5; ++c)
        check(same_cell(row[c], before.visible_lines()[0][c]), "projection lost Cell style/marks/flags");
    check(row[0].style.fg == 1 && row[0].style.bg == 4 && row[0].style.bold && row[0].style.dim &&
          row[0].style.underline && row[0].style.reverse && row[0].style.strikethrough && row[0].style.overline,
          "styled Unicode fixture lost its attributes");
    check(row[5].text == " " && !row[5].written && row[5].style.plain() && row.id == 0 &&
          page.snapshot.cursor_row == -1, "display padding invented source content or a protocol caret");

    const auto trail = page.snapshot.mapping.hit(document, 0, 3, ViewAffinity::leading, revision);
    const auto end = page.snapshot.mapping.hit(document, 0, 4, ViewAffinity::trailing, revision);
    check(trail && end, "wide Cell hit endpoints are missing");
    TextSelection selection;
    check(selection.set(document, {*end, *trail}), "reverse drag could not normalize the wide Cell");
    const auto copied = selection.copy(document, 64);
    check(copied.status == SelectionCopyStatus::ok && copied.text == "\xe7\x95\x8c",
          "a trailing-cell drag copied half a wide Cell");
    one_span(page.snapshot.mapping.project(document, *selection.range(), revision), 0, 2, 4);

    struct Query { const char* bytes; size_t first, last; const char* copied; };
    // A query containing just the combining mark still owns the complete Cell.
    for (const auto query : {Query{"\xc3\xa9", 0, 1, "\xc3\xa9"},
                             Query{"\xcc\x81", 1, 2, "e\xcc\x81"},
                             Query{"\xe7\x95\x8c" "Z", 2, 5, "\xe7\x95\x8c" "Z"}}) {
        TextSearch search;
        const auto generation = search.start(document, query.bytes);
        for (int i = 0; i < 100 && search.status() == SearchStatus::running; ++i)
            search.step(document, generation, 4);
        check(search.status() == SearchStatus::complete && search.matches().size() == 1,
              "literal Unicode Find did not finish with one match");
        const auto match = search.match(document, 0);
        check(match && match->range.begin == *document.position(0, query.first) &&
              match->range.end == *document.position(0, query.last), "Find lost its complete source atom range");
        one_span(page.snapshot.mapping.project(document, match->range, revision), 0, query.first, query.last);
        TextSelection found;
        check(found.set(document, match->range) && found.copy(document, 64).text == query.copied,
              "Find range, display highlight and copied source disagree");
    }
    const auto next = viewport.project(parser.screen, 10, 3, {0, 0}, 3);
    check(!page.snapshot.mapping.hit(document, 0, 2, ViewAffinity::leading, viewport.layout_revision()) &&
          page.snapshot.mapping.project(document, *selection.range(), viewport.layout_revision()).empty(),
          "a prior Unicode layout accepted new-layout coordinates");
    check(next.snapshot.mapping.hit(document, 0, 2, ViewAffinity::leading, viewport.layout_revision()) ==
          document.position(0, 2), "fresh Unicode layout lost the stable source position");
    check(selection.copy(document, 64).text == "\xe7\x95\x8c", "display-only relayout invalidated source selection");
    same_source(before, parser.screen);
    dsr(parser, "\x1b[1;6R");
    parser.finish();
}

static void exact_fit_pages_preserve_atoms() {
    Parser parser(3, 6, 0);
    parser.feed("\xc3\xa9" "e\xcc\x81" "\xe7\x95\x8c" "Z");
    const Screen before = parser.screen;
    HistoryViewport viewport;
    const auto first = viewport.project(parser.screen, 2, 1, {0, 0}, 1);
    check(first.stop == ProjectionStop::page_full && first.source_rows_read == 1 &&
          first.snapshot.lines.size() == 1 && first.snapshot.lines[0][0].text == "\xc3\xa9" &&
          first.snapshot.lines[0][1].text == "e\xcc\x81" && first.next.bound &&
          first.next.row == 0 && first.next.column == 2, "first page consumed bytes instead of complete Cells");
    const auto second = viewport.project(parser.screen, 2, 1, first.next, 1);
    check(second.stop == ProjectionStop::page_full && second.source_rows_read == 1 &&
          second.snapshot.lines.size() == 1 && second.snapshot.lines[0][0].text == "\xe7\x95\x8c" &&
          second.snapshot.lines[0][0].width == 2 && second.snapshot.lines[0][1].width == 0 &&
          second.snapshot.lines[0][1].text.empty() && second.next.row == 0 && second.next.column == 4,
          "page continuation split or duplicated a wide lead/trail pair");
    const auto third = viewport.project(parser.screen, 2, 1, second.next, 1);
    check(third.source_rows_read == 1 && third.snapshot.lines.size() == 1 &&
          third.snapshot.lines[0][0].text == "Z" && third.next.row == 1 && third.next.column == 0,
          "whole-atom pagination lost the suffix");
    same_source(before, parser.screen);
}

static void maximum_parser_cell_bytes_are_preserved() {
    Parser parser(2, 4, 0);
    // Existing Unicode-15 core representation: U+20000 plus 16 U+1D185 marks.
    // 4 + 16*4 = 68 bytes. This is a storage bound, not an original-policy claim.
    std::string atom = "\xf0\xa0\x80\x80";
    for (int i = 0; i < 16; ++i) atom += "\xf0\x9d\x86\x85";
    parser.feed(atom + "Z");
    check(atom.size() == 68 && parser.screen.visible_lines()[0][0].marks == 16,
          "maximum parser Cell fixture differs");
    const Screen before = parser.screen;
    HistoryViewport viewport;
    const auto page = viewport.project(parser.screen, 4, 2, {0, 0}, 2);
    check(page.snapshot.lines[0][0].text == atom && page.snapshot.lines[0][0].marks == 16 &&
          page.snapshot.lines[0][0].width == 2 && page.snapshot.lines[0][1].width == 0 &&
          page.snapshot.lines[0][2].text == "Z", "valid maximum-sized Cell was truncated or rejected");
    same_source(before, parser.screen);
}

static void source_window_does_not_publish_a_partial_logical_row() {
    Parser parser(3, 4, 0);
    parser.feed("\xc3\xa9" "e\xcc\x81" "\xe7\x95\x8c" "Z");
    const Screen before = parser.screen;
    HistoryViewport viewport;
    const auto limited = viewport.project(parser.screen, 6, 1, {0, 0}, 1);
    check(limited.stop == ProjectionStop::source_limit && limited.source_rows_read == 1 &&
          limited.snapshot.lines.empty() && limited.next.bound &&
          limited.next.row == 0 && limited.next.column == 0,
          "Unicode source-window exhaustion published a partial soft row or falsely advanced");
    const auto resumed = viewport.project(parser.screen, 6, 1, limited.next, 2);
    check(resumed.source_rows_read == 2 && resumed.snapshot.lines.size() == 1 &&
          resumed.snapshot.lines[0][0].text == "\xc3\xa9" &&
          resumed.snapshot.lines[0][1].text == "e\xcc\x81" &&
          resumed.snapshot.lines[0][2].text == "\xe7\x95\x8c" &&
          resumed.snapshot.lines[0][3].width == 0 && resumed.snapshot.lines[0][4].text == "Z" &&
          resumed.next.row == 2 && resumed.next.column == 0,
          "expanded source window lost or duplicated complete Unicode atoms");
    same_source(before, parser.screen);
}

template<class Operation>
static void rejects_without_publication(HistoryViewport& viewport, const Screen& screen, Operation operation) {
    const auto revision = viewport.layout_revision();
    const Screen before = screen;
    bool rejected = false;
    try { operation(); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "unsupported projection source/request did not throw invalid_argument");
    check(viewport.layout_revision() == revision, "failed projection advanced the published layout");
    same_source(before, screen);
}

static void invalid_atom_request(bool trail_start) {
    Parser parser(2, 8, 0);
    parser.feed("A");
    HistoryViewport viewport;
    (void)viewport.project(parser.screen, 8, 2, {0, 0}, 2);
    parser.feed("\r\xe7\x95\x8c" "Z");
    rejects_without_publication(viewport, parser.screen, [&] {
        // No assertion about a two-column atom with one column remaining on an
        // otherwise wider page. Only impossible width=1 and a mid-atom start.
        (void)viewport.project(parser.screen, trail_start ? 8 : 1, 2,
                               {0, trail_start ? size_t{1} : size_t{0}}, 2);
    });
}

enum class InvalidSource { orphan_trail, missing_trail, nonempty_trail, marked_trail,
                           lead_at_end, overlong_text, clipped, fragment, spacer };

static void invalid_imported_source(InvalidSource kind) {
    Parser parser(2, 8, 0);
    parser.feed("A");
    HistoryViewport viewport;
    (void)viewport.project(parser.screen, 8, 2, {0, 0}, 2);
    auto rows = parser.screen.visible_lines();
    auto& line = rows[0];
    switch (kind) {
    case InvalidSource::orphan_trail: line[1] = {"", 0, {}, 0, true}; break;
    case InvalidSource::missing_trail: line[1] = {"\xe7\x95\x8c", 2, {}, 0, true}; break;
    case InvalidSource::nonempty_trail:
    case InvalidSource::marked_trail:
        line[1] = {"\xe7\x95\x8c", 2, {}, 0, true};
        line[2] = {"", 0, {}, 0, true};
        if (kind == InvalidSource::nonempty_trail) line[2].text = "X";
        else line[2].marks = 1;
        break;
    case InvalidSource::lead_at_end: line[7] = {"\xe7\x95\x8c", 2, {}, 0, true}; break;
    case InvalidSource::overlong_text: line[0].text.assign(69, 'A'); break;
    case InvalidSource::clipped: line[0].clipped = true; break;
    case InvalidSource::fragment: line[0].grapheme_fragment = true; break;
    case InvalidSource::spacer: line[0].leading_spacer = true; break;
    }
    ++line.version;
    parser.screen.publish({}, std::move(rows));
    rejects_without_publication(viewport, parser.screen, [&] {
        (void)viewport.project(parser.screen, 8, 2, {0, 0}, 2);
    });
}

int main() {
    size_t passed = 0, failed = 0;
    const auto run = [&](const std::string& name, const auto& operation) {
        try { operation(); ++passed; }
        catch (const std::exception& error) { ++failed; std::cerr << name << ": " << error.what() << '\n'; }
    };
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool bytewise : {false, true})
            run(std::string(mode == ParseMode::strict ? "strict/" : "recovery/") +
                (bytewise ? "bytewise" : "whole"), [&] { complete_atoms_and_mapping(mode, bytewise); });
    run("exact-fit-pages", exact_fit_pages_preserve_atoms);
    run("68-byte-parser-atom", maximum_parser_cell_bytes_are_preserved);
    run("source-window-resume", source_window_does_not_publish_a_partial_logical_row);
    run("mid-trail-start", [] { invalid_atom_request(true); });
    run("width-one", [] { invalid_atom_request(false); });
    for (const auto entry : {
        std::pair{InvalidSource::orphan_trail, "orphan-trail"},
        std::pair{InvalidSource::missing_trail, "missing-trail"},
        std::pair{InvalidSource::nonempty_trail, "nonempty-trail"},
        std::pair{InvalidSource::marked_trail, "marked-trail"},
        std::pair{InvalidSource::lead_at_end, "lead-at-end"},
        std::pair{InvalidSource::overlong_text, "69-byte-import"},
        std::pair{InvalidSource::clipped, "candidate-clipped"},
        std::pair{InvalidSource::fragment, "candidate-fragment"},
        std::pair{InvalidSource::spacer, "candidate-spacer"}})
        run(entry.second, [&] { invalid_imported_source(entry.first); });
    std::cout << "Unicode display projection engineering checks: " << passed << '/' << passed + failed
              << " cases passed (no original resize, remaining-column, attachment or Qt claim)\n";
    return failed ? 1 : 0;
}
