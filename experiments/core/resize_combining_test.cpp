// Literal WindTerm 2.7.0 combining-boundary diagnostic. Independent strict
// public recording: research/ui/results/2026-11-02-resize-unicode-reference/
// combining-boundary.json, SHA256:
// f6ac486eb30ebcc0908d402299728727970df72386f042c8345a0f471e3ab93e.
// Compare only the complete UTF-8 body, DSR and grid. API marker rectangles,
// caret offsets and private attachment state are not Cell-coordinate oracles.
// The literal suffix order does not prescribe an isolated-mark storage policy.
#include "terminal.hpp"
#include "text_document.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace termforge;

struct Checks {
    std::string context;
    size_t assertions = 0, failures = 0, phases = 0, passed = 0;

    void expect(bool condition, const std::string& message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << ": " << message << '\n';
    }
};

struct Stage {
    const char* label;
    std::string_view bytes;
    int columns;
    const char* reply;
    std::vector<std::string> original_body;
};

static void feed(Parser& parser, std::string_view bytes, bool bytewise) {
    if (bytewise) {
        for (const char byte : bytes) parser.feed(std::string_view(&byte, 1));
    } else {
        parser.feed(bytes);
    }
}

static void omit_empty_tail(std::vector<std::string>& lines) {
    // Normalize only terminal empty-string capacity, as in the separate wide
    // case. Never trim spaces, space-only extents or interior empty rows.
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

static std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

static void check_body(Checks& checks, const Screen& screen, std::vector<std::string> expected) {
    const auto actual = source_lines(screen);
    omit_empty_tail(expected);
    checks.expect(actual.size() == expected.size(), "logical body row count expected " +
        std::to_string(expected.size()) + ", observed " + std::to_string(actual.size()));
    for (size_t row = 0; row < expected.size(); ++row) {
        checks.expect(row < actual.size() && actual[row] == expected[row],
            "logical body row " + std::to_string(row) + " expected UTF-8 " + hex(expected[row]) +
            ", observed " + (row < actual.size() ? hex(actual[row]) : "missing"));
    }
}

static std::vector<std::string> original_body(bool after_mark, bool extra_empty_row) {
    // Complete arrays independently transcribed from the strict public body.
    // Compact JSON with the exporter's default ASCII escapes has SHA256:
    // phase 0: 881daac9af8dedd04673a48efbd6ac10450ed08c23e461c1b26666e16fa82040
    // phase 1: aa7ae5dd2da851a8c6e32d3db80ab078942418ae66b0bf480e501bf8c1e821ec
    // phases 2/3: f585595566948a83a6c1b172e9d1c8a0830e6e9cb62ca80dfcbfbb18b17372a6
    // After the mark, the non-ASCII suffix bytes are exactly cc82 65 cc81:
    // U+0302, e, U+0301. Do not normalize or move U+0302 after e or Z.
    std::vector<std::string> result = {
        "", "0123456789", "ANCHOR", "", "", "", "",
        std::string(60, ' ') + (after_mark ? "\xcc\x82" "e\xcc\x81" : "e\xcc\x81" "Z")
    };
    if (extra_empty_row) result.push_back("");
    return result;
}

static std::vector<Stage> original_stages() {
    // Exact 60-byte initial input; SHA256:
    // c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49.
    // The later two-byte U+0302 input is cc82, SHA256:
    // 4c2ca04592f16379e8baeaaec5f381c566ae771bbdfda88ad32880113b5e31a7.
    const std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;61He\xcc\x81" "Z";
    return {
        {"combining-initial", source, 91, "\x1b[8;63R", original_body(false, false)},
        {"combining-shrunk-unmodified", "", 61, "\x1b[8;61R", original_body(false, true)},
        {"combining-next-circumflex", "\xcc\x82", 61, "\x1b[8;61R", original_body(true, false)},
        {"combining-restored-after-mark", "", 91, "\x1b[8;61R", original_body(true, false)}
    };
}

static void replay(Checks& checks, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    const auto stages = original_stages();
    const auto prefix = std::string(mode == ParseMode::strict ? "strict/" : "recovery/") +
        (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < stages.size(); ++phase) {
        const auto& stage = stages[phase];
        checks.context = prefix + stage.label;
        const auto previous = checks.failures;
        try {
            if (phase && stage.columns != stages[phase - 1].columns)
                parser.screen.resize_columns_preserving(stage.columns);
            feed(parser, stage.bytes, bytewise);
            checks.expect(parser.screen.width() == stage.columns && parser.screen.height() == 20,
                          "actual protocol grid differs from the original phase");
            feed(parser, "\x1b[6n", bytewise);
            const auto replies = parser.take_replies();
            std::string observed;
            for (const auto& reply : replies) observed += hex(reply) + " ";
            checks.expect(replies == std::vector<std::string>{stage.reply},
                          "DSR expected " + hex(stage.reply) + ", observed " + observed);
            check_body(checks, parser.screen, stage.original_body);
        } catch (const std::exception& error) {
            checks.expect(false, std::string("diagnostic observation failed: ") + error.what());
        }
        ++checks.phases;
        if (checks.failures == previous) ++checks.passed;
    }
    parser.finish();
}

int main() {
    Checks checks;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool bytewise : {false, true}) replay(checks, mode, bytewise);
    std::cout << "retained-column combining-boundary diagnostic: " << checks.passed << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (one strict original record, four stages x four feeds; complete UTF-8 body, DSR and grid; "
                 "no API-offset, marker-rectangle or attachment-policy oracle)\n";
    return checks.failures ? 1 : 0;
}
