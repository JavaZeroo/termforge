#include "candidate_terminal.hpp"
#include "history_view.hpp"
#include "text_document.hpp"
#include "text_search.hpp"
#include "text_selection.hpp"
#include "text_projection.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iomanip>

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
    require(termforge::project_line(whole.screen.history[0]).text == "A中e\xCC\x81😀",
            "canonical text projection must omit wide spacer payloads and preserve literal scalars");
    for (const auto& cell : whole.screen.history[0])
        require(cell.width != 0 || cell.written, "unused cell was incorrectly treated as a wide spacer");
    require(whole.screen.history[0][7].width == 1 && whole.screen.history[0][7].text == " " &&
            !whole.screen.history[0][7].written, "unused cell must retain its paintable width");
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

void per_buffer_margins() {
    const std::string before = "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r";
    const std::string alt = "\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D";
    const std::string after = "\x1b[?1049l\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HTF_MARGIN_DONE\r\n";
    for (const bool fragmented : {false, true}) {
        termforge::CandidateTerminal terminal(24, 80, 100);
        const auto feed = [&](const std::string& data) {
            if (fragmented) {
                for (const char byte : data) terminal.feed(std::string_view(&byte, 1));
            } else terminal.feed(data);
        };
        feed(before);
        feed(alt);
        require(text(terminal.screen.lines[0]) == "ALT_OUTSIDE" && text(terminal.screen.lines[3]) == "ALT_C" &&
                text(terminal.screen.lines[4]) == "     ALT_D", "alternate scroll region differs");
        feed(after);
        const std::vector<std::string> expected = {
            "TOP_FIXED", "REGION_C", "        REGION_D", "BOTTOM_FIXED", "", "TF_MARGIN_DONE",
        };
        for (size_t row = 0; row < expected.size(); ++row)
            require(text(terminal.screen.lines[row]) == expected[row], "main scroll region was not restored");
        for (size_t row = expected.size(); row < terminal.screen.lines.size(); ++row)
            require(text(terminal.screen.lines[row]).empty(), "unexpected content after margin fixture");
        require(terminal.screen.history.empty(), "per-buffer margins polluted main history");
        require(terminal.screen.row == 6 && terminal.screen.column == 0 && !(terminal.mode_bits() & (1 << 12)),
                "margin fixture cursor or alternate mode differs");
    }
}

void margin_resize_and_reset() {
    termforge::CandidateTerminal resized(5, 8, 4);
    resized.feed("\x1b[2;3r\x1b[?1049h\x1b[4;5r");
    resized.resize(8, 8);
    resized.feed("\x1b[?1049l\x1b[1;1H1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9");
    require(resized.screen.history.size() == 1 && text(resized.screen.history[0]) == "1",
            "resize retained an inactive margin or incorrect history");
    for (size_t row = 0; row < resized.screen.lines.size(); ++row)
        require(text(resized.screen.lines[row]) == std::to_string(row + 2),
                "resize did not reset both scrolling regions");

    termforge::CandidateTerminal reset(4, 8, 4);
    reset.feed("\x1b[2;3r\x1b[?1049h\x1b[3;4r\x1b" "c");
    reset.feed("\x1b[?1049h1\r\n2\r\n3\r\n4\r\n5");
    require(reset.screen.history.empty() && text(reset.screen.lines[0]) == "2" &&
            text(reset.screen.lines[1]) == "3" && text(reset.screen.lines[2]) == "4" &&
            text(reset.screen.lines[3]) == "5", "RIS did not reset both scrolling regions");
}

