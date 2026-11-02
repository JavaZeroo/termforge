// Incremental projection engineering contract. No new original behavior oracle.
// The separate legacy 500-fragment diagnostic is the observed baseline;
// this new API did not have an independently executed preimplementation RED.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using namespace termforge;
static_assert(!std::is_copy_constructible_v<ProjectionJob>);
static_assert(std::is_nothrow_move_constructible_v<ProjectionJob>);

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Action> static void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "expected invalid_argument");
}
static void yielded(const ProjectionStep& step, size_t rows) {
    check(step.status == ProjectionStepStatus::yielded && !step.page && step.source_rows_read == rows,
          "yield exposed a partial page or counted the wrong step work");
}
static std::string row_text(const ProjectionStep& step, size_t row) {
    check(step.status == ProjectionStepStatus::ready && step.page.has_value(), "missing complete page");
    return project_line(step.page->snapshot.lines.at(row)).text;
}
static std::string alphabet(size_t count) {
    std::string result;
    for (size_t i = 0; i < count; ++i) result += static_cast<char>('A' + i % 26);
    return result;
}

static void long_soft_row_progresses() {
    Parser parser(1, 1, 1000);
    const auto expected = alphabet(500);
    parser.feed(expected); parser.finish();
    parser.screen.resize_columns_preserving(500);
    check(parser.screen.retained_size() == 500, "fixture did not create 500 source fragments");
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 500, 1);
    yielded(viewport.step_projection(parser.screen, job), 200);
    yielded(viewport.step_projection(parser.screen, job), 200);
    check(viewport.layout_revision() == 0, "yield published a layout");
    const auto final = viewport.step_projection(parser.screen, job);
    check(final.source_rows_read == 100 && final.page && final.page->source_rows_read == 500 &&
          final.page->snapshot.lines.size() == 1 && final.page->next.row == 500 &&
          final.page->stop == ProjectionStop::source_end && viewport.layout_revision() == 1,
          "500-fragment row did not finish in 200+200+100 bounded steps");
    check(row_text(final, 0) == expected, "incremental source text was lost or duplicated");
    TextDocument document(parser.screen);
    for (const size_t column : {size_t{0}, size_t{199}, size_t{200}, size_t{499}}) {
        const auto hit = final.page->snapshot.mapping.hit(document, 0, column, ViewAffinity::leading, 1);
        check(hit && hit->line_id == parser.screen.retained_line(column).id && hit->column == 0,
              "source mapping did not survive the step boundary");
    }
    rejects([&] { viewport.step_projection(parser.screen, job); });
}

static void complete_rows_wait_for_the_page() {
    Parser parser(2, 1, 0); parser.feed("AB");
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 1, 2);
    yielded(viewport.step_projection(parser.screen, job, 1), 1);
    check(viewport.layout_revision() == 0, "one complete row leaked from an incomplete page");
    auto final = viewport.step_projection(parser.screen, job, 1);
    check(final.page && final.page->snapshot.lines.size() == 2 && final.page->source_rows_read == 2,
          "completed row was dropped between steps");
    check(row_text(final, 0) == "A" && row_text(final, 1) == "B", "page text differs");
}

