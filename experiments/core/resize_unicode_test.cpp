// Literal WindTerm 2.7.0 wide-boundary resize observations, independently
// recorded by research/ui/resize_unicode_reference.md. Public recording SHA256:
// 2b8f8b17bfb95a418c29dbad55a12df13f27df1a92b3c3d2fc01c374087e2b0e.
// The four original stages and the additional pagination/mapping engineering
// cases have separate counters. Neither private API offsets nor glyph rectangle
// widths are terminal Cell columns. In particular, Z has no display oracle.
#include "terminal.hpp"
#include "history_view.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

static constexpr std::string_view wide = "\xe7\x95\x8c";

struct Checks {
    std::string context;
    size_t assertions = 0, failures = 0, cases = 0, passed = 0;

    void expect(bool condition, const std::string& message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << ": " << message << '\n';
    }
};

struct CellLocation {
    size_t row, column;
    bool operator==(const CellLocation& other) const {
        return row == other.row && column == other.column;
    }
};

struct VisibleMarker {
    const char* text;
    size_t row, column;
};

struct Stage {
    const char* label;
    std::string_view bytes;
    int columns;
    const char* reply;
    std::vector<std::string> original_body;
    std::vector<VisibleMarker> visible;
};

static void feed(Parser& parser, std::string_view bytes, bool bytewise) {
    if (bytewise) {
        for (const char byte : bytes) parser.feed(std::string_view(&byte, 1));
    } else {
        parser.feed(bytes);
    }
}

