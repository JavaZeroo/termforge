// Fixed #156 mechanism diagnosis through the real public core/selection APIs.
#include "terminal.hpp"
#include "text_document.hpp"
#include "text_selection.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace termforge;
constexpr std::string_view protocol = "termforge-resize-repaint-witness-v1";
constexpr std::string_view transport_sha256 = "c1d7cb18d5da399a838c997e0abe9375fd4db037e9f8f609ac02e1a58878cb84";
constexpr size_t copy_limit = 8192;
constexpr std::array<size_t, 6> boundaries{0, 210, 420, 630, 861, 1071};
struct Case { const char* name; size_t fourth_prefix; bool predicts_bad_state; };
constexpr std::array<Case, 4> cases{{{"A", 231, false}, {"B", 204, true},
                                   {"C", 225, false}, {"D", 219, true}}};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint64_t fnv(std::string_view value) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
void quoted(std::ostream& out, std::string_view value) {
    Screen::quoted(out, std::string(value));
}
// Same original fixture generator as text_client.cpp at the failed source.
std::string rows(unsigned first, unsigned count, bool matches) {
    std::ostringstream out;
    for (unsigned row = first; row < first + count; ++row) {
        out << (matches ? "TF_FIND_" : "TF_EVICT_") << std::setw(3) << std::setfill('0') << row;
        if (matches) out << " needle \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81";
        out << "\r\n";
    }
    return out.str();
}
std::string selected_text() { return rows(0, 80, true) + "TF_FIND_READY 080\r\n"; }
std::string redraw(bool expanded) {
    std::string result = "\x1b[?25l\x1b[H";
    for (unsigned row = 75; row < 80; ++row) {
        const auto line = rows(row, 1, true);
        result += line.substr(0, line.size() - 2) + "\x1b[K\r\n";
    }
    result += "TF_FIND_READY 080\x1b[K\r\n";
    const int blank_rows = expanded ? 4 : 1;
    for (int index = 0; index < blank_rows; ++index) {
        result += "\x1b[K";
        if (index + 1 < blank_rows) result += "\r\n";
    }
    if (expanded) result += "\x1b[7;1H";
    result += "\x1b[?25h";
    return result;
}
std::string expected_transport() {
    const auto normal = redraw(false), expanded = redraw(true);
    require(normal.size() == 210 && expanded.size() == 231, "constructed redraw size differs");
    return normal + normal + normal + expanded + normal;
}
std::string read_transport(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "transport input cannot be opened");
    std::array<char, 1072> buffer{};
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    require(!stream.bad() && stream.gcount() == 1071, "transport input must contain exactly 1071 bytes");
    std::string result(buffer.data(), static_cast<size_t>(stream.gcount()));
    require(result == expected_transport(), "transport differs from the fixed constructed bytes");
    return result;
}
const char* copy_status(SelectionCopyStatus value) {
    switch (value) {
    case SelectionCopyStatus::ok: return "ok";
    case SelectionCopyStatus::empty: return "empty";
    case SelectionCopyStatus::invalidated: return "invalidated";
    case SelectionCopyStatus::limit_exceeded: return "limit_exceeded";
    }
    throw std::runtime_error("unknown selection copy status");
}
void position(std::ostream& out, TextPosition value) {
    out << "{\"line_id\":" << value.line_id << ",\"version\":" << value.version
        << ",\"column\":" << value.column << ",\"epoch\":" << value.epoch << '}';
}
void range(std::ostream& out, const std::optional<TextRange>& value) {
    if (!value) { out << "null"; return; }
    out << "{\"begin\":"; position(out, value->begin);
    out << ",\"end\":"; position(out, value->end); out << '}';
}
uint64_t span_hash(const ProjectedLine& value) {
    std::ostringstream canonical;
    for (const auto span : value.spans)
        canonical << span.byte_begin << ',' << span.byte_end << ','
                  << span.column_begin << ',' << span.column_end << ';';
    return fnv(canonical.str());
}
// Match ShellWindow::select_all_text's public range construction, including
// its excluded empty endpoint after a hard break. Never inject line identities.
bool select_all(TextSelection& selection, const TextDocument& document) {
    for (size_t end = document.size(); end > 0; --end) {
        const auto row = document.line(end - 1);
        if (row.projection.spans.empty() && row.ending == LineEnding::none) continue;
        auto last = document.position(end - 1, row.projection.spans.empty() ? 0 : row.projection.spans.back().column_end);
        if (row.ending == LineEnding::hard_break && end < document.size()) last = document.position(end, 0);
        const auto first = document.position(0, 0);
        return first && last && selection.set(document, {*first, *last});
    }
    selection.clear(); return false;
}
struct CaseResult { bool bad_state; size_t checkpoints; };
CaseResult run_case(Case item, std::string_view transport) {
    Parser parser(7, 111, 100, ParseMode::recovery);
    TextSelection selection;
    const auto expected = selected_text();
    size_t offset = 0, checkpoint_count = 0;
    std::optional<size_t> first_invalidation;
    std::cout << "{\"schema\":\"termforge-resize-repaint-case-begin-v1\",\"case\":";
    quoted(std::cout, item.name);
    std::cout << ",\"fourth_prefix_bytes\":" << item.fourth_prefix
              << ",\"predicts_bad_state\":" << (item.predicts_bad_state ? "true" : "false") << "}\n";
    const auto checkpoint = [&](const char* operation, const char* label) {
        TextDocument document(parser.screen);
        require(document.size() <= 110, "diagnostic document bound exceeded");
        const auto before = selection.range();
        const bool reconciled = selection.reconcile(document);
        if (before && !reconciled && !first_invalidation) first_invalidation = checkpoint_count;
        const auto copied = selection.copy(document, copy_limit);
        std::ostringstream out;
        out << "{\"schema\":\"termforge-resize-repaint-checkpoint-v1\",\"case\":";
        quoted(out, item.name); out << ",\"sequence\":" << checkpoint_count++ << ",\"operation\":";
        quoted(out, operation); out << ",\"label\":"; quoted(out, label);
        out << ",\"transport_consumed_bytes\":" << offset << ",\"grid_columns\":" << parser.screen.width()
            << ",\"grid_rows\":" << parser.screen.height() << ",\"cursor_row\":" << parser.screen.row
            << ",\"cursor_column\":" << parser.screen.column << ",\"wrap_pending\":" << (parser.screen.wrap_pending ? "true" : "false")
            << ",\"history_rows\":" << parser.screen.history.size() << ",\"document_rows\":" << document.size()
            << ",\"discarded_rows\":" << parser.screen.discarded << ",\"buffer_epoch\":" << document.epoch()
            << ",\"range_before_reconcile\":"; range(out, before);
        out << ",\"reconciled\":" << (reconciled ? "true" : "false") << ",\"range_after_reconcile\":";
        range(out, selection.range()); out << ",\"copy_status\":"; quoted(out, copy_status(copied.status));
        out << ",\"copy_bytes\":" << copied.text.size() << ",\"copy_fnv1a64\":\"" << fnv(copied.text)
            << "\",\"copy_matches_initial_literal\":" << (copied.status == SelectionCopyStatus::ok && copied.text == expected ? "true" : "false")
            << ",\"first_invalidation_sequence\":";
        if (first_invalidation) out << *first_invalidation; else out << "null";
        out << ",\"lines\":[";
        for (size_t index = 0; index < document.size(); ++index) {
            const auto row = document.line(index);
            if (index) out << ',';
            out << "{\"index\":" << index << ",\"id\":" << row.projection.id
                << ",\"version\":" << row.projection.version << ",\"columns\":" << row.columns
                << ",\"ending\":" << static_cast<int>(row.ending) << ",\"joins_next\":" << (row.joins_next ? "true" : "false")
                << ",\"text_bytes\":" << row.projection.text.size() << ",\"text_fnv1a64\":\"" << fnv(row.projection.text)
                << "\",\"span_count\":" << row.projection.spans.size() << ",\"span_fnv1a64\":\"" << span_hash(row.projection) << "\"}";
        }
        out << "]}";
        require(out.str().size() <= 40000, "diagnostic checkpoint byte bound exceeded");
        std::cout << out.str() << '\n' << std::flush;
    };
    parser.feed("\x1b[2J\x1b[H" + expected);
    checkpoint("seed-feed", "original-fixture-body");
    require(expected.size() == 2499 && parser.screen.history.size() == 75 &&
            parser.screen.retained_size() == 82 && parser.screen.row == 6 && parser.screen.column == 0,
            "initial fixture shape differs; stop without altering seed or identities");
    require(select_all(selection, TextDocument(parser.screen)), "initial Select All range was rejected");
    checkpoint("selection", "actual-select-all-policy");
    const auto initial = selection.copy(TextDocument(parser.screen), copy_limit);
    require(initial.status == SelectionCopyStatus::ok && initial.text == expected,
            "initial Select All is not the fixed 2499-byte literal");
    const auto resize = [&](int rows, int columns, const char* label) {
        parser.screen.resize(rows, columns); checkpoint("resize", label);
    };
    const auto feed = [&](size_t end, const char* label) {
        require(end > offset && end <= transport.size(), "invalid fixed feed interval");
        parser.feed(transport.substr(offset, end - offset)); offset = end;
        checkpoint("feed", label);
    };
    constexpr std::array<int, 3> columns{112, 112, 111};
    for (size_t index = 0; index < columns.size(); ++index) {
        resize(7, columns[index], "declared-leading-grid");
        feed(boundaries[index + 1], "complete-leading-redraw");
    }
    resize(10, 149, "zoom75-expanded-grid");
    feed(boundaries[3] + item.fourth_prefix, "fourth-redraw-prefix");
    resize(7, 111, "restore-grid-at-declared-cut");
    if (offset < boundaries[4]) feed(boundaries[4], "fourth-redraw-original-tail");
    feed(boundaries[5], "complete-fifth-redraw");
    const auto final = selection.copy(TextDocument(parser.screen), copy_limit);
    const bool bad = final.status != SelectionCopyStatus::ok || final.text != expected;
    std::cout << "{\"schema\":\"termforge-resize-repaint-case-result-v1\",\"case\":";
    quoted(std::cout, item.name); std::cout << ",\"status\":";
    quoted(std::cout, bad ? "observed_bad_state" : "selection_retained");
    std::cout << ",\"semantic_acceptance\":false,\"ci_cause_established\":false,\"transport_consumed_bytes\":" << offset
              << ",\"checkpoints\":" << checkpoint_count
              << ",\"prediction_matches\":" << (bad == item.predicts_bad_state ? "true" : "false") << "}\n" << std::flush;
    require(checkpoint_count == (item.fourth_prefix == 231 ? 12u : 13u), "fixed checkpoint count differs");
    return {bad, checkpoint_count};
}
} // namespace

