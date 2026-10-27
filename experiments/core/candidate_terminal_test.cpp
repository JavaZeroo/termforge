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

void streaming_graphemes_selection_and_history() {
    const std::string zwj = "👩\xe2\x80\x8d💻", keycap = "1\xef\xb8\x8f\xe2\x83\xa3";
    for (const bool fragmented : {false, true}) {
        termforge::CandidateTerminal terminal(3, 16, 8);
        const auto feed = [&](const std::string& input) {
            if (fragmented) for (const char byte : input) terminal.feed(std::string_view(&byte, 1));
            else terminal.feed(input);
        };
        feed("A" + zwj + keycap + "Z\r\n");
        const auto& row = terminal.screen.lines[0];
        require(row[1].text == zwj && row[1].width == 2 && row[2].width == 0 &&
                row[3].text == keycap && row[3].width == 2 && row[4].width == 0 &&
                row[5].text == "Z", "streaming whole-cluster cell layout differs");
        require(termforge::project_line(row).text == "A" + zwj + keycap + "Z",
                "whole-cluster literal projection differs");
        termforge::TextDocument document(terminal.screen);
        termforge::TextSelection selection;
        require(selection.set(document, {*document.position(0, 2), *document.position(0, 4)}),
                "inside-trailer selection failed");
        require(selection.copy(document, 1024).text == zwj + keycap &&
                selection.range()->begin.column == 1 && selection.range()->end.column == 5,
                "selection split a complex grapheme");
        terminal.resize(3, 20);
        require(selection.reconcile(document) && selection.copy(document, 1024).text == zwj + keycap,
                "width-only resize lost whole grapheme selection");
        feed("B\r\nC\r\nD");
        require(terminal.screen.history.size() == 1 &&
                termforge::project_line(terminal.screen.history[0]).text == "A" + zwj + keycap + "Z" &&
                selection.reconcile(document) && selection.copy(document, 1024).text == zwj + keycap,
                "complex grapheme history or retained selection differs");
    }
    termforge::CandidateTerminal clipped(3, 4, 8);
    clipped.feed("\x1b[?7lABC" + zwj);
    require(clipped.screen.lines[0][3].text == zwj && clipped.screen.lines[0][3].width == 1 &&
            clipped.screen.lines[0][3].clipped, "clipped grapheme metadata or scalar retention differs");
    termforge::CandidateTerminal resized_one(3, 4, 8);
    resized_one.feed(zwj);
    resized_one.resize(3, 1);
    require(resized_one.screen.lines[0][0].text == zwj && resized_one.screen.lines[0][0].width == 1 &&
            resized_one.screen.lines[0][0].clipped && resized_one.screen.row == 0 &&
            resized_one.screen.column == 0 && resized_one.screen.wrap_pending,
            "resize to one column lost text, clipping metadata or cursor");
    termforge::CandidateTerminal bounded(3, 16, 8);
    std::string long_marks = "e";
    for (int index = 0; index < 200; ++index) long_marks += "\xcc\x81";
    bounded.feed(long_marks);
    require(termforge::project_line(bounded.screen.lines[0]).text == long_marks &&
            bounded.grapheme_overflows() == 3 && bounded.screen.lines[0][1].grapheme_fragment,
            "bounded grapheme spill discarded text or diagnostics");
    termforge::TextDocument document(bounded.screen);
    termforge::TextSelection selection;
    require(selection.select_line(document, 0) && selection.copy(document, 4096).text == long_marks,
            "bounded grapheme fragments did not copy the original literal text");
}

void streaming_margin_padding_is_not_copied_as_an_inserted_space() {
    const std::string keycap = "1\xef\xb8\x8f\xe2\x83\xa3";
    for (const bool fragmented : {false, true}) {
        for (const std::string prefix : {"ABC", "ABC "}) {
            termforge::CandidateTerminal terminal(3, 4, 8);
            const std::string literal = prefix + keycap;
            if (fragmented) for (const char byte : literal) terminal.feed(std::string_view(&byte, 1));
            else terminal.feed(literal);
            require(terminal.screen.lines[0][3].leading_spacer == (prefix == "ABC"),
                    "actual written space and leading-wide padding were conflated");
            termforge::TextDocument document(terminal.screen);
            termforge::TextSelection selection;
            require(selection.set(document, {*document.position(0, 0), *document.position(1, 2)}) &&
                    selection.copy(document, 1024).text == literal,
                    "margin growth copy inserted padding or removed a literal space");
            terminal.resize(3, 8);
            require(termforge::project_line(terminal.screen.lines[0]).text == literal &&
                    selection.select_line(document, 0) && selection.copy(document, 1024).text == literal,
                    "resize copied padding or lost a true space");
            terminal.feed("\r\nD\r\nE\r\nF");
            require(terminal.screen.history.size() == 1 &&
                    termforge::project_line(terminal.screen.history[0]).text == literal &&
                    selection.select_line(document, 0) && selection.copy(document, 1024).text == literal,
                    "history copied padding or lost original margin text");
        }
    }
}