static void omit_empty_tail(std::vector<std::string>& lines) {
    // Original Document allocation and Screen viewport capacity differ. Only
    // terminal empty strings are omitted, never spaces or interior empty rows.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

static std::vector<std::string> source_lines(const Screen& screen) {
    TextDocument document(screen);
    if (document.size() > 200) throw std::runtime_error("source observation exceeds 200 fragments");
    std::vector<std::string> lines;
    std::string logical;
    for (size_t row = 0; row < document.size(); ++row) {
        const auto line = document.line(row);
        logical += line.projection.text;
        if (!line.joins_next) {
            lines.push_back(std::move(logical));
            logical.clear();
        }
    }
    if (!logical.empty()) lines.push_back(std::move(logical));
    omit_empty_tail(lines);
    return lines;
}

static void check_body(Checks& checks, const Screen& screen, std::vector<std::string> expected) {
    const auto actual = source_lines(screen);
    omit_empty_tail(expected);
    checks.expect(actual.size() == expected.size(), "logical body row count differs");
    for (size_t row = 0; row < expected.size(); ++row) {
        checks.expect(row < actual.size() && actual[row] == expected[row],
                      "logical UTF-8 body row " + std::to_string(row) + " differs");
    }
}

static std::vector<CellLocation> visible_positions(const ViewSnapshot& page, std::string_view marker) {
    std::vector<CellLocation> result;
    for (size_t row = 0; row < page.lines.size(); ++row) {
        const auto& line = page.lines[row];
        for (size_t column = 0; column < line.size(); ++column) {
            if (line[column].width == 0) continue;
            std::string text;
            for (size_t end = column; end < line.size() && text.size() < marker.size(); ++end) {
                text += line[end].text;
                if (text.size() > marker.size() || marker.substr(0, text.size()) != text) break;
            }
            if (text == marker) result.push_back({row, column});
        }
    }
    return result;
}

static void check_visible(Checks& checks, const ViewSnapshot& page,
                          const std::vector<VisibleMarker>& expected) {
    // ASCII prefixes make these measured start positions unambiguous. Z is
    // deliberately absent: its requested API position was inside UTF-8 for
    // the preceding CJK scalar, so that rectangle cannot locate Z's Cell.
    for (const char* label : {"0123456789", "ANCHOR", "\xe7\x95\x8c", "Q"}) {
        std::vector<CellLocation> wanted;
        for (const auto& marker : expected) {
            if (std::string_view(marker.text) == label) wanted.push_back({marker.row, marker.column});
        }
        checks.expect(visible_positions(page, label) == wanted,
                      std::string("visible Cell start set differs for ") + label);
    }
}

static std::vector<std::string> original_body(bool after_q, bool extra_empty_row) {
    // Before Q, the CJK scalar starts after 60 ASCII spaces. Z's public text
    // offsets are scalar/UTF-16 61 and UTF-8 byte 63; its queried API value 84
    // (line start 23 + 61) lies inside the CJK UTF-8 bytes [83,86). Our complete
    // two-cell representation places Z at Cell 62, which is not a public API
    // column oracle. Preserve Z in the exact body without asserting its rect.
    // Independently transcribed complete public arrays, before normalization.
    // Hashes use the exporter's compact JSON with ASCII escapes:
    // phase 0: 8f166cf4210a90c5732bf6d0025a78a954e1fc1a5881bd268d428049c5f4c00d
    // phase 1: d4c0033f2322c581fff9313e65e57e0b402c182c94c24b31ee71bd8723381926
    // phases 2/3: 530f686c138f746be5743e894498a3ac1fd6e113b4497d73fc4845bd3f23c86e
    std::vector<std::string> result = {
        "", "0123456789", "ANCHOR", "", "", "", "",
        std::string(60, ' ') + (after_q ? "Q" : "\xe7\x95\x8c" "Z")
    };
    if (extra_empty_row) result.push_back("");
    return result;
}

static std::vector<Stage> original_stages() {
    // Exact 60-byte source; SHA256:
    // ef608ca9c74bf7f46eeaffa2b920da7d23a4095498f9a3011034189979c14d30.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;61H\xe7\x95\x8c" "Z";
    return {
        {"wide-initial", source, 91, "\x1b[8;64R", original_body(false, false),
            {{"0123456789", 1, 0}, {"ANCHOR", 2, 0}, {"\xe7\x95\x8c", 7, 60}}},
        {"wide-shrunk-unmodified", "", 61, "\x1b[8;61R", original_body(false, true),
            {{"0123456789", 1, 0}, {"ANCHOR", 2, 0}, {"\xe7\x95\x8c", 8, 0}}},
        {"wide-next-Q", "Q", 61, "\x1b[8;61R", original_body(true, false),
            {{"0123456789", 1, 0}, {"ANCHOR", 2, 0}, {"Q", 7, 60}}},
        {"wide-restored-after-Q", "", 91, "\x1b[8;61R", original_body(true, false),
            {{"0123456789", 1, 0}, {"ANCHOR", 2, 0}, {"Q", 7, 60}}}
    };
}

static std::string feed_name(ParseMode mode, bool bytewise) {
    return std::string(mode == ParseMode::strict ? "strict/" : "recovery/") +
        (bytewise ? "bytewise/" : "whole/");
}

static void replay_original(Checks& checks, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    HistoryViewport viewport;
    const auto stages = original_stages();
    for (size_t phase = 0; phase < stages.size(); ++phase) {
        const auto& stage = stages[phase];
        checks.context = "original/" + feed_name(mode, bytewise) + stage.label;
        const auto previous = checks.failures;
        try {
            if (phase && stage.columns != stages[phase - 1].columns)
                parser.screen.resize_columns_preserving(stage.columns);
            feed(parser, stage.bytes, bytewise);
            checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                          "actual protocol grid differs from the original phase");
            feed(parser, "\x1b[6n", bytewise);
            checks.expect(parser.take_replies() == std::vector<std::string>{stage.reply},
                          "DSR differs from the literal original reply");
            check_body(checks, parser.screen, stage.original_body);
            const auto page = viewport.project(parser.screen, static_cast<size_t>(stage.columns), 20,
                                               {0, 0}, parser.screen.retained_size());
            check_visible(checks, page.snapshot, stage.visible);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("original replay observation failed: ") + error.what());
        }
        ++checks.cases;
        if (checks.failures == previous) ++checks.passed;
    }
    parser.finish();
}

static bool one_span(const std::vector<ViewCellSpan>& spans, size_t first, size_t last) {
    return spans.size() == 1 && spans[0].row == 0 &&
        spans[0].column_begin == first && spans[0].column_end == last;
}