static void selection_and_find_cross_step_boundary() {
    Parser parser(1, 1, 1000);
    parser.feed(alphabet(500)); parser.finish();
    parser.screen.resize_columns_preserving(500);
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 500, 1);
    yielded(viewport.step_projection(parser.screen, job), 200);
    yielded(viewport.step_projection(parser.screen, job), 200);
    const auto final = viewport.step_projection(parser.screen, job);
    check(final.status == ProjectionStepStatus::ready && final.page &&
          final.source_rows_read == 100 && final.page->source_rows_read == 500,
          "selection fixture did not finish in 200+200+100 bounded steps");

    // This literal comes from the repeating A..Z input at source ranks 193..212,
    // independently of the projected page or either reader's returned text.
    const std::string expected = "LMNOPQRSTUVWXYZABCDE";
    TextDocument document(parser.screen);
    const auto revision = viewport.layout_revision();
    const auto& mapping = final.page->snapshot.mapping;
    const auto first = mapping.hit(document, 0, 193, ViewAffinity::leading, revision);
    const auto last = mapping.hit(document, 0, 213, ViewAffinity::leading, revision);
    check(first && last && *first == *document.position(193, 0) &&
          *last == *document.position(213, 0), "carried mapping lost reverse-drag endpoints");
    TextSelection selection;
    check(selection.set(document, {*last, *first}), "reverse selection across step boundary failed");
    const auto copied = selection.copy(document, 20);
    check(copied.status == SelectionCopyStatus::ok && copied.text == expected,
          "reverse selection did not copy the exact 20 source characters");
    const auto single_span = [](const std::vector<ViewCellSpan>& spans) {
        check(spans.size() == 1 && spans[0].row == 0 && spans[0].column_begin == 193 &&
              spans[0].column_end == 213, "source range did not map to one display span 193..213");
    };
    single_span(mapping.project(document, *selection.range(), revision));

    TextSearch search;
    const auto generation = search.start(document, expected);
    for (int i = 0; i < 100 && search.status() == SearchStatus::running; ++i)
        search.step(document, generation, 64);
    check(search.status() == SearchStatus::complete, "Find did not finish its bounded source scan");
    bool found = false;
    for (size_t i = 0; i < search.matches().size(); ++i) {
        const auto match = search.match(document, i);
        if (!match || document.index_of(match->range.begin.line_id) != size_t{193}) continue;
        check(match->range.begin == *document.position(193, 0) &&
              match->range.end == *document.position(212, 1), "Find returned the wrong source range");
        single_span(mapping.project(document, match->range, revision));
        TextSelection found_selection;
        check(found_selection.set(document, match->range), "Find range could not be selected");
        const auto found_copy = found_selection.copy(document, 20);
        check(found_copy.status == SelectionCopyStatus::ok && found_copy.text == expected,
              "Find range did not copy the independent source literal");
        found = true;
    }
    check(found, "Find omitted the source match starting at rank 193");
}

static void earlier_content_change_invalidates() {
    Parser parser(4, 1, 0); parser.feed("ABCD");
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 4, 1);
    yielded(viewport.step_projection(parser.screen, job, 2), 2);
    const auto next_version = parser.screen.retained_line(2).version;
    const auto order = parser.screen.order_revision();
    parser.feed("\x1b[1;1H!");
    check(parser.screen.order_revision() == order && parser.screen.retained_line(2).version == next_version,
          "fixture changed the next row or document order");
    rejects([&] { viewport.step_projection(parser.screen, job); });
    rejects([&] { viewport.step_projection(parser.screen, job); });
    check(viewport.layout_revision() == 0, "stale partial content was published");
}

static void same_identity_import_invalidates() {
    Parser parser(4, 1, 0); parser.feed("ABCD");
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 4, 1);
    yielded(viewport.step_projection(parser.screen, job, 2), 2);
    auto rows = parser.screen.visible_lines();
    const auto first_version = rows[0].version, order = parser.screen.order_revision();
    rows[0][0].text = "!";
    parser.screen.publish({}, std::move(rows));
    check(parser.screen.retained_line(0).version == first_version && parser.screen.order_revision() == order,
          "same-id/version import unexpectedly changed the structural fixture");
    rejects([&] { viewport.step_projection(parser.screen, job); });
    check(viewport.layout_revision() == 0, "imported content revived pending copied values");
}

static void owners_and_source_cursors_are_bound() {
    Parser a(2, 1, 0), b(2, 1, 0); a.feed("AB"); b.feed("AB");
    HistoryViewport viewport, foreign;
    auto job = viewport.begin_projection(a.screen, 2, 1);
    rejects([&] { foreign.step_projection(a.screen, job); });
    foreign.cancel_projection(job);
    auto moved = std::move(job);
    rejects([&] { viewport.step_projection(a.screen, job); });
    check(row_text(viewport.step_projection(a.screen, moved), 0) == "AB", "foreign cancel damaged the job");
    auto wrong_source = viewport.begin_projection(a.screen, 2, 1);
    rejects([&] { viewport.step_projection(b.screen, wrong_source); });
    rejects([&] { viewport.step_projection(a.screen, wrong_source); });
    const auto first = viewport.project(a.screen, 1, 1, {0, 0}, 2);
    auto keep = viewport.begin_projection(a.screen, 2, 1);
    rejects([&] { (void)viewport.begin_projection(b.screen, 1, 1, first.next); });
    rejects([&] { (void)viewport.project(b.screen, 1, 1, first.next, 2); });
    check(row_text(viewport.step_projection(a.screen, keep), 0) == "AB", "failed foreign cursor prepare superseded job");
    auto own = viewport.begin_projection(a.screen, 1, 1);
    auto page = viewport.step_projection(a.screen, own);
    rejects([&] { (void)foreign.begin_projection(b.screen, 1, 1, page.page->next); });
}

