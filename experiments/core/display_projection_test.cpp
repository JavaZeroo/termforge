// Original fixed-body display observations, separated from resize/write policy.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace termforge;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void marker(const ViewSnapshot& page, const char* text, size_t row, size_t column) {
    size_t found = 0;
    for (size_t r = 0; r < page.lines.size(); ++r)
        for (size_t c = 0; c < page.lines[r].size(); ++c)
            if (page.lines[r][c].text == text) {
                ++found;
                check(r == row && c == column, "marker differs from the literal observed display position");
            }
    check(found == 1, "display duplicated or lost a fixed source marker");
}

static void original_body_three_widths(ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    // SHA256 c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c.
    // This is the 78-byte source in the original resize-state strict export.
    const std::string source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[?7l\x1b[8;91Huv"
        "\x1b[?7h\x1b[10;91Hwx";
    if (bytewise) for (const char byte : source) parser.feed(std::string_view(&byte, 1));
    else parser.feed(source);
    TextDocument document(parser.screen);
    std::vector<std::pair<uint64_t, uint64_t>> identities;
    for (size_t i = 0; i < document.size(); ++i) {
        const auto meta = document.meta(i);
        identities.push_back({meta.id, meta.version});
    }
    const auto epoch = document.epoch(), order = document.order_revision();
    TextSelection selection;
    check(selection.set(document, {*document.position(9, 90), *document.position(10, 1)}),
          "source wx range could not be selected");
    TextSearch search;
    const auto generation = search.start(document, "wx");
    for (int i = 0; i < 100 && search.status() == SearchStatus::running; ++i)
        search.step(document, generation, 4);
    check(search.status() == SearchStatus::complete && search.matches().size() == 1,
          "source soft continuation did not yield exactly one wx result");

    HistoryViewport viewport;
    const auto wide = viewport.project(parser.screen, 91, 20, {0, 0}, 20);
    const auto first_revision = viewport.layout_revision();
    marker(wide.snapshot, "v", 7, 90);
    marker(wide.snapshot, "w", 9, 90);
    marker(wide.snapshot, "x", 10, 0);
    check(wide.snapshot.lines[2][0].text == "A", "ANCHOR moved in the wide display");
    const auto narrow = viewport.project(parser.screen, 61, 20, {0, 0}, 20);
    const auto narrow_revision = viewport.layout_revision();
    marker(narrow.snapshot, "v", 8, 29);
    marker(narrow.snapshot, "w", 11, 29);
    marker(narrow.snapshot, "x", 11, 30);
    check(narrow.snapshot.lines[2][0].text == "A", "ANCHOR moved in the narrow display");
    check(first_revision != narrow_revision &&
          !wide.snapshot.mapping.hit(document, 7, 90, ViewAffinity::leading, narrow_revision),
          "old-width page accepted a hit under the new layout revision");
    check(!narrow.snapshot.mapping.hit(document, 8, 29, ViewAffinity::leading),
          "layout-bound page bypassed its revision guard");
    const auto hit = narrow.snapshot.mapping.hit(document, 8, 29, ViewAffinity::leading, narrow_revision);
    check(hit && *hit == *document.position(7, 90), "narrow v hit did not retain its source coordinate");
    const auto highlights = narrow.snapshot.mapping.project(document, search.matches().front().range, narrow_revision);
    check(highlights.size() == 1 && highlights[0].row == 11 &&
          highlights[0].column_begin == 29 && highlights[0].column_end == 31,
          "wx Find highlight did not join its two source rows in the narrow view");
    check(selection.copy(document, 32).text == "wx", "pure display projection changed the selected text");
    parser.feed("\x1b[6n");
    check(parser.take_replies() == std::vector<std::string>{"\x1b[11;2R"},
          "display projection changed the independent protocol cursor");

    const auto restored = viewport.project(parser.screen, 91, 20, {0, 0}, 20);
    marker(restored.snapshot, "v", 7, 90);
    marker(restored.snapshot, "w", 9, 90);
    marker(restored.snapshot, "x", 10, 0);
    check(!narrow.snapshot.mapping.hit(document, 8, 29, ViewAffinity::leading, viewport.layout_revision()),
          "restored layout accepted a stale narrow page");
    check(document.epoch() == epoch && document.order_revision() == order && parser.screen.width() == 91,
          "projection changed the source epoch, order or protocol width");
    for (size_t i = 0; i < document.size(); ++i) {
        const auto meta = document.meta(i);
        check(identities[i] == std::make_pair(meta.id, meta.version), "projection changed a source identity/version");
    }
    check(wide.source_rows_read <= 20 && narrow.source_rows_read <= 20 && restored.source_rows_read <= 20,
          "projection exceeded its explicit source window");
    parser.finish();
}

static void bounded_window_resume_and_stale_source() {
    Parser parser(4, 4, 2);
    parser.feed("ABCDxy\r\nZ");
    HistoryViewport viewport;
    const auto limited = viewport.project(parser.screen, 6, 2, {0, 0}, 1);
    check(limited.stop == ProjectionStop::source_limit && limited.snapshot.lines.empty() &&
          limited.source_rows_read == 1 && limited.next.bound && limited.next.row == 0 && limited.next.column == 0,
          "partial soft row at a source boundary was published or falsely advanced");
    const auto expanded = viewport.project(parser.screen, 6, 2, limited.next, 3);
    check(expanded.snapshot.lines.size() == 2 && expanded.snapshot.lines[0][3].text == "D" &&
          expanded.snapshot.lines[0][4].text == "x" && expanded.snapshot.lines[0][5].text == "y" &&
          expanded.snapshot.lines[1][0].text == "Z" && expanded.source_rows_read == 3,
          "expanded source window did not resume the soft row before its real hard break");
    const auto first = viewport.project(parser.screen, 3, 1, {0, 0}, 4);
    check(first.stop == ProjectionStop::page_full && first.next.row == 0 && first.next.column == 3 &&
          first.source_rows_read == 1, "page limit did not return the precise source suffix");
    const auto second = viewport.project(parser.screen, 3, 1, first.next, 4);
    check(second.snapshot.lines.size() == 1 && second.snapshot.lines[0][0].text == "D" &&
          second.snapshot.lines[0][1].text == "x" && second.snapshot.lines[0][2].text == "y" &&
          second.source_rows_read == 2, "page continuation lost or duplicated source characters");
    const auto revision = viewport.layout_revision();
    parser.feed("\x1b[1;1H!");
    bool rejected = false;
    try { (void)viewport.project(parser.screen, 3, 1, first.next, 4); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && viewport.layout_revision() == revision,
          "source overwrite revived a stale continuation or invalidated a page on failed preparation");
    const auto before_reorder = viewport.project(parser.screen, 3, 1, {0, 0}, 4).next;
    parser.feed("\x1b[2;1H\x1b[L");
    rejected = false;
    try { (void)viewport.project(parser.screen, 3, 1, before_reorder, 4); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "changed document order reused a rank continuation whose first row still exists");
    const auto before_switch = viewport.project(parser.screen, 3, 1, {0, 0}, 4).next;
    parser.feed("\x1b[?1049h\x1b[?1049l");
    rejected = false;
    try { (void)viewport.project(parser.screen, 3, 1, before_switch, 4); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "returning to primary revived a continuation from an older buffer epoch");
}

static void bounded_history_and_cell_fidelity() {
    Parser parser(2, 6, 300);
    for (int i = 0; i < 250; ++i) parser.feed("row\r\n");
    parser.feed("\x1b[31;44;4mA  \x1b[0m");
    const auto last = parser.screen.retained_size() - 1;
    HistoryViewport viewport;
    const auto page = viewport.project(parser.screen, 2, 2, {last, 0}, last + 1);
    check(page.source_rows_read == 1 && page.stop == ProjectionStop::source_end &&
          page.snapshot.lines.size() == 2, "tail projection expanded the historical prefix or lost explicit spaces");
    const auto& a = page.snapshot.lines[0][0];
    const auto& explicit_space = page.snapshot.lines[1][0];
    const auto& padding = page.snapshot.lines[1][1];
    check(a.text == "A" && a.style.fg == 1 && a.style.bg == 4 && a.style.underline &&
          explicit_space.text == " " && explicit_space.written && explicit_space.style.bg == 4 &&
          !padding.written && padding.style.plain(), "projection rebuilt cells from text and lost their source attributes");
    check(page.snapshot.lines[0].id == 0 && page.snapshot.lines[1].id == 0,
          "composite display rows invented source identities");
    const auto revision = viewport.layout_revision();
    bool rejected = false;
    try { (void)viewport.project(parser.screen, 2, 2, {0, 0}, 201); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && viewport.layout_revision() == revision, "oversize source request ignored its work bound");
    Parser wide(2, 6, 0);
    wide.feed("\xe4\xb8\xad");
    HistoryViewport wide_view;
    rejected = false;
    try { (void)wide_view.project(wide.screen, 2, 2, {0, 0}, 2); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && wide_view.layout_revision() == 0,
          "unsupported wide content was silently treated as an ASCII cell");
}

static void empty_soft_successor_does_not_invent_a_display_row() {
    Parser parser(3, 4, 0);
    parser.feed("ABCDE\x1b[2;1H\x1b[2K");
    HistoryViewport viewport;
    const auto whole = viewport.project(parser.screen, 4, 3, {0, 0}, 3);
    check(whole.snapshot.lines.size() == 2 && whole.snapshot.lines[0][0].text == "A" &&
          whole.snapshot.lines[0][3].text == "D" && whole.snapshot.lines[1][0].text == " ",
          "empty soft successor created an extra display row after an exactly full logical line");
    const auto first = viewport.project(parser.screen, 4, 1, {0, 0}, 3);
    const auto second = viewport.project(parser.screen, 4, 1, first.next, 3);
    check(first.snapshot.lines.size() == 1 && second.snapshot.lines.size() == 1 &&
          second.stop == ProjectionStop::source_end && second.snapshot.lines[0][0].text == " ",
          "page continuation forgot the soft boundary and invented another empty row");
    const auto limited = viewport.project(parser.screen, 4, 3, {0, 0}, 1);
    const auto resumed = viewport.project(parser.screen, 4, 3, limited.next, 3);
    check(limited.stop == ProjectionStop::source_limit && limited.snapshot.lines.size() == 1 &&
          resumed.stop == ProjectionStop::source_end && resumed.snapshot.lines.size() == 1,
          "source-window continuation lost the completed soft prefix");
}

int main() {
    try {
        for (const auto mode : {ParseMode::strict, ParseMode::recovery})
            for (const bool bytewise : {false, true}) original_body_three_widths(mode, bytewise);
        bounded_window_resume_and_stale_source();
        bounded_history_and_cell_fidelity();
        empty_soft_successor_does_not_invent_a_display_row();
        std::cout << "display projection checks passed (4 original-source feeds, 3 engineering scenarios)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