void prepend_and_inserted_wide_trailer_copy() {
    const std::string prepend = "\xdc\x8f";
    for (const bool fragmented : {false, true}) {
        termforge::CandidateTerminal prefix(3, 8, 8);
        const std::string literal = prepend + "A";
        if (fragmented) for (const char byte : literal) prefix.feed(std::string_view(&byte, 1));
        else prefix.feed(literal);
        termforge::TextDocument prefix_document(prefix.screen);
        termforge::TextSelection prefix_selection;
        require(prefix.screen.lines[0][0].text == literal && prefix_selection.select_line(prefix_document,0) &&
                prefix_selection.copy(prefix_document,1024).text == literal,
                "zero-width Prepend scalar disappeared from terminal or copied text");
        for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
                {"X"," X ABCDE"}, {"1\xef\xb8\x8f\xe2\x83\xa3", " 1\xef\xb8\x8f\xe2\x83\xa3 ABCD"},
                {"⌚\xef\xb8\x8e", " ⌚\xef\xb8\x8e ABCD"}}) {
            termforge::CandidateTerminal terminal(3,8,8);
            terminal.feed("中ABCDEF\x1b[1;2H\x1b[4h");
            const auto check_pairs = [&] {
                const auto& line = terminal.screen.lines[0];
                for (size_t column=0;column<line.size();++column) {
                    require(line[column].width != 2 || (column+1<line.size() && line[column+1].width==0),
                            "inserted prefix left an unpaired wide lead");
                    require(line[column].width != 0 || (column && line[column-1].width==2),
                            "inserted prefix left an unpaired wide trailer");
                }
            };
            if (fragmented) for (const char byte : pair.first) { terminal.feed(std::string_view(&byte,1)); check_pairs(); }
            else { terminal.feed(pair.first); check_pairs(); }
            termforge::TextDocument document(terminal.screen);
            termforge::TextSelection selection;
            require(selection.select_line(document,0) && selection.copy(document,1024).text == pair.second,
                    "inserted wide-trailer copy did not preserve the atomic-clearing policy");
        }
    }
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