void empty_payload_events() {
    termforge::CandidateTerminal terminal(3, 8, 2);
    terminal.feed("\x1b]0;title\a");
    const auto events = terminal.take_events();
    require(events.size() == 1 && events[0].kind == 2 && events[0].bytes == "title",
            "title event was not preserved");
    terminal.feed("\x1b]0;\a");
    const auto empty = terminal.take_events();
    require(empty.size() == 1 && empty[0].kind == 2 && empty[0].bytes.empty() &&
            terminal.take_events().empty(), "zero-length event was not drained exactly once");
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

std::string read_replay(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read ConPTY replay fixture");
    std::ostringstream content; content << input.rdbuf();
    return content.str();
}

std::vector<std::string> replay_sections(const std::string& data) {
    std::vector<std::string> result;
    size_t next = 0;
    while (next < data.size()) {
        const auto begin = data.find("\x1b[?25l", next);
        require(begin != std::string::npos, "replay section start is missing");
        const auto end = data.find("\x1b[?25h", begin + 6);
        require(end != std::string::npos, "replay section end is missing");
        next = end + 6;
        result.push_back(data.substr(begin, next - begin));
    }
    return result;
}

void replay_case(const std::filesystem::path& path, bool fragmented) {
    termforge::CandidateTerminal terminal(7, 111, 100);
    std::string all_expected;
    for (int index = 0; index < 80; ++index) {
        std::ostringstream line;
        line << "TF_FIND_" << std::setw(3) << std::setfill('0') << index
             << " needle \xE4\xB8\xAD\xE6\x96\x87 e\xCC\x81\r\n";
        all_expected += line.str();
        terminal.feed(line.str());
    }
    terminal.feed("TF_FIND_READY 080\r\n");
    all_expected += "TF_FIND_READY 080\r\n";
    const std::string expected = "TF_FIND_077 needle \xE4\xB8\xAD\xE6\x96\x87 e\xCC\x81";
    const auto target = [&] {
        for (const auto& line : terminal.screen.lines)
            if (text(line).rfind("TF_FIND_077", 0) == 0) return line.id;
        return uint64_t{};
    };
    const auto original = target();
    require(original != 0, "initial replay target is missing");
    termforge::TextDocument initial(terminal.screen);
    termforge::TextSelection selection, whole;
    require(selection.select_line(initial, *initial.index_of(original)), "initial replay selection failed");
    require(whole.set(initial, {*initial.position(0, 0), *initial.position(initial.size() - 1, 0)}) &&
            whole.copy(initial, 4096).text == all_expected, "initial whole-document selection differs");
    termforge::TextSearch search;
    const auto generation = search.start(initial, "TF_FIND_077");
    for (int index = 0; index < 10000 && search.status() == termforge::SearchStatus::running; ++index)
        search.step(initial, generation, 1);
    require(search.status() == termforge::SearchStatus::complete && search.matches().size() == 1,
            "initial replay search failed");
    const auto replay = replay_sections(read_replay(path));
    const std::pair<int, int> dimensions[] = {{7,112},{7,112},{7,111},{10,149}};
    require(replay.size() == std::size(dimensions), "replay dimensions differ from its sections");
    for (size_t index = 0; index < replay.size(); ++index) {
        terminal.resize(dimensions[index].first, dimensions[index].second);
        if (fragmented) {
            for (const char byte : replay[index]) {
                terminal.feed(std::string_view(&byte, 1));
                termforge::TextDocument document(terminal.screen);
                require(selection.copy(document, 1024).status == termforge::SelectionCopyStatus::ok,
                        "fragmented replay invalidated selection before completion");
            }
        } else terminal.feed(replay[index]);
        const auto current = target();
        termforge::TextDocument document(terminal.screen);
        for (size_t row = 0; row < document.size(); ++row) {
            const auto id = document.meta(row).id;
            require(id != 0 && document.index_of(id) == std::optional<size_t>(row) &&
                    (row == 0 || document.meta(row - 1).id < id),
                    "ConPTY replay did not retain ordered unique row identities");
        }
        const auto old_position = document.index_of(original);
        const auto new_position = document.index_of(current);
        require(current == original && old_position == new_position && old_position.has_value() &&
                document.line(*old_position).projection.text == expected,
                "ConPTY replay changed the logical row identity or text");
        const auto copied = selection.copy(document, 1024);
        require(copied.status == termforge::SelectionCopyStatus::ok && copied.text == expected,
                "ConPTY replay invalidated selection text");
        const auto whole_copied = whole.copy(document, 4096);
        require(whole_copied.status == termforge::SelectionCopyStatus::ok && whole_copied.text == all_expected,
                "ConPTY replay changed whole-document selected bytes or its excluded empty endpoint");
        const auto match = search.match(document, 0);
        require(match && match->range.begin.line_id == original, "ConPTY replay invalidated the Find match");
        const auto changed = search.notify_change(document);
        for (int step = 0; step < 10000 && search.status() == termforge::SearchStatus::running; ++step) {
            search.step(document, changed, 4096);
            require(search.retained_rows() <= document.size(), "ConPTY replay grows Find metadata beyond the retained document");
        }
        require(search.status() == termforge::SearchStatus::complete && search.match(document, 0),
                "ConPTY replay did not converge to a live Find result");
    }
    terminal.feed("");
    termforge::TextDocument settled(terminal.screen);
    const auto settled_copy = whole.copy(settled, 4096);
    require(settled_copy.status == termforge::SelectionCopyStatus::ok && settled_copy.text == all_expected &&
            search.match(settled, 0) && search.retained_rows() <= settled.size(),
            "restored history did not survive a subsequent synchronization");
    terminal.feed(expected + "\r\n");
    uint64_t duplicate = 0;
    for (const auto& line : terminal.screen.lines)
        if (text(line).rfind("TF_FIND_077", 0) == 0) duplicate = line.id;
    require(duplicate != 0 && duplicate != original, "duplicate text reused the original row identity");
}

void incomplete_replay_is_bounded() {
    termforge::CandidateTerminal terminal(2, 16, 4);
    terminal.feed("AB");
    termforge::TextDocument initial(terminal.screen);
    termforge::TextSelection selection;
    require(selection.select_line(initial, 0), "bounded replay initial selection failed");
    terminal.resize(2, 16);
    terminal.feed("\x1b[?25l");
    termforge::TextDocument pending(terminal.screen);
    require(selection.copy(pending, 1024).status == termforge::SelectionCopyStatus::ok,
            "bounded replay did not retain a consistent pending snapshot");
    terminal.feed(std::string(8193, 'x'));
    termforge::TextDocument released(terminal.screen);
    require(selection.copy(released, 1024).status == termforge::SelectionCopyStatus::invalidated,
            "unterminated replay exceeded its bound without releasing identity");

    termforge::CandidateTerminal ending(2, 16, 4);
    ending.feed("AB");
    termforge::TextDocument before_finish(ending.screen);
    termforge::TextSelection finish_selection;
    require(finish_selection.select_line(before_finish, 0), "finish replay initial selection failed");
    ending.resize(2, 16);
    ending.feed("\x1b[?25l");
    ending.feed("C");
    ending.finish();
    termforge::TextDocument after_finish(ending.screen);
    require(text(ending.screen.lines[0]) == "ABC" &&
            finish_selection.copy(after_finish, 1024).status == termforge::SelectionCopyStatus::invalidated,
            "finish did not release the actual state after an incomplete replay");
}

void history_clear_does_not_resurrect() {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    require(terminal.screen.history.size() == 2, "clear-history fixture is missing retained rows");
    terminal.resize(2, 9);
    terminal.feed("\x1b[?25l\x1b[?25h");
    require(terminal.screen.history.size() == 2, "completed resize did not establish retained history");
    terminal.feed("\x1b[3J");
    require(terminal.screen.history.empty(), "explicit history clear resurrected retained rows");
    terminal.feed("E\r\nF");
    termforge::TextDocument document(terminal.screen);
    for (size_t index = 0; index < document.size(); ++index) {
        const auto& value = document.line(index).projection.text;
        require(value != "A" && value != "B", "new output resurrected explicitly cleared history");
    }
}

void conpty_resize_replay(const std::filesystem::path& path) {
    replay_case(path, false);
    replay_case(path, true);
    incomplete_replay_is_bounded();
    history_clear_does_not_resurrect();
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
    const auto alpha = terminal.screen.history[0].id, gamma = terminal.screen.lines[1].id;
    terminal.resize(2, 16);
    const auto widened = search.match(document, 0);
    require(terminal.screen.history[0].id == alpha && terminal.screen.lines[1].id == gamma &&
            widened && widened->range.begin.line_id == gamma &&
            selection.copy(document, 1024).text == "alpha",
            "width-only candidate resize invalidated unchanged hard-row text or identity");

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
    require(text(erase.screen.lines[0]).empty() && text(erase.screen.lines[1]) == " wo",
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

int main(int argc, char** argv) {
    try {
        fragmentation();
        alternate_and_replies();
        per_buffer_margins();
        margin_resize_and_reset();
        empty_payload_events();
        identity_versions_and_eviction();
        conpty_resize_replay(argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::path(__FILE__).parent_path() / "fixtures/conpty-resize-replay.bin");
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