static void request_supersession_and_cancellation() {
    Parser parser(4, 1, 0); parser.feed("ABCD");
    HistoryViewport viewport;
    auto old = viewport.begin_projection(parser.screen, 4, 1);
    yielded(viewport.step_projection(parser.screen, old, 1), 1);
    rejects([&] { (void)viewport.begin_projection(parser.screen, 0, 1); });
    rejects([&] { (void)viewport.project(parser.screen, 4, 1, {0, 0}, 201); });
    yielded(viewport.step_projection(parser.screen, old, 1), 1);
    auto current = viewport.begin_projection(parser.screen, 4, 1);
    rejects([&] { viewport.step_projection(parser.screen, old); });
    viewport.cancel_projection(old);
    check(row_text(viewport.step_projection(parser.screen, current), 0) == "ABCD", "old cancellation damaged new request");
    auto cancelled = viewport.begin_projection(parser.screen, 4, 1);
    viewport.cancel_projection(cancelled);
    rejects([&] { viewport.step_projection(parser.screen, cancelled); });
    auto superseded = viewport.begin_projection(parser.screen, 4, 1);
    (void)viewport.project(parser.screen, 4, 1, {0, 0}, 4);
    rejects([&] { viewport.step_projection(parser.screen, superseded); });
}

static void resize_guards_distinguish_noops() {
    Parser parser(4, 1, 0); parser.feed("ABCD");
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 4, 1);
    yielded(viewport.step_projection(parser.screen, job, 1), 1);
    parser.screen.resize_columns_preserving(1); parser.screen.resize(4, 1);
    check(row_text(viewport.step_projection(parser.screen, job), 0) == "ABCD", "same-size resize cancelled a valid job");
    auto changed = viewport.begin_projection(parser.screen, 4, 1);
    parser.screen.resize_columns_preserving(2);
    rejects([&] { viewport.step_projection(parser.screen, changed); });
    auto physical = viewport.begin_projection(parser.screen, 4, 1);
    parser.screen.resize(3, 2);
    rejects([&] { viewport.step_projection(parser.screen, physical); });
}

static void empty_soft_fragments_do_not_add_rows() {
    Screen screen(3, 1, 0);
    auto rows = screen.visible_lines(); rows[0][0].text = "A"; rows[0][0].written = true;
    rows[0].ending = LineEnding::soft_wrap; rows[0].wrap_columns = 1; rows[0].next_id = rows[1].id;
    rows[1].ending = LineEnding::soft_wrap; rows[1].next_id = rows[2].id;
    screen.publish({}, std::move(rows));
    HistoryViewport viewport;
    auto job = viewport.begin_projection(screen, 1, 2);
    yielded(viewport.step_projection(screen, job, 1), 1);
    yielded(viewport.step_projection(screen, job, 1), 1);
    auto last = viewport.step_projection(screen, job, 1);
    check(last.page && last.page->snapshot.lines.size() == 1 && row_text(last, 0) == "A",
          "soft empty suffix fabricated a hard display row");
    auto first_job = viewport.begin_projection(screen, 1, 1);
    auto first = viewport.step_projection(screen, first_job, 1);
    auto next_job = viewport.begin_projection(screen, 1, 1, first.page->next);
    yielded(viewport.step_projection(screen, next_job, 1), 1);
    yielded(viewport.step_projection(screen, next_job, 1), 1);
    auto tail = viewport.step_projection(screen, next_job, 1);
    check(tail.page && tail.page->snapshot.lines.empty() && tail.page->stop == ProjectionStop::source_end,
          "forward page invented a line after a soft empty suffix");
}

static void complete_unicode_atoms_across_pages() {
    Parser parser(1, 6, 0); parser.feed("\x1b[31;4me\xcc\x81\xe7\x95\x8cZ");
    HistoryViewport viewport;
    auto first_job = viewport.begin_projection(parser.screen, 2, 1);
    auto first = viewport.step_projection(parser.screen, first_job, 1);
    check(row_text(first, 0) == "e\xcc\x81" && first.page->next.column == 1 &&
          first.page->snapshot.lines[0][0].marks == 1 && first.page->snapshot.lines[0][0].style.underline,
          "mark/style or pre-wide padding was lost");
    auto second_job = viewport.begin_projection(parser.screen, 2, 1, first.page->next);
    auto second = viewport.step_projection(parser.screen, second_job, 1);
    check(row_text(second, 0) == "\xe7\x95\x8c" && second.page->snapshot.lines[0][0].width == 2 &&
          second.page->snapshot.lines[0][1].width == 0 && second.page->next.column == 3,
          "wide atom split or duplicated across pages");
    auto third_job = viewport.begin_projection(parser.screen, 2, 1, second.page->next);
    auto third = viewport.step_projection(parser.screen, third_job, 1);
    check(row_text(third, 0) == "Z" && third.page->stop == ProjectionStop::source_end,
          "last source suffix was not resumed exactly");
    TextDocument document(parser.screen);
    check(!first.page->snapshot.mapping.hit(document, 0, 0, ViewAffinity::leading, viewport.layout_revision()),
          "previous published layout mapping remained current");
    // A returned cursor names source, not a global display-row checkpoint.
    // Starting a different width at it intentionally defines a new local origin.
    auto different_width = viewport.begin_projection(parser.screen, 3, 1, first.page->next);
    auto suffix = viewport.step_projection(parser.screen, different_width, 1);
    check(row_text(suffix, 0) == "\xe7\x95\x8cZ" && suffix.page->stop == ProjectionStop::source_end,
          "source anchor incorrectly required the previous display width");
}