static void engineering_page_boundary(Checks& checks, ParseMode mode, bool bytewise) {
    // Core representation contract, not an original-recording extension:
    // abc [0,3), CJK [3,5), Z [5,6). Byte offsets would instead end at 7.
    Parser parser(1, 8, 0, mode);
    feed(parser, "abc\xe7\x95\x8c" "Z", bytewise);
    const TextDocument document(parser.screen);
    const auto begin = document.position(0, 3), end = document.position(0, 6);
    checks.expect(begin && end, "engineering source Cell boundaries are missing");
    if (!begin || !end) return;
    const auto original = source_lines(parser.screen);
    HistoryViewport viewport;
    const auto first = viewport.project(parser.screen, 4, 1, {0, 0}, 1);
    checks.expect(first.stop == ProjectionStop::page_full && first.source_rows_read == 1 &&
                  first.snapshot.lines.size() == 1 && first.next.bound &&
                  first.next.row == 0 && first.next.column == 3 && first.next.source == *begin,
                  "one-column remainder consumed or split the next wide source atom");
    if (first.snapshot.lines.size() != 1) return;
    const auto& row = first.snapshot.lines[0];
    checks.expect(row.size() == 4 && row[0].text == "a" && row[1].text == "b" && row[2].text == "c" &&
                  row[3].text == " " && !row[3].written && row[3].style.plain() && row.id == 0,
                  "display-only remainder padding changed content or invented a source row");
    const auto revision = viewport.layout_revision();
    checks.expect(first.snapshot.mapping.hit(document, 0, 3, ViewAffinity::leading, revision) == begin &&
                  first.snapshot.mapping.hit(document, 0, 4, ViewAffinity::trailing, revision) == begin,
                  "source-less padding did not clamp to the existing source boundary");
    checks.expect(first.snapshot.mapping.project(document, {*begin, *end}, revision).empty(),
                  "unconsumed wide source range highlighted display-only padding");

    const auto second = viewport.project(parser.screen, 4, 1, first.next, 1);
    checks.expect(second.stop == ProjectionStop::source_end && second.source_rows_read == 1 &&
                  second.snapshot.lines.size() == 1 && second.next.row == 1 && second.next.column == 0,
                  "wide page continuation lost its exact source endpoint");
    if (second.snapshot.lines.size() != 1) return;
    const auto& next = second.snapshot.lines[0];
    checks.expect(next.size() == 4 && next[0].text == wide && next[0].width == 2 &&
                  next[1].text.empty() && next[1].width == 0 && next[2].text == "Z",
                  "continuation did not preserve the complete wide lead/trail and suffix");
    const auto next_revision = viewport.layout_revision();
    const auto trail = second.snapshot.mapping.hit(document, 0, 1, ViewAffinity::leading, next_revision);
    const auto after = second.snapshot.mapping.hit(document, 0, 3, ViewAffinity::trailing, next_revision);
    checks.expect(trail && after, "continued-page selection endpoints are missing");
    if (trail && after) {
        TextSelection selection;
        const bool selected = selection.set(document, {*after, *trail});
        checks.expect(selected, "reverse selection from the wide trail was rejected");
        if (selected) {
            const auto copied = selection.copy(document, 64);
            checks.expect(copied.status == SelectionCopyStatus::ok && copied.text == std::string(wide) + "Z",
                          "wide trail selection copied partial bytes, padding or a duplicate atom");
            checks.expect(one_span(second.snapshot.mapping.project(document, *selection.range(), next_revision), 0, 3),
                          "continued selection highlight differs from the complete Cell footprint");
        }
    }
    TextSearch search;
    const auto generation = search.start(document, std::string(wide) + "Z");
    for (int step = 0; step < 100 && search.status() == SearchStatus::running; ++step)
        search.step(document, generation, 4);
    checks.expect(search.status() == SearchStatus::complete && search.matches().size() == 1,
                  "continued source Find did not complete with one match");
    const auto match = search.match(document, 0);
    checks.expect(match && match->range.begin == *begin && match->range.end == *end,
                  "Find confused UTF-8 byte offsets with source Cell boundaries");
    if (match) {
        checks.expect(one_span(second.snapshot.mapping.project(document, match->range, next_revision), 0, 3),
                      "Find highlight lost the continued wide source range");
    }
    checks.expect(source_lines(parser.screen) == original && document.valid(*begin) && document.valid(*end),
                  "read-only pagination changed source content or invalidated its identities");
    feed(parser, "\x1b[6n", bytewise);
    checks.expect(parser.take_replies() == std::vector<std::string>{"\x1b[1;7R"},
                  "display pagination changed the engineering protocol cursor");
    parser.finish();
}

int main() {
    Checks original, engineering;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery}) {
        for (const bool bytewise : {false, true}) {
            replay_original(original, mode, bytewise);
            engineering.context = "engineering/" + feed_name(mode, bytewise) + "wide-remainder-pagination";
            const auto previous = engineering.failures;
            try {
                engineering_page_boundary(engineering, mode, bytewise);
            } catch (const std::exception& error) {
                engineering.expect(false, std::string("engineering observation failed: ") + error.what());
            }
            ++engineering.cases;
            if (engineering.failures == previous) ++engineering.passed;
        }
    }
    std::cout << "retained-column wide-boundary original checks: " << original.passed << '/' << original.cases
              << " phases, " << original.failures << " failed assertions of " << original.assertions
              << " (one strict original record, four stages x four feeds; UTF-8 body and DSR; "
                 "visible start coordinates only; no Z API-offset or glyph-width oracle)\n";
    std::cout << "wide-remainder pagination engineering checks: " << engineering.passed << '/' << engineering.cases
              << " cases, " << engineering.failures << " failed assertions of " << engineering.assertions
              << " (complete Cell pagination, source-less padding, selection and Find; not original observations)\n";
    return original.failures || engineering.failures ? 1 : 0;
}
