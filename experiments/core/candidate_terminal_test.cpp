#include "candidate_terminal.hpp"
#include "history_view.hpp"
#include "text_document.hpp"
#include "text_search.hpp"
#include "text_selection.hpp"
#include <iostream>
#include <sstream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string text(const termforge::Line& line) {
    std::string value;
    for (const auto& cell : line) value += cell.text;
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

std::string state(const termforge::CandidateTerminal& terminal) {
    std::ostringstream out;
    terminal.screen.json(out);
    return out.str();
}

void fragmentation() {
    const std::string data = "A中e\xCC\x81😀\r\nB\r\nC\r\nD";
    termforge::CandidateTerminal whole(3, 8, 2), byte(3, 8, 2);
    whole.feed(data);
    for (char value : data) byte.feed(std::string_view(&value, 1));
    whole.finish(); byte.finish();
    require(state(whole) == state(byte), "fragmented candidate state differs");
    require(whole.screen.history[0][0].width == 1, "unexpected narrow cell");
    require(whole.screen.history[0][1].width == 2, "unexpected wide cell");
    require(whole.screen.history[0][2].width == 0 && whole.screen.history[0][2].written,
            "wide character spacer was not preserved as written");
    require(whole.screen.history[0][3].marks == 1, "combining mark missing");
}

void alternate_and_replies() {
    termforge::CandidateTerminal terminal(3, 8, 2);
    terminal.feed("A\r\nB\r\nC\r\nD\x1b[?1049hALT\x1b[6n\x1b[?1049lZ");
    require(text(terminal.screen.lines[0]) == "B" && text(terminal.screen.lines[1]) == "C" &&
            text(terminal.screen.lines[2]) == "DZ", "alternate screen did not restore main content");
    require(terminal.screen.history.size() == 1 && text(terminal.screen.history[0]) == "A",
            "alternate screen polluted history");
    require((terminal.mode_bits() & (1 << 12)) == 0, "alternate mode remained enabled");
    const auto events = terminal.take_events();
    require(events.size() == 1 && events[0].kind == 1 && events[0].bytes == "\x1b[3;5R",
            "cursor reply was not preserved");
    require(terminal.take_events().empty(), "candidate events were not drained");
}

void identity_versions_and_eviction() {
    termforge::CandidateTerminal terminal(2, 4, 1);
    const auto first = terminal.screen.lines[0].id;
    const auto second = terminal.screen.lines[1].id;
    terminal.feed("A");
    require(terminal.screen.lines[0].id == first && terminal.screen.lines[0].version == 2,
            "cell update did not preserve identity and increment version");
    terminal.feed("");
    require(terminal.screen.lines[0].version == 2, "empty feed incremented version");
    terminal.feed("\r\nB\r\nC\r\n");
    require(terminal.screen.history.size() == 1 && text(terminal.screen.history[0]) == "B",
            "bounded history retained wrong row");
    require(terminal.screen.discarded == 1, "history eviction count is wrong");
    require(terminal.screen.history[0].id == second, "scrolled row lost its identity");
    require(terminal.screen.lines[1].id != first && terminal.screen.lines[1].version == 1,
            "reused row did not receive a fresh identity");
    termforge::TextDocument document(terminal.screen);
    require(document.index_of(second) == std::optional<size_t>(0), "text document cannot resolve scrolled identity");
    require(!document.index_of(first), "evicted identity remains resolvable");
}