int main(int argc, char** argv) {
    size_t completed = 0, bad_states = 0, checkpoints = 0;
    try {
        require(argc == 3 && std::string_view(argv[1]) == "--transport", "usage: resize_repaint_witness --transport fixed-input.bin");
        const auto transport = read_transport(argv[2]);
        std::cout << "{\"schema\":\"termforge-resize-repaint-input-v1\",\"protocol\":";
        quoted(std::cout, protocol); std::cout << ",\"transport_expected_sha256\":";
        quoted(std::cout, transport_sha256);
        std::cout << ",\"transport_bytes\":" << transport.size() << ",\"transport_fnv1a64\":\"" << fnv(transport)
                  << "\",\"byte_equal_to_constructed_fixture\":true,\"semantic_acceptance\":false}\n";
        for (const auto item : cases) {
            const auto result = run_case(item, transport);
            ++completed; bad_states += result.bad_state ? 1u : 0u;
            checkpoints += result.checkpoints;
        }
        std::cout << "{\"schema\":\"termforge-resize-repaint-summary-v1\",\"diagnostic_complete\":true,\"cases_completed\":"
                  << completed << ",\"observed_bad_states\":" << bad_states
                  << ",\"checkpoints\":" << checkpoints
                  << ",\"semantic_acceptance\":false,\"ci_cause_established\":false,\"cli_zero_means\":\"four declared cases recorded\"}\n";
        std::cout.flush();
        require(bool(std::cout), "diagnostic output write failed");
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"schema\":\"termforge-resize-repaint-summary-v1\",\"diagnostic_complete\":false,\"cases_completed\":"
                  << completed << ",\"semantic_acceptance\":false,\"ci_cause_established\":false}\n" << std::flush;
        std::cerr << "resize repaint witness: " << error.what() << '\n';
        return 2;
    }
}
