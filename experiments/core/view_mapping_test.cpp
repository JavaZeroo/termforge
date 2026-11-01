// Display-page coordinates published beside one immutable view: a display row
// is composed from source ranges, so hit-testing and range projection must map
// back to document coordinates instead of assuming they are the same thing.
#include "terminal.hpp"
#include "text_document.hpp"
#include "view_mapping.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include <iostream>
#include <stdexcept>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void interior_columns_map_by_offset() {
    Parser parser(3, 4, 8);
    parser.feed("ABCD\r\nEF");
    const TextDocument document(parser.screen);
    const auto begin = document.position(0, 0);
    const auto end = document.position(0, 4);
    check(begin.has_value() && end.has_value(), "source positions for the first row");

    ViewMapping mapping(4, {ViewSourceSpan{0, 0, 4, TextRange{*begin, *end}}});
    const auto hit = mapping.hit(document, 0, 2, ViewAffinity::leading);
    check(hit.has_value() && document.valid(*hit) && hit->column == 2,
          "an equal-width span maps a display column onto the same source column");

    const auto projected = mapping.project(document, TextRange{*begin, *end});
    check(projected.size() == 1, "one composed row yields one display span");
    check(projected[0].row == 0 && projected[0].column_begin == 0 && projected[0].column_end == 4,
          "projection returns the mapped display range");
}

static void trailing_padding_has_no_source_coordinates() {
    Parser parser(3, 4, 8);
    parser.feed("ABCD");
    const TextDocument document(parser.screen);
    const auto begin = document.position(0, 0);
    const auto end = document.position(0, 4);
    check(begin.has_value() && end.has_value(), "source positions for the padded row");

    // Six display columns, but only the first four carry source coordinates.
    ViewMapping mapping(6, {ViewSourceSpan{0, 0, 4, TextRange{*begin, *end}}});
    const auto padded = mapping.hit(document, 0, 5, ViewAffinity::leading);
    check(padded.has_value() && *padded == *end,
          "synthetic padding reports the end of the last mapped source span");
    const auto beyond = mapping.hit(document, 0, 7, ViewAffinity::leading);
    check(!beyond.has_value(), "columns past the page width have no mapping");
}

static void partial_source_ranges_keep_display_boundaries() {
    Parser parser(2, 8, 8);
    parser.feed("ABCDEFGH");
    const TextDocument document(parser.screen);
    ViewMapping mapping(8, {{0, 0, 8, {*document.position(0, 0), *document.position(0, 8)}}});
    const auto projected = mapping.project(document,
        {*document.position(0, 2), *document.position(0, 5)});
    check(projected.size() == 1 && projected[0].row == 0 &&
          projected[0].column_begin == 2 && projected[0].column_end == 5,
          "a partial source selection must not highlight its entire mapped row");
}

static void translated_and_atomic_spans_preserve_their_contract() {
    Parser parser(2, 8, 8);
    parser.feed("ABCDEFGH");
    const TextDocument document(parser.screen);
    ViewMapping translated(8, {{0, 2, 6, {*document.position(0, 1), *document.position(0, 5)}}});
    const auto projected = translated.project(document,
        {*document.position(0, 2), *document.position(0, 4)});
    check(projected.size() == 1 && projected[0].column_begin == 3 && projected[0].column_end == 5,
          "source/display offsets must both apply to partial range projection");
    const auto hit = translated.hit(document, 0, 3, ViewAffinity::leading);
    check(hit && hit->column == 2, "translated display hit returns the source column");

    ViewMapping atomic(4, {{0, 1, 3, {*document.position(0, 1), *document.position(0, 5)}}});
    const auto atom = atomic.project(document,
        {*document.position(0, 2), *document.position(0, 4)});
    check(atom.size() == 1 && atom[0].column_begin == 1 && atom[0].column_end == 3,
          "an unequal-width source span stays an indivisible display atom");
}