void selection_search_and_viewport() {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("alpha\r\nbeta\r\ngamma");
    termforge::TextDocument document(terminal.screen);
    require(document.line(0).projection.text == "alpha", "candidate text projection differs");
    require(terminal.screen.history[0].id < terminal.screen.lines[0].id &&
            terminal.screen.lines[0].id < terminal.screen.lines[1].id, "candidate retained IDs are not ordered");
    termforge::TextSelection selection;
    const auto begin = document.position(0, 0), end = document.position(0, 5);
    require(begin && end, "candidate text positions were not generated");
    require(document.valid(*begin) && document.valid(*end), "candidate text positions are not live");
    require(selection.set(document, {*begin, *end}), "candidate range selection failed");
    const auto copied = selection.copy(document, 1024);
    require(copied.status == termforge::SelectionCopyStatus::ok && copied.text == "alpha",
            "candidate selection copy differs");

    termforge::TextSearch search;
    const auto generation = search.start(document, "gamma");
    for (int i = 0; i < 10000 && search.status() == termforge::SearchStatus::running; ++i)
        search.step(document, generation, 1);
    require(search.status() == termforge::SearchStatus::complete && search.matches().size() == 1 &&
            search.match(document, 0), "candidate search did not resolve gamma");

    termforge::HistoryViewport viewport;
    const auto top = viewport.scroll_to(terminal.screen, 0);
    require(top.anchor == terminal.screen.history[0].id && !top.following,
            "candidate history top anchor differs");
    const auto bottom = viewport.bottom(terminal.screen);
    require(bottom.following && bottom.anchor == terminal.screen.lines[0].id,
            "candidate history bottom anchor differs");
}

void linebreaks() {
    termforge::CandidateTerminal terminal(2, 3, 2);
    terminal.feed("abcd");
    const auto& row = terminal.screen.lines[0];
    require(row.ending == termforge::LineEnding::soft_wrap && row.wrap_columns == 3,
            "soft wrap metadata missing");
    require(row.next_id == terminal.screen.lines[1].id &&
            termforge::continues_into(row, terminal.screen.lines[1]), "soft wrap continuation is broken");
    terminal.feed("\r\n");
    require(terminal.screen.lines[0].ending == termforge::LineEnding::hard_break,
            "hard break metadata missing");
}

void scrolling_and_erase() {
    termforge::CandidateTerminal terminal(3, 8, 2);
    terminal.feed("A\r\nB\r\nC\r\nD\x1b[2;3r\x1b[3;1H\nX\x1b[r\x1b[1;1H");
    require(text(terminal.screen.lines[0]) == "B" && text(terminal.screen.lines[1]) == "D" &&
            text(terminal.screen.lines[2]) == "X", "scroll region result differs");

    termforge::CandidateTerminal erase(3, 12, 3);
    erase.feed("one\r\ntwo\x1b[2;1H\x1b[1J");
    require(text(erase.screen.lines[0]).empty() && text(erase.screen.lines[1]) == "wo",
            "ED1 did not clear page start through cursor");
    termforge::CandidateTerminal clear(3, 12, 3);
    clear.feed("A\r\nB\r\nC\r\nD\x1b[2J");
    require(clear.screen.history.size() == 1 && text(clear.screen.history[0]) == "A",
            "ED2 unexpectedly migrated or cleared history");
}

void resize_and_errors() {
    termforge::CandidateTerminal terminal(3, 8, 2);
    terminal.feed("abc中\r\n1234567890");
    terminal.resize(2, 6);
    require(terminal.screen.height() == 2 && terminal.screen.width() == 6, "candidate resize dimensions differ");
    require(text(terminal.screen.lines[0]) == "123456" && text(terminal.screen.lines[1]) == "7890",
            "candidate reflow projection differs");

    bool rejected = false;
    try { termforge::CandidateTerminal invalid(0, 8, 2); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid dimensions were accepted");
    rejected = false;
    try { terminal.resize(0, 6); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid resize was accepted");
    rejected = false;
    try { terminal.feed(std::string(1024 * 1024 + 1, 'x')); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "oversized feed was accepted");
}
} // namespace

int main() {
    try {
        fragmentation();
        alternate_and_replies();
        identity_versions_and_eviction();
        selection_search_and_viewport();
        linebreaks();
        scrolling_and_erase();
        resize_and_errors();
        std::cout << "candidate terminal adapter checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
