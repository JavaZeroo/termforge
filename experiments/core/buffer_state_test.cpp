#include "terminal.hpp"
#include "text_document.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include "history_view.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string row_text(const Line& line) {
    std::string value;
    for (const auto& cell : line) value += cell.text;
    return value;
}
static std::string snapshot(const Screen& screen) {
    std::ostringstream out;
    screen.json(out);
    return out.str();
}
static void independent_text_and_cursor() {
    Parser parser(2, 8, 2);
    parser.feed("MAIN\r\nBASE\r\nEND");
    auto& screen = parser.screen;
    const auto main_top = screen.lines.front().id;
    const auto main_bottom = screen.lines.back().id;
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    screen.move(1, 1);
    const auto alternate_top = screen.lines.front().id;
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.active_buffer() == ScreenBuffer::primary && screen.row == 1 && screen.column == 3,
          "primary cursor was not restored");
    check(screen.history.size() == 1 && row_text(screen.history.front()) == "MAIN    " &&
          row_text(screen.lines[0]) == "BASE    " && row_text(screen.lines[1]) == "END     ",
          "primary retained text was changed by alternate output");
    check(screen.lines.front().id == main_top && screen.lines.back().id == main_bottom,
          "activation replaced saved primary row identities");
    check(alternate_top > main_bottom, "alternate rows reused primary row identities");
    parser.feed("!");
    screen.activate_buffer(ScreenBuffer::alternate);
    check(screen.row == 1 && screen.column == 1 && row_text(screen.lines.front()) == "ALT     ",
          "alternate text or cursor was not preserved");
    screen.activate_buffer(ScreenBuffer::primary);
    check(row_text(screen.lines.back()) == "END!    ", "alternate activation overwrote later primary output");
}
static void writing_context_roundtrip() {
    Parser parser(2, 4, 2);
    parser.feed("\x1b[31;44mABCD");
    auto& screen = parser.screen;
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.move(0, 0);
    parser.feed("\x1b[32;45mZ");
    screen.move(1, 1);
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.row == 0 && screen.column == 3 && screen.wrap_pending &&
          screen.style.fg == 1 && screen.style.bg == 4,
          "activation lost the saved writing context");
    parser.feed("\xcc\x81");
    check(screen.lines[0][3].text == "D\xcc\x81" && screen.lines[0][3].marks == 1,
          "restored combining attachment did not target its saved base");
    parser.feed("E");
    check(row_text(screen.lines[0]) == "ABCD\xcc\x81" && row_text(screen.lines[1]) == "E   " &&
          screen.lines[0].ending == LineEnding::soft_wrap && screen.lines[1][0].style.fg == 1 &&
          screen.lines[1][0].style.bg == 4,
          "restored pending wrap did not resume with saved text and style");
    screen.activate_buffer(ScreenBuffer::alternate);
    check(screen.row == 1 && screen.column == 1 && screen.style.fg == 2 && screen.style.bg == 5,
          "primary output changed the alternate cursor or style");
}
static void bounded_independent_history() {
    Parser parser(2, 8, 2);
    parser.feed("ONE\r\nTWO\r\nTHREE\r\nFOUR\r\nFIVE");
    auto& screen = parser.screen;
    const auto main_last = screen.lines.back().id;
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("A\r\nB\r\nC\r\nD\r\nE");
    const auto alternate_last = screen.lines.back().id;
    check(screen.history.empty() && screen.discarded == 3,
          "alternate retention was not bounded independently of primary history");
    check(alternate_last > main_last, "alternate scrolling recycled line identities");
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.history.size() == 2 && screen.discarded == 1 &&
          row_text(screen.history[0]) == "TWO     " && row_text(screen.history[1]) == "THREE   " &&
          row_text(screen.lines[0]) == "FOUR    " && row_text(screen.lines[1]) == "FIVE    ",
          "alternate scrolling changed saved primary history or eviction state");
    parser.feed("\r\nSIX");
    check(screen.history.size() == 2 && screen.discarded == 2 &&
          row_text(screen.history.front()) == "THREE   " && screen.lines.back().id > alternate_last,
          "primary scrolling lost its budget or global identity monotonicity");
}
static void resize_both_frames_and_rollback() {
    Parser parser(3, 6, 2);
    parser.feed("A\xe4\xb8\xad" "B\r\nCDEF\r\nGHIJ");
    auto& screen = parser.screen;
    const auto main_first = screen.lines.front().id;
    const auto main_second = screen.lines[1].id;
    const auto main_third = screen.lines[2].id;
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    screen.move(2, 5);
    screen.resize(2, 3);
    check(screen.height() == 2 && screen.width() == 3 && screen.history.empty() && screen.discarded == 1,
          "alternate resize used primary retention");
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.height() == 2 && screen.width() == 3 && screen.row == 1 && screen.column == 2 &&
          screen.history.size() == 1 && screen.history[0].id == main_first &&
          screen.lines[0].id == main_second && screen.lines[1].id == main_third &&
          row_text(screen.history[0]) == "A\xe4\xb8\xad" &&
          row_text(screen.lines[0]) == "CDE" && row_text(screen.lines[1]) == "GHI",
          "resize did not preserve the hidden primary frame's physical-row policy");
    const auto primary = snapshot(screen);
    screen.activate_buffer(ScreenBuffer::alternate);
    const auto alternate = snapshot(screen);
    bool rejected = false;
    try { screen.resize(2, 0); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && snapshot(screen) == alternate && screen.width() == 3,
          "invalid resize partially changed the active frame");
    screen.activate_buffer(ScreenBuffer::primary);
    check(snapshot(screen) == primary, "invalid resize partially changed the hidden frame");
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.resize(2, 2);
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.history[0].size() == 2 && screen.history[0][1].width == 1 &&
          screen.history[0][1].text == " " && screen.history[0].cropped,
          "hidden-frame width crop left half of a wide glyph");
}
static void growth_preserves_metadata_and_unique_ids() {
    Parser parser(3, 4, 2);
    parser.feed("ABCDxy\r\nZ");
    auto& screen = parser.screen;
    const auto soft = screen.lines[0], hard = screen.lines[1];
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    const auto alternate_last = screen.lines.back().id;
    screen.resize(5, 6);
    const auto alternate_new_first = screen.lines[3].id, alternate_new_last = screen.lines[4].id;
    check(alternate_new_first > alternate_last && alternate_new_last > alternate_new_first &&
          row_text(screen.lines[0]) == "ALT   " && row_text(screen.lines.back()) == "      ",
          "alternate growth did not append fresh default rows");
    screen.activate_buffer(ScreenBuffer::primary);
    TextDocument document(screen);
    check(screen.lines[0].id == soft.id && screen.lines[0].version == soft.version + 1 &&
          screen.lines[0].ending == LineEnding::soft_wrap && screen.lines[0].next_id == soft.next_id &&
          screen.lines[0].wrap_columns == soft.wrap_columns && document.meta(0).joins_next &&
          screen.lines[1].id == hard.id && screen.lines[1].ending == LineEnding::hard_break &&
          !document.meta(1).joins_next,
          "hidden growth changed soft-wrap links or hard-break metadata");
    check(screen.lines[3].id > alternate_new_last && screen.lines[4].id > screen.lines[3].id &&
          row_text(screen.lines[0]) == "ABCD  " && row_text(screen.lines[1]) == "xy    " &&
          row_text(screen.lines.back()) == "      ",
          "two-frame growth reused ids or reflowed physical rows");
}
static void hidden_resize_failure_rolls_back_both_frames() {
    Parser parser(2, 4, 2);
    parser.feed("MAIN");
    auto& screen = parser.screen;
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    const auto alternate = snapshot(screen);
    screen.activate_buffer(ScreenBuffer::primary);
    const auto primary = snapshot(screen);
    // Reserve imported identities near exhaustion. Active growth can prepare
    // one row, but hidden growth cannot allocate its own row afterwards.
    auto imported = screen.lines;
    imported.resize(1);
    imported[0].id = (std::numeric_limits<uint64_t>::max)() - 2;
    screen.normalize_identities(imported);
    bool rejected = false;
    try { screen.resize(3, 4); } catch (const std::overflow_error&) { rejected = true; }
    check(rejected && screen.height() == 2 && snapshot(screen) == primary,
          "hidden resize failure committed active geometry or content");
    screen.activate_buffer(ScreenBuffer::alternate);
    check(screen.height() == 2 && snapshot(screen) == alternate,
          "hidden resize failure changed the alternate frame");
    screen.activate_buffer(ScreenBuffer::primary);
    parser.feed("\r\nA\r\nB");
    check(screen.lines.back().id == (std::numeric_limits<uint64_t>::max)() - 1,
          "failed hidden resize consumed the shared identity allocator");
}
static void positions_do_not_revive() {
    Parser parser(2, 8, 2);
    parser.feed("MAIN");
    auto& screen = parser.screen;
    TextDocument document(screen);
    const auto original = *document.position(0, 0);
    check(document.valid(TextPosition{original.line_id, original.version, original.column}),
          "initial document no longer accepts the legacy position shape");
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    screen.activate_buffer(ScreenBuffer::primary);
    check(screen.lines[0].id == original.line_id && screen.lines[0].version == original.version,
          "activation changed primary content identity to invalidate positions");
    check(!document.valid(original), "old primary position revived after a buffer roundtrip");
    const auto current = *document.position(0, 0);
    check(document.valid(current) && current != original, "new document position lost its buffer context");
    screen.activate_buffer(ScreenBuffer::primary);
    check(document.valid(current), "idempotent storage activation invalidated current positions");
}
static void selection_does_not_revive() {
    Parser parser(2, 8, 2);
    parser.feed("MAIN");
    auto& screen = parser.screen;
    TextDocument document(screen);
    TextSelection selection;
    check(selection.set(document, {*document.position(0, 0), *document.position(0, 4)}) &&
          selection.copy(document, 1024).text == "MAIN", "primary selection setup failed");
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.activate_buffer(ScreenBuffer::primary);
    check(selection.copy(document, 1024).status == SelectionCopyStatus::invalidated && selection.empty(),
          "selection reconciled an old range into the returned primary frame");
    check(selection.set(document, {*document.position(0, 0), *document.position(0, 4)}) &&
          selection.copy(document, 1024).text == "MAIN", "fresh selection failed after activation");
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    check(selection.copy(document, 1024).status == SelectionCopyStatus::invalidated && selection.empty(),
          "a primary selection stayed usable in the alternate frame");
    check(selection.set(document, {*document.position(0, 0), *document.position(0, 3)}) &&
          selection.copy(document, 1024).text == "ALT", "fresh alternate selection failed");
    screen.activate_buffer(ScreenBuffer::primary);
    check(!selection.reconcile(document), "an alternate selection stayed usable in the primary frame");
}
static void finish_search(TextSearch& search, const TextDocument& document, uint64_t generation) {
    for (int step = 0; search.status() == SearchStatus::running && step < 100; ++step)
        search.step(document, generation, 64);
    check(search.status() == SearchStatus::complete, "bounded search did not finish");
}
static void search_does_not_revive() {
    Parser parser(2, 8, 2);
    parser.feed("MAIN");
    auto& screen = parser.screen;
    TextDocument document(screen);
    TextSearch search;
    auto generation = search.start(document, "MAIN");
    finish_search(search, document, generation);
    check(search.matches().size() == 1 && search.match(document, 0).has_value(), "primary search setup failed");
    screen.activate_buffer(ScreenBuffer::alternate);
    parser.feed("ALT");
    check(!search.match(document, 0), "a primary search match stayed usable in the alternate frame");
    generation = search.start(document, "ALT");
    finish_search(search, document, generation);
    check(search.matches().size() == 1 && search.match(document, 0).has_value(),
          "fresh alternate search positions did not carry the active document context");
    screen.activate_buffer(ScreenBuffer::primary);
    search.notify_change(document);
    check(search.status() == SearchStatus::invalidated && search.matches().empty(),
          "search kept an old query scan after a buffer roundtrip");
    generation = search.start(document, "MAIN");
    finish_search(search, document, generation);
    check(search.matches().size() == 1 && search.match(document, 0).has_value(),
          "fresh search positions did not carry the active document context");
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.activate_buffer(ScreenBuffer::primary);
    check(!search.match(document, 0), "completed search match revived before notification");
    const auto complete_step = search.step(document, generation, 0);
    check(!complete_step.accepted && complete_step.status == SearchStatus::invalidated && search.matches().empty(),
          "zero-budget step did not invalidate a completed search after activation");
    generation = search.start(document, "MAIN");
    search.step(document, generation, 2);
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.activate_buffer(ScreenBuffer::primary);
    const auto running_step = search.step(document, generation, 64);
    check(!running_step.accepted && running_step.status == SearchStatus::invalidated && search.matches().empty(),
          "a partial scan continued across activation without notification");
}
static void viewport_does_not_revive() {
    Parser parser(2, 8, 3);
    parser.feed("ONE\r\nTWO\r\nTHREE\r\nFOUR");
    auto& screen = parser.screen;
    HistoryViewport viewport;
    const auto paused = viewport.scroll_to(screen, 0);
    check(!paused.following && paused.top == 0, "primary viewport setup failed");
    screen.activate_buffer(ScreenBuffer::alternate);
    HistoryViewport new_view;
    const auto new_frame = new_view.snapshot(screen);
    check(new_frame.viewport.following && new_frame.viewport.top == 0 && new_frame.cursor_row == screen.row,
          "a new viewport did not initialize in a nonzero epoch");
    check(new_view.scroll_to(screen, 0).following, "a new alternate viewport could not scroll");
    screen.activate_buffer(ScreenBuffer::primary);
    const auto resumed = viewport.snapshot(screen);
    check(resumed.viewport.following && resumed.viewport.top == 2 && resumed.cursor_row == screen.row &&
          row_text(resumed.lines.front()) == "THREE   ",
          "a paused history viewport revived after a buffer roundtrip");
    screen.activate_buffer(ScreenBuffer::alternate);
    screen.activate_buffer(ScreenBuffer::primary);
    const auto requested = viewport.scroll_to(screen, 0);
    check(!requested.following && requested.top == 0 && requested.anchor == paused.anchor,
          "epoch reset overrode an explicit scroll in the current frame");
    HistoryViewport new_primary_view;
    check(new_primary_view.snapshot(screen).viewport.following &&
          !new_primary_view.scroll_to(screen, 0).following && new_primary_view.snapshot(screen).viewport.top == 0,
          "a new primary viewport could not follow and scroll in a nonzero epoch");
}

int main() {
    try {
        independent_text_and_cursor();
        writing_context_roundtrip();
        bounded_independent_history();
        resize_both_frames_and_rollback();
        growth_preserves_metadata_and_unique_ids();
        hidden_resize_failure_rolls_back_both_frames();
        positions_do_not_revive();
        selection_does_not_revive();
        search_does_not_revive();
        viewport_does_not_revive();
        std::cout << "owned buffer state checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
