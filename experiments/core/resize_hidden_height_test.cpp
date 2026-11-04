// Retained-frame engineering contract: a hidden primary frame keeps its
// retained source cells when only the height changes. The original
// hidden-primary height recording is preserved but its strict audit failed, so
// these assertions are an implementation invariant under the confirmed
// retained-body direction (ADR-0010), not an original-behavior oracle. A width
// change while the frame is hidden still fits it to the protocol footprint,
// matching the separately recorded hidden-width policy.
#include "terminal.hpp"
#include "history_view.hpp"
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
    size_t assertions = 0, failures = 0;

    void expect(bool condition, const std::string& message) {
        ++assertions;
        if (condition) return;
        ++failures;
        std::cerr << context << ": " << message << '\n';
    }
};

static void feed(Parser& parser, std::string_view bytes) { parser.feed(bytes); }

static void omit_empty_tail(std::vector<std::string>& lines) {
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

static std::vector<std::string> source_lines(const TextDocument& document) {
    if (document.size() > 200) throw std::runtime_error("source exceeds the bounded fixture");
    std::vector<std::string> result;
    std::string logical;
    for (size_t rank = 0; rank < document.size(); ++rank) {
        const auto line = document.line(rank);
        logical += line.projection.text;
        if (!line.joins_next) {
            result.push_back(std::move(logical));
            logical.clear();
        }
    }
    if (!logical.empty()) result.push_back(std::move(logical));
    omit_empty_tail(result);
    return result;
}

static std::string line_text(const Line& line) {
    std::string text;
    for (const auto& cell : line) text += cell.text;
    return text;
}

// The fixed 75 B original source from the combined width/height sample. Its
// document keeps the retained `uvw` tail at zero-based row 11, columns 89..91.
static constexpr std::string_view source = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
    "\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B\x1b[12;90Huvw\x1b[19;1HLOWEND";

static std::vector<std::string> expected_body() {
    std::vector<std::string> result = {
        "", "HTOP", "", "", "", "HARD_A", "HARD_B", "", "", "", "",
        std::string(89, ' ') + "uvw", "", "", "", "", "", "LOWEND", ""
    };
    omit_empty_tail(result);
    return result;
}

static std::vector<std::string> body_of(const Screen& screen) {
    TextDocument document(screen);
    return source_lines(document);
}

// The hidden frame is inspected through an owner copy so the active frame and
// the original screen guards are not switched for the assertion itself.
static Screen hidden_frame(const Screen& screen) {
    Screen copy(screen);
    if (screen.active_buffer() != ScreenBuffer::primary) copy.activate_buffer(ScreenBuffer::primary);
    return copy;
}

// Narrow first while the frame is active so its rows are retained wider than
// the protocol width, then hide it and change only the height.
static void hidden_retains_tail_across_height_only() {
    Checks checks{"hidden/height-only"};
    Parser parser(20, 91, 1000);
    feed(parser, source);
    parser.screen.resize_columns_preserving(61);
    // The 92-character logical row is held as a 91-cell physical row plus one
    // soft continuation cell; both keep their source width after narrowing.
    checks.expect(parser.screen.visible_lines()[11].size() == 91,
                  "active width retention did not keep the source tail");
    parser.screen.activate_buffer(ScreenBuffer::alternate);
    parser.screen.resize_preserving(14, 61);
    parser.screen.resize_preserving(20, 61);
    const auto hidden = hidden_frame(parser.screen);
    bool tail = false;
    for (const auto& line : hidden.visible_lines())
        if (line.size() == 91 && line_text(line).find("uv") != std::string::npos) tail = true;
    checks.expect(tail, "hidden live row lost its retained tail across height-only changes");
    parser.screen.activate_buffer(ScreenBuffer::primary);
    checks.expect(body_of(parser.screen) == expected_body(),
                  "returning to the primary frame lost retained source text");
    if (checks.failures) throw std::runtime_error("hidden/height-only failures");
}

// Shrink enough while hidden that the retained tail rows move into primary
// history; a later height restore must not rewrite the appended history rows.
static void hidden_retains_tail_in_history() {
    Checks checks{"hidden/history"};
    Parser parser(20, 91, 1000);
    feed(parser, source);
    parser.screen.resize_columns_preserving(61);
    parser.screen.move(19, 0);
    parser.screen.activate_buffer(ScreenBuffer::alternate);
    parser.screen.resize_preserving(5, 61);
    const auto hidden = hidden_frame(parser.screen);
    checks.expect(hidden.history_size() >= 14,
                  "hidden height shrink did not move the prefix into history");
    bool retained = false;
    bool continuation = false;
    for (const auto& line : hidden.history_lines()) {
        const auto text = line_text(line);
        if (line.size() == 91 && text.find("uv") != std::string::npos) retained = true;
        // The one-cell soft continuation is padded to the active width before
        // the frame is hidden; its leading written cell still identifies it.
        if (!text.empty() && text[0] == 'w' && text.find_first_not_of(' ') == 0) continuation = true;
    }
    checks.expect(retained && continuation,
                  "hidden height shrink cropped the appended history rows");
    parser.screen.resize_preserving(20, 61);
    parser.screen.activate_buffer(ScreenBuffer::primary);
    checks.expect(body_of(parser.screen) == expected_body(),
                  "returning after hidden shrink/restore lost retained source text");
    if (checks.failures) throw std::runtime_error("hidden/history failures");
}

// A width change while hidden still follows the separately recorded hidden
// width policy: live rows fit the protocol footprint. This is deliberately not
// applied to a height-only change above.
static void hidden_width_change_still_fits_footprint() {
    Checks checks{"hidden/mixed-width-change"};
    Parser parser(20, 91, 1000);
    feed(parser, source);
    parser.screen.resize_columns_preserving(61);
    parser.screen.activate_buffer(ScreenBuffer::alternate);
    parser.screen.resize_preserving(14, 40);
    parser.screen.activate_buffer(ScreenBuffer::primary);
    checks.expect(parser.screen.visible_lines()[6].size() == 40,
                  "hidden width change did not fit the row to the new footprint");
    checks.expect(body_of(parser.screen)[11].find("uvw") == std::string::npos,
                  "hidden width policy no longer crops the retained tail");
    if (checks.failures) throw std::runtime_error("hidden/mixed-width-change failures");
}

int main() {
    try {
        hidden_retains_tail_across_height_only();
        hidden_retains_tail_in_history();
        hidden_width_change_still_fits_footprint();
        std::cout << "hidden retained-frame height checks passed; "
                     "engineering invariant, no original/Qt/performance acceptance\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
