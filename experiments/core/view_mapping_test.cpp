// Display-page coordinates published beside one immutable view: a display row
// is composed from source ranges, so hit-testing and range projection must map
// back to document coordinates instead of assuming they are the same thing.
#include "terminal.hpp"
#include "text_document.hpp"
#include "view_mapping.hpp"
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
        stale_epoch_positions_do_not_hit();
        std::cout << "view mapping checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
