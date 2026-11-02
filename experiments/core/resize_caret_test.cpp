// Literal WindTerm 2.7.0 controls for combining input after DSR, CUP and resize.
// Public recordings: research/ui/results/2026-11-02-unicode-caret-reference/
// dsr-only-q.json: eb2ec7b8f84ec5a78f3597ef8b2f86947f0a430c9e9b4849680e97d6f8d9544b
// cup-position-q.json: 24a5bad1a5455bb4b95bb9ef20b05b81c79f4237d1310e7fdcb93dd6da98972a
// resize-mark-q.json: 1e8e152e659c93f77e96e8b611e5fc9df43d8947912ac804f1f239eb7535f6f2
// Only complete source UTF-8, DSR and protocol grid are oracles. Original API
// positions/rectangles do not establish C++ Cell columns or mark attachment.
// Terminal empty-string capacity is excluded; spaces and interior rows are exact.
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

static std::vector<std::string> body(std::string_view suffix, bool extra_empty_row = false) {
    std::vector<std::string> result = {
        "", "0123456789", "ANCHOR", "", "", "", "", std::string(60, ' ') + std::string(suffix)
    };
    if (extra_empty_row) result.push_back("");
    return result;
}

struct Case {
    const char* name;
    std::vector<Stage> stages;
};

static std::vector<Case> original_cases() {
    // All cases start with these exact 60 bytes. SHA256:
    // c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49.
    const std::string_view initial = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        "\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;61He\xcc\x81" "Z";
    // Fixed UTF-8 suffixes are transcribed independently from the public bodies.
    // In the last two cases U+0302 precedes e/Q in the text. This byte order is
    // compatible with several storage models and does not prescribe one.
    return {
        {"dsr-only-q", {
            {"initial", initial, 91, "\x1b[8;63R", body("e\xcc\x81" "Z")},
            {"dsr-only", "", 91, "\x1b[8;63R", body("e\xcc\x81" "Z")},
            {"append-mark", "\xcc\x82", 91, "\x1b[8;63R", body("e\xcc\x81" "Z\xcc\x82")},
            {"next-Q", "Q", 91, "\x1b[8;64R", body("e\xcc\x81" "Z\xcc\x82" "Q")}
        }},
        {"cup-position-q", {
            {"initial", initial, 91, "\x1b[8;63R", body("e\xcc\x81" "Z")},
            {"explicit-CUP", "\x1b[8;61H", 91, "\x1b[8;61R", body("e\xcc\x81" "Z")},
            {"append-mark", "\xcc\x82", 91, "\x1b[8;61R", body("\xcc\x82" "e\xcc\x81" "Z")},
            {"next-Q", "Q", 91, "\x1b[8;62R", body("\xcc\x82" "QZ")}
        }},
        {"resize-mark-q", {
            {"initial", initial, 91, "\x1b[8;63R", body("e\xcc\x81" "Z")},
            {"shrunk-unmodified", "", 61, "\x1b[8;61R", body("e\xcc\x81" "Z", true)},
            {"append-mark", "\xcc\x82", 61, "\x1b[8;61R", body("\xcc\x82" "e\xcc\x81")},
            {"next-Q", "Q", 61, "\x1b[8;61R", body("\xcc\x82" "Q")}
        }}
    };
}

static void replay(Checks& checks, const Case& sample, ParseMode mode, bool bytewise) {
    Parser parser(20, 91, 0, mode);
    const auto prefix = std::string(sample.name) + "/" +
        (mode == ParseMode::strict ? "strict/" : "recovery/") + (bytewise ? "bytewise/" : "whole/");
    for (size_t phase = 0; phase < sample.stages.size(); ++phase) {
        const auto& stage = sample.stages[phase];
        checks.context = prefix + stage.label;
        const auto previous = checks.failures;
        try {
            if (phase && stage.columns != sample.stages[phase - 1].columns)
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
    for (const auto& sample : original_cases())
        for (const auto mode : {ParseMode::strict, ParseMode::recovery})
            for (const bool bytewise : {false, true}) replay(checks, sample, mode, bytewise);
    std::cout << "combining caret controls: " << checks.passed << '/' << checks.phases
              << " phases, " << checks.failures << " failed assertions of " << checks.assertions
              << " (three strict original records, 12 stages x four feeds; complete UTF-8 body, DSR and grid; "
                 "no API-offset, rectangle or attachment-policy oracle)\n";
    return checks.failures ? 1 : 0;
}