void pending_replay_keeps_resized_grid() {
    for (const auto dimensions : {std::pair<int, int>{2, 16}, {2, 4}, {1, 1}, {3, 5}}) {
        termforge::CandidateTerminal terminal(2, 8, 4);
        terminal.feed("ABCDE中");
        terminal.resize(dimensions.first, dimensions.second);
        const auto resized = state(terminal);
        const auto check_grid = [&] {
            const auto& screen = terminal.screen;
            require(screen.height() == dimensions.first && screen.width() == dimensions.second,
                    "pending replay reverted the published resize dimensions");
            require(screen.row >= 0 && screen.row < screen.height() &&
                    screen.column >= 0 && screen.column < screen.width(), "pending replay cursor exceeds its grid");
            for (const auto& line : screen.lines) {
                require(line.size() == static_cast<size_t>(screen.width()), "pending replay row width differs");
                for (size_t column = 0; column < line.size(); ++column) {
                    if (line[column].width == 2)
                        require(column + 1 < line.size() && line[column + 1].width == 0, "pending replay orphan lead");
                    if (line[column].width == 0)
                        require(column && line[column - 1].width == 2, "pending replay orphan trailer");
                }
            }
        };
        check_grid();
        const std::string incomplete = "\x1b[?25lX";
        for (const char byte : incomplete) {
            terminal.feed(std::string_view(&byte, 1));
            check_grid();
            require(state(terminal) == resized, "pending replay changed its last complete resized snapshot");
        }
        terminal.feed("\x1b[?25h");
        check_grid();
        require(state(terminal) != resized, "completed replay failed to publish its actual writes");
    }
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

enum class ReplayClearPhase { before, pending, completed, coalesced };

void history_clear_replay_boundaries(std::string_view clear, ReplayClearPhase phase, bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    termforge::TextDocument before(terminal.screen);
    termforge::TextSelection selection;
    require(selection.select_line(before, 0) && selection.copy(before, 64).text == "A",
            "clear-during-replay selection fixture differs");
    termforge::TextSearch search;
    const auto generation = search.start(before, "A");
    for (int step = 0; step < 100 && search.status() == termforge::SearchStatus::running; ++step)
        search.step(before, generation, 64);
    require(search.match(before, 0).has_value(), "clear-during-replay Find fixture differs");
    terminal.resize(2, 9);
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    const auto discarded = terminal.screen.discarded;
    if (phase == ReplayClearPhase::coalesced) feed("\x1b[?25l" + std::string(clear) + "\x1b[?25h");
    else {
        if (phase != ReplayClearPhase::before) feed("\x1b[?25l");
        if (phase == ReplayClearPhase::completed) feed("\x1b[?25h");
        feed(clear);
    }
    require(terminal.screen.history.empty(), "clear during replay resurrected old history");
    require(terminal.screen.discarded == discarded, "explicit clear was counted as history eviction");
    termforge::TextDocument cleared(terminal.screen);
    require(selection.copy(cleared, 64).status == termforge::SelectionCopyStatus::invalidated &&
            !search.match(cleared, 0), "cleared history kept a stale selection or Find hit");
    if (phase == ReplayClearPhase::pending) feed("\x1b[?25h");
    terminal.feed("E\r\nF");
    for (size_t index = 0; index < terminal.screen.retained_size(); ++index) {
        const auto value = termforge::project_line(terminal.screen.retained_line(index)).text;
        require(value != "A" && value != "B", "later output resurrected history cleared during replay");
    }
}

void clear_cached_history_with_empty_native_grid(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 8);
    // Height growth pulls A/B into the native viewport; repaint replaces them.
    // Completed replay retains A/B in the adapter's history cache.
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B", "empty-native-history cache fixture differs");
    terminal.resize(4, 9);
    terminal.feed("\x1b[?25l");
    const std::string clear = "\x1b[03J";
    if (fragmented) for (const char byte : clear) terminal.feed(std::string_view(&byte, 1));
    else terminal.feed(clear);
    require(terminal.screen.history.empty(), "clear missed adapter history when native history was empty");
    terminal.feed("\x1b[?25h");
    require(terminal.screen.history.empty(), "replay completion resurrected cleared adapter history");
}

void erase_screen_keeps_replay_history(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(2, 9);
    const std::string erase = "\x1b[?25l\x1b[2J\x1b[?25h";
    if (fragmented) for (const char byte : erase) terminal.feed(std::string_view(&byte, 1));
    else terminal.feed(erase);
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B", "ED2 incorrectly cleared replay history");
    require(text(terminal.screen.lines[0]).empty() && text(terminal.screen.lines[1]).empty(),
            "ED2 did not erase the visible screen");
}

void cached_history_survives_noop() {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 4);
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B", "cached history no-op fixture differs");
    termforge::TextDocument before(terminal.screen);
    termforge::TextSelection selection;
    require(selection.select_line(before, 0), "cached history selection fixture failed");
    termforge::TextSearch search;
    const auto generation = search.start(before, "A");
    for (int step = 0; step < 100 && search.status() == termforge::SearchStatus::running; ++step)
        search.step(before, generation, 64);
    require(search.match(before, 0).has_value(), "cached history Find fixture failed");
    const auto expected = state(terminal);
    for (const auto bytes : {std::string_view{}, std::string_view("\0", 1), std::string_view("\x1b[6n")}) {
        terminal.feed(bytes);
        require(state(terminal) == expected, "transparent feed discarded cached history");
    }
    termforge::TextDocument after(terminal.screen);
    require(selection.copy(after, 64).status == termforge::SelectionCopyStatus::ok &&
            selection.copy(after, 64).text == "A", "empty feed invalidated cached history selection");
    require(search.match(after, 0).has_value(), "transparent feed invalidated cached history Find hit");
    terminal.feed("\x1b[3J");
    termforge::TextDocument cleared(terminal.screen);
    require(terminal.screen.history.empty() && !search.match(cleared, 0) &&
            selection.copy(cleared, 64).status == termforge::SelectionCopyStatus::invalidated,
            "clear did not invalidate cached history text actions");
}