static void tail_seek_does_not_scan_the_prefix() {
    Parser parser(1, 1, 1000); const auto text = alphabet(500); parser.feed(text);
    std::vector<Line> history(parser.screen.history_lines().begin(), parser.screen.history_lines().end());
    auto live = parser.screen.visible_lines(); history[0][0].text = "\xff";
    parser.screen.publish(std::move(history), std::move(live));
    HistoryViewport viewport;
    auto job = viewport.begin_projection(parser.screen, 1, 1, {400, 0});
    auto page = viewport.step_projection(parser.screen, job, 1);
    check(page.source_rows_read == 1 && row_text(page, 0) == text.substr(400, 1),
          "tail request scanned an unrelated malformed historical prefix");
}

static void exceptions_cancel_without_publication() {
    Parser parser(2, 1, 0); parser.feed("AB");
    HistoryViewport viewport;
    for (const size_t budget : {size_t{0}, size_t{201}}) {
        auto job = viewport.begin_projection(parser.screen, 2, 1);
        rejects([&] { viewport.step_projection(parser.screen, job, budget); });
        rejects([&] { viewport.step_projection(parser.screen, job); });
    }
    auto rows = parser.screen.visible_lines(); rows[1][0].text = "\xff";
    parser.screen.publish({}, std::move(rows));
    auto job = viewport.begin_projection(parser.screen, 1, 2);
    yielded(viewport.step_projection(parser.screen, job, 1), 1);
    rejects([&] { viewport.step_projection(parser.screen, job, 1); });
    check(viewport.layout_revision() == 0, "exception published an already completed partial-page row");
    rejects([&] { viewport.step_projection(parser.screen, job); });
}

static void epoch_and_structure_invalidate() {
    Parser parser(4, 1, 0); parser.feed("ABCD");
    HistoryViewport viewport;
    auto epoch_job = viewport.begin_projection(parser.screen, 4, 1);
    parser.screen.activate_buffer(ScreenBuffer::alternate);
    parser.screen.activate_buffer(ScreenBuffer::primary);
    rejects([&] { viewport.step_projection(parser.screen, epoch_job); });
    auto order_job = viewport.begin_projection(parser.screen, 4, 1);
    parser.feed("\x1b[2;1H\x1b[L");
    rejects([&] { viewport.step_projection(parser.screen, order_job); });
}

int main() {
    const std::pair<const char*, void(*)()> cases[] = {
        {"500-fragment-progress", long_soft_row_progresses},
        {"selection-find-step-boundary", selection_and_find_cross_step_boundary},
        {"unpublished-page", complete_rows_wait_for_the_page},
        {"earlier-content", earlier_content_change_invalidates},
        {"same-id-import", same_identity_import_invalidates},
        {"owner-bindings", owners_and_source_cursors_are_bound},
        {"request-cancel", request_supersession_and_cancellation},
        {"resize-guards", resize_guards_distinguish_noops},
        {"empty-soft-suffix", empty_soft_fragments_do_not_add_rows},
        {"unicode-page-boundary", complete_unicode_atoms_across_pages},
        {"no-history-prefix-scan", tail_seek_does_not_scan_the_prefix},
        {"exception-contract", exceptions_cancel_without_publication},
        {"epoch-order", epoch_and_structure_invalidate}
    };
    size_t passed = 0;
    for (const auto& [name, run] : cases) {
        try { run(); ++passed; }
        catch (const std::exception& error) { std::cerr << name << ": " << error.what() << '\n'; }
    }
    std::cout << "incremental projection jobs: " << passed << '/' << std::size(cases)
              << " engineering cases (no original or Qt acceptance claim)\n";
    return passed == std::size(cases) ? 0 : 1;
}