static void physical_page_selection_copy_and_resize() {
    Parser parser(2, 8, 4);
    parser.feed("FIRST\r\nSECOND\r\nTHIRD");
    TextDocument document(parser.screen);
    HistoryViewport viewport;
    viewport.scroll_to(parser.screen, 0);
    const auto page = viewport.snapshot(parser.screen);
    const auto first = page.mapping.hit(document, 0, 2, ViewAffinity::leading);
    const auto last = page.mapping.hit(document, 1, 3, ViewAffinity::trailing);
    check(first && last && first->line_id == page.lines[0].id && last->line_id == page.lines[1].id,
          "a historical page binds hits to the displayed source rows");
    TextSelection selection;
    check(selection.set(document, {*last, *first}), "mapped reverse drag forms a selection");
    const auto copy = selection.copy(document, 64);
    check(copy.status == SelectionCopyStatus::ok && copy.text == "RST\r\nSEC",
          "mapped historical drag copies literal source text with the hard break");
    const auto spans = page.mapping.project(document, *selection.range());
    check(spans.size() == 2 && spans[0].row == 0 && spans[0].column_begin == 2 &&
          spans[0].column_end == 8 && spans[1].row == 1 && spans[1].column_begin == 0 &&
          spans[1].column_end == 3, "mapped selection highlights only its visible physical-cell ranges");

    parser.screen.resize(2, 10);
    check(!page.mapping.hit(document, 0, 2, ViewAffinity::leading),
          "a pre-resize page cannot mint current source positions");
    check(selection.reconcile(document) && selection.copy(document, 64).text == "RST\r\nSEC",
          "geometry-only growth preserves the existing source selection policy");
    const auto grown = viewport.snapshot(parser.screen);
    const auto grown_spans = grown.mapping.project(document, *selection.range());
    check(grown_spans.size() == 2 && grown_spans[0].column_begin == 2 && grown_spans[0].column_end == 10 &&
          grown_spans[1].column_end == 3, "fresh geometry projects the reconciled source range");

    parser.screen.resize(2, 4);
    check(selection.copy(document, 64).status == SelectionCopyStatus::invalidated &&
          !grown.mapping.hit(document, 0, 2, ViewAffinity::leading),
          "real cropping invalidates both the saved selection and the old page mapping");
}

static void mapped_wide_and_combining_selection() {
    Parser parser(2, 8, 2);
    parser.feed("A\xe4\xb8\xad" "e\xcc\x81Z");
    TextDocument document(parser.screen);
    HistoryViewport viewport;
    const auto page = viewport.snapshot(parser.screen);
    const auto first = page.mapping.hit(document, 0, 2, ViewAffinity::leading);
    const auto last = page.mapping.hit(document, 0, 4, ViewAffinity::trailing);
    check(first && last, "wide trailing-cell and combining-base hits exist");
    TextSelection selection;
    check(selection.set(document, {*first, *last}), "mapped glyph selection accepted");
    check(selection.copy(document, 16).text == "\xe4\xb8\xad" "e\xcc\x81",
          "mapping keeps full wide scalar and attached combining bytes");
    const auto spans = page.mapping.project(document, *selection.range());
    check(spans.size() == 1 && spans[0].column_begin == 1 && spans[0].column_end == 4,
          "display highlight expands the same complete glyphs as copy");
}

static void old_page_cannot_reuse_evicted_or_changed_source() {
    Parser parser(2, 4, 1);
    parser.feed("ONE\r\nTWO");
    TextDocument document(parser.screen);
    HistoryViewport viewport;
    const auto page = viewport.snapshot(parser.screen);
    parser.feed("\r\nTHR");
    check(page.mapping.hit(document, 0, 1, ViewAffinity::leading).has_value(),
          "an unchanged row keeps its source position after moving into history");
    parser.feed("\r\nFOR");
    check(!page.mapping.hit(document, 0, 1, ViewAffinity::leading),
          "an evicted historical row never resolves to its successor");
    const auto current = viewport.snapshot(parser.screen);
    parser.feed("\rX");
    check(!current.mapping.hit(document, 1, 1, ViewAffinity::leading),
          "changed visible text rejects the older published page version");
}

static void stale_epoch_positions_do_not_hit() {
    Parser parser(3, 4, 8);
    parser.feed("ABCD");
    const TextDocument current(parser.screen);
    const auto begin = current.position(0, 0);
    const auto end = current.position(0, 4);
    check(begin.has_value() && end.has_value(), "source positions before the epoch change");

    ViewMapping mapping(4, {ViewSourceSpan{0, 0, 4, TextRange{*begin, *end}}});

    // Alternate-buffer activation advances the buffer epoch, so positions
    // captured against the old page are stale even though the ids look usable.
    parser.screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    const TextDocument refreshed(parser.screen);

    const auto hit = mapping.hit(refreshed, 0, 2, ViewAffinity::leading);
    check(!hit.has_value(), "a mapping captured at an older epoch must not resolve");
    const auto projected = mapping.project(refreshed, TextRange{*begin, *end});
    check(projected.empty(), "projection over stale coordinates returns nothing");
}

int main() {
    try {
        interior_columns_map_by_offset();
        trailing_padding_has_no_source_coordinates();
        partial_source_ranges_keep_display_boundaries();
        translated_and_atomic_spans_preserve_their_contract();
        physical_page_selection_copy_and_resize();
        mapped_wide_and_combining_selection();
        old_page_cannot_reuse_evicted_or_changed_source();
        stale_epoch_positions_do_not_hit();
        std::cout << "view mapping checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