void cached_history_resize_replay_preserves_long_text() {
    const std::string expected = "ABCDEFGH\r\nB\r\nC\r\nD";
    for (const bool fragmented : {false, true}) {
        termforge::CandidateTerminal terminal(2, 8, 16);
        const auto feed = [&](std::string_view bytes) {
            if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
            else terminal.feed(bytes);
        };
        feed(expected);
        terminal.resize(4, 4);
        feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");

        termforge::TextDocument document(terminal.screen);
        termforge::TextSearch search;
        const auto generation = search.start(document, "EFGH");
        for (int step = 0; step < 100 && search.status() == termforge::SearchStatus::running; ++step)
            search.step(document, generation, 64);
        const auto match = search.match(document, 0);
        require(search.status() == termforge::SearchStatus::complete && search.matches().size() == 1 && match,
                "resize replay lost or duplicated the long history row's Find result");
        termforge::TextSelection found;
        require(found.set(document, match->range) && found.copy(document, 64).text == "EFGH",
                "resize replay Find cannot select the original text beyond the new width");

        // The repaint writes C/D into the first two visible rows. Include all
        // retained history, but stop at D rather than copying unused screen rows.
        const auto begin = document.position(0, 0);
        const auto end = document.position(terminal.screen.history.size() + 1, 1);
        termforge::TextSelection whole;
        require(begin && end && whole.set(document, {*begin, *end}),
                "resize replay whole-text selection failed");
        const auto copied = whole.copy(document, 64);
        require(copied.status == termforge::SelectionCopyStatus::ok && copied.text == expected,
                fragmented ? "bytewise resize replay duplicated or lost original history text" :
                             "whole-feed resize replay duplicated or lost original history text");
    }
}

void cached_history_stays_with_primary(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 4);
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    require(terminal.screen.history.size() == 2, "primary cached history fixture differs");
    const auto first = terminal.screen.history[0].id, second = terminal.screen.history[1].id;
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    feed("\x1b[?1049h");
    require(terminal.screen.history.empty(), "primary cached history leaked into alternate screen");
    feed("\x1b[HALT\r\nX\r\nY\r\nZ\r\nQ\x1b[3J");
    require(terminal.screen.history.empty(), "alternate output retained primary history");
    feed("\x1b[?1049l");
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B" && terminal.screen.history[0].id == first &&
            terminal.screen.history[1].id == second, "alternate switch lost primary cached history");
    termforge::TextDocument restored(terminal.screen);
    termforge::TextSelection selection;
    require(selection.select_line(restored, 0) && selection.copy(restored, 64).text == "A",
            "restored history cannot be selected");
    termforge::TextSearch search;
    const auto generation = search.start(restored, "A");
    for (int step = 0; step < 100 && search.status() == termforge::SearchStatus::running; ++step)
        search.step(restored, generation, 64);
    const auto match = search.match(restored, 0);
    require(match && match->range.begin.line_id == first && match->range.begin.column == 0,
            "restored history Find coordinates differ");
    feed("\x1b[?1049h\x1b[HALT\x1b[?1049l");
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B", "coalesced buffer switch lost cached history");
}

void alternate_interrupts_pending_history_replay(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 4);
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD");
    feed("\x1b[?1049h\x1b[HALT");
    require(terminal.screen.history.empty(), "interrupted primary history leaked into alternate screen");
    feed("\x1b[?1049l\x1b[?25h");
    require(terminal.screen.history.size() == 2 && text(terminal.screen.history[0]) == "A" &&
            text(terminal.screen.history[1]) == "B", "alternate interrupted replay lost primary history");
}

void cached_history_eviction_count_is_stable(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 4);
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    const auto discarded = terminal.screen.discarded;
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    feed("\x1b[4;1HE\r\nF\r\nG\r\n");
    require(terminal.screen.history.size() == 4 && text(terminal.screen.history[0]) == "B" &&
            text(terminal.screen.history[1]) == "C" && text(terminal.screen.history[2]) == "D" &&
            text(terminal.screen.history[3]).empty(), "cached eviction retained the wrong rows");
    require(terminal.screen.discarded == discarded + 1, "cached eviction count differs");
    terminal.feed("");
    require(terminal.screen.discarded == discarded + 1, "empty feed reset cached eviction count");
    feed("H\r\nI\r\n");
    require(terminal.screen.discarded == discarded + 3, "native and cached eviction counts were not combined");
    require(terminal.screen.history.size() == 4 && text(terminal.screen.history[0]) == "D" &&
            text(terminal.screen.history[1]).empty() && text(terminal.screen.history[2]) == "E" &&
            text(terminal.screen.history[3]) == "F", "native and cached eviction retained the wrong rows");
    feed("\x1b[3J");
    require(terminal.screen.history.empty() && terminal.screen.discarded == discarded + 3,
            "explicit clear changed accumulated eviction count");
}

void cached_history_resize_and_reset_in_alternate(bool reset, bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 4);
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    feed("\x1b[?1049h");
    terminal.resize(3, 6);
    feed("\x1b[?25l\x1b[HALT\x1b[?25h");
    require(terminal.screen.history.empty(), "alternate resize leaked cached primary history");
    feed(reset ? "\x1b" "c" : "\x1b[3J");
    feed("\x1b[?1049l");
    require(!(terminal.mode_bits() & (1 << 12)), "primary screen was not restored");
    if (reset) require(terminal.screen.history.empty(), "RIS resurrected suspended primary history");
    else require(terminal.screen.history.size() >= 2 && text(terminal.screen.history[0]) == "A" &&
                 text(terminal.screen.history[1]) == "B", "alternate ED3 or resize cleared primary history");
}

void cached_history_evicts_native_prefix(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed("A\r\nB\r\nC\r\nD\r\nE\r\nF");
    terminal.resize(3, 8);
    terminal.feed("\x1b[?25l\x1b[2J\x1b[HE\r\nF\x1b[?25h");
    const std::string output = "\x1b[3;1HG\r\n";
    if (fragmented) for (const char byte : output) terminal.feed(std::string_view(&byte, 1));
    else terminal.feed(output);
    require(terminal.screen.history.size() == 4 && text(terminal.screen.history[0]) == "B" &&
            text(terminal.screen.history[1]) == "C" && text(terminal.screen.history[2]) == "D" &&
            text(terminal.screen.history[3]) == "E" && terminal.screen.discarded == 1,
            "interleaved history eviction differs");
    const auto expected = state(terminal);
    for (int repeat = 0; repeat < 3; ++repeat) {
        terminal.feed("");
        require(state(terminal) == expected, "native prefix was reimported and evicted again");
    }
    terminal.resize(7, 8);
    for (size_t index = 0; index < terminal.screen.retained_size(); ++index)
        require(text(terminal.screen.retained_line(index)) != "A", "height growth resurrected an evicted native row");
}

void cached_eviction_invalidates_replay_origin(bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    const auto feed = [&](std::string_view bytes) {
        if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
        else terminal.feed(bytes);
    };
    feed("A\r\nB\r\nC\r\nD");
    terminal.resize(4, 8);
    feed("\x1b[?25l\x1b[2J\x1b[HC\r\nD\x1b[?25h");
    require(terminal.screen.history.size() == 2, "old replay cache fixture differs");
    feed("\x1b[4;1H");
    terminal.resize(1, 8);
    require(terminal.screen.history.size() == 4 && text(terminal.screen.history[0]) == "B" &&
            text(terminal.screen.history[1]) == "C" && text(terminal.screen.history[2]) == "D" &&
            text(terminal.screen.history[3]).empty() && terminal.screen.discarded == 1,
            "resize cache eviction differs");
    const auto expected = state(terminal);
    for (int repeat = 0; repeat < 3; ++repeat) {
        terminal.feed("");
        require(state(terminal) == expected, "old replay restored an evicted cache row");
    }
    feed("\x1b[?25l\x1b[?1049hALT\x1b[?1049l\x1b[?25h");
    require(terminal.screen.discarded == 1, "buffer switch re-evicted an old replay cache row");
    for (const auto& line : terminal.screen.history)
        require(text(line) != "A", "buffer switch restored an evicted old replay row");
}

void replay_text_coordinates(int from, int to, bool during_replay, bool fragmented) {
    termforge::CandidateTerminal terminal(2, 8, 4);
    terminal.feed(from == 0 ? "H\r\nABC\r\nTAIL" : "H\r\n\x1b[3GABC\r\nTAIL");
    termforge::TextDocument initial(terminal.screen);
    termforge::TextSelection selection;
    require(selection.set(initial, {*initial.position(1, from), *initial.position(1, from + 3)}) &&
            selection.copy(initial, 64).text == "ABC", "shift fixture selection differs");
    termforge::TextSearch search;
    const auto generation = search.start(initial, "ABC");
    for (int step = 0; step < 200 && search.status() == termforge::SearchStatus::running; ++step)
        search.step(initial, generation, 64);
    const auto before = search.match(initial, 0);
    require(search.status() == termforge::SearchStatus::complete && search.matches().size() == 1 &&
            before && before->range.begin.column == static_cast<size_t>(from) &&
            before->range.end.column == static_cast<size_t>(from + 3),
            "shift fixture Find coordinates differ");

    terminal.resize(2, 10);
    if (!during_replay) terminal.feed("\x1b[?25l\x1b[?25h");
    // Exercise both resize replay and subsequent output, in both directions.
    // Keeping column 2 unchanged is the control for harmless trailing padding.
    std::string bytes = "\x1b[1;1H\x1b[2K";
    bytes += to == 0 ? "ABC\r\n" : "\x1b[1;3HABC\r\n";
    if (during_replay) bytes = "\x1b[?25l" + bytes + "\x1b[?25h";
    if (fragmented) for (const char byte : bytes) terminal.feed(std::string_view(&byte, 1));
    else terminal.feed(bytes);
    termforge::TextDocument shifted(terminal.screen);
    require(shifted.line(1).projection.text == (to == 0 ? "ABC" : "  ABC"), "shifted live text differs");
    if (from != to) {
        require(selection.copy(shifted, 64).status == termforge::SelectionCopyStatus::invalidated,
                "replay kept a selection at the old columns after moving its text");
        require(!search.match(shifted, 0), "replay kept a Find hit at the old columns after moving its text");
    } else {
        const auto copy = selection.copy(shifted, 64);
        require(copy.status == termforge::SelectionCopyStatus::ok && copy.text == "ABC",
                "unchanged text coordinates invalidated a selection");
    }

    const auto changed = search.notify_change(shifted);
    for (int step = 0; step < 200 && search.status() == termforge::SearchStatus::running; ++step)
        search.step(shifted, changed, 64);
    const auto after = search.match(shifted, 0);
    require(search.status() == termforge::SearchStatus::complete && search.matches().size() == 1 &&
            after && after->range.begin.column == static_cast<size_t>(to) &&
            after->range.end.column == static_cast<size_t>(to + 3),
            "Find did not converge to the moved text's actual columns");
}

void conpty_resize_replay(const std::filesystem::path& path) {
    replay_case(path, false);
    replay_case(path, true);
    incomplete_replay_is_bounded();
    pending_replay_keeps_resized_grid();
    cached_history_survives_noop();
    cached_history_resize_replay_preserves_long_text();
    cached_history_stays_with_primary(false);
    cached_history_stays_with_primary(true);
    for (const bool fragmented : {false, true}) {
        alternate_interrupts_pending_history_replay(fragmented);
        cached_history_eviction_count_is_stable(fragmented);
        cached_history_resize_and_reset_in_alternate(false, fragmented);
        cached_history_resize_and_reset_in_alternate(true, fragmented);
        cached_history_evicts_native_prefix(fragmented);
        cached_eviction_invalidates_replay_origin(fragmented);
    }
    history_clear_does_not_resurrect();
    for (const bool fragmented : {false, true}) {
        for (const auto clear : {"\x1b[3J", "\x1b[03J", "\x1b" "c"})
            for (const auto phase : {ReplayClearPhase::before, ReplayClearPhase::pending,
                                    ReplayClearPhase::completed, ReplayClearPhase::coalesced})
                history_clear_replay_boundaries(clear, phase, fragmented);
        clear_cached_history_with_empty_native_grid(fragmented);
        erase_screen_keeps_replay_history(fragmented);
    }
    for (const bool during_replay : {true, false}) for (const bool fragmented : {false, true}) {
        replay_text_coordinates(0, 2, during_replay, fragmented);
        replay_text_coordinates(2, 0, during_replay, fragmented);
        replay_text_coordinates(2, 2, during_replay, fragmented);
    }
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
        streaming_graphemes_selection_and_history();
        streaming_margin_padding_is_not_copied_as_an_inserted_space();
        prepend_and_inserted_wide_trailer_copy();
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
