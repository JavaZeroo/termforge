// Isolated row-order fixtures/cost probe, with no production parser or UI entry.
#include "document.hpp"
#include "terminal.hpp"
#include "text_selection.hpp"
#include "text_search.hpp"
#include "history_view.hpp"
#include "generated/selection.hpp"
#include "generated/search.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <string_view>

using namespace termforge;
namespace order = termforge::line_order;

static void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
static void fill(Screen& screen, int row, std::string_view value) {
    screen.move(row, 0);
    for (const unsigned char byte : value) screen.put(byte, std::string(1, static_cast<char>(byte)));
}
static void nonsorted_fixture(Screen& screen) {
    // Seven real Screen-allocated identities; remove 2 and move 7 before 6.
    // Direct storage mutation is isolated here, not a new production mutator.
    fill(screen, 5, "needle"); fill(screen, 6, "needle");
    screen.lines.erase(screen.lines.begin() + 1);
    std::swap(screen.lines[4], screen.lines[5]);
    screen.move(0, 0);
}

static int existing_red() {
    Screen screen(7, 8, 2);
    nonsorted_fixture(screen);
    TextDocument doc(screen);
    int failures = 0;
    const auto verify = [&](bool condition, const char* label) {
        std::cout << label << ':' << (condition ? "pass" : "FAIL") << '\n';
        if (!condition) ++failures;
    };
    verify(doc.index_of(7) == std::optional<size_t>(4), "existing TextDocument locates ID7 at4");
    verify(doc.index_of(6) == std::optional<size_t>(5), "existing TextDocument locates ID6 at5");
    verify(!doc.index_of(2), "existing TextDocument rejects deleted ID2");
    TextSearch search;
    const auto generation = search.start(doc, "needle");
    for (int step = 0; step < 1000 && search.status() == SearchStatus::running; ++step)
        search.step(doc, generation, 64);
    const auto anchor = doc.position(4, 0);
    const auto next = search.navigate_no_wrap(doc, SearchDirection::next, anchor);
    verify(next && next->match.range.begin.line_id == 6,
           "existing TextSearch navigates from displayed ID7 to ID6");
    std::cout << "existing_caller_failures=" << failures << '\n';
    return failures ? 1 : 0;
}

template<class Adapter> static void correctness(const char* name) {
    Screen screen(7, 8, 2);
    nonsorted_fixture(screen);
    const auto retained6 = screen.lines[5], retained7 = screen.lines[4];
    order::Source source(screen); source.order_revision = 1;
    Adapter adapter(source);
    order::Document<Adapter> doc(source, adapter);
    check(doc.index_of(7) == std::optional<size_t>(4) && doc.index_of(6) == std::optional<size_t>(5) &&
          !doc.index_of(2), "literal nonmonotonic identity positions differ");
    const auto saved7 = *doc.position(4, 0), saved6 = *doc.position(5, 0);
    check(doc.compare(saved7, saved6) == std::optional<int>(-1), "display order followed numeric identity");
    R164Selection<Adapter> selection;
    check(selection.set(doc, {saved7, *doc.position(5, 6)}) &&
          selection.copy(doc, 100).text == "needle\r\nneedle", "real selection-copy mechanism lost displayed rows");
    R164Search<Adapter> search;
    const auto generation = search.start(doc, "needle");
    for (int step = 0; step < 1000 && search.status() == SearchStatus::running; ++step)
        search.step(doc, generation, 64);
    check(search.status() == SearchStatus::complete && search.matches().size() == 2,
          "experimental fresh-snapshot search did not complete with two literal matches");
    const auto next = search.navigate_no_wrap(doc, SearchDirection::next, saved7);
    check(next && next->match.range.begin.line_id == 6, "fresh search navigation did not follow document order");
    check(adapter.locate(7) == std::optional<size_t>(4), "surviving viewport-anchor identity was lost");
    // Change structure again: IDs/versions survive, a stale cached rank must not.
    std::swap(screen.lines[4], screen.lines[5]); ++source.order_revision;
    const auto stale_step = search.step(doc, generation, 64);
    check(stale_step.status == SearchStatus::invalidated && !stale_step.accepted && !search.match(doc, 0),
          "snapshot-only search returned a stale result after a structure revision");
    check(doc.index_of(7) == std::optional<size_t>(5) && doc.valid(saved7) &&
          screen.lines[4].id == retained6.id && screen.lines[4].version == retained6.version &&
          screen.lines[5].id == retained7.id && screen.lines[5].version == retained7.version,
          "row movement replaced stable identities/versions or kept stale table positions");
    check(selection.copy(doc, 100).status == SelectionCopyStatus::invalidated,
          "selection stitched a structurally changed multi-row range");
    screen.lines.erase(screen.lines.begin() + 5); ++source.order_revision;
    check(!doc.index_of(7) && !doc.valid(saved7) && !adapter.locate(2), "deleted identity revived");
    screen.activate_buffer(ScreenBuffer::alternate);
    check(!doc.index_of(6) && !doc.valid(saved6), "old frame stamp crossed into alternate storage");
    source.epoch = screen.buffer_epoch(); ++source.order_revision;
    check(!doc.index_of(6), "old primary identity leaked through a refreshed alternate cache");

    Parser wrapped(3, 4, 2); wrapped.feed("ABCDxy");
    order::Source wrap_source(wrapped.screen); Adapter wrap_adapter(wrap_source);
    order::Document<Adapter> wrap_doc(wrap_source, wrap_adapter);
    check(wrap_doc.meta(0).joins_next, "real soft-wrap link setup differs");
    std::swap(wrapped.screen.lines[1], wrapped.screen.lines[2]); ++wrap_source.order_revision;
    check(!wrap_doc.meta(0).joins_next, "new adjacency invented a soft continuation");

    // Ordered append/evict still uses actual existing Screen history policy.
    Parser ordered(2, 8, 2); ordered.feed("A\r\nB\r\nC");
    order::Source ordered_source(ordered.screen); ordered_source.sorted_proven = true;
    Adapter ordered_adapter(ordered_source);
    const auto removed = ordered.screen.history.front().id;
    const auto alive = ordered.screen.lines[0].id;
    HistoryViewport viewport;
    viewport.scroll_to(ordered.screen, 0);
    ordered.feed("\r\nD\r\nE"); ++ordered_source.order_revision;
    check(!ordered_adapter.locate(removed) && ordered_adapter.locate(alive) == std::optional<size_t>(0),
          "ordered append/eviction lost a retained identity or revived an evicted one");
    check(ordered_adapter.counts.rebuilds == 0, "sorted proof unnecessarily built an unordered table");
    check(viewport.reconcile(ordered.screen).anchor == alive,
          "existing sorted viewport eviction policy changed in the actual caller");
    std::cout << name << ": correctness checks passed\n";
}

using Clock = std::chrono::steady_clock;
static long long nanos(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
template<class Adapter> static void cost(size_t history, const std::string& workflow, const char* adapter_name) {
    check(history == 100 || history == 1000 || history == 10000, "unplanned history size");
    check(workflow == "append" || workflow == "reorder" || workflow == "evict", "unplanned workflow");
    Screen screen(6, 8, history);
    screen.move(5, 0);
    for (size_t row = 0; row < history; ++row) screen.newline();
    order::Source source(screen); source.sorted_proven = workflow == "append";
    if (workflow == "evict") std::swap(screen.lines[3], screen.lines[4]);
    Adapter adapter(source);
    uint64_t checksum = 0;
    // Exactly 1 warmup + 3 measured samples; 32 mutations and 16 lookups/round.
    // No full-history Search runs: their unsorted-scan cost needs a separate bound.
    for (int sample = -1; sample < 3; ++sample) for (int round = 0; round < 32; ++round) {
        auto started = Clock::now();
        if (workflow == "reorder")
            std::rotate(screen.lines.begin() + 1, screen.lines.begin() + 2, screen.lines.end() - 1);
        else { screen.move(screen.height() - 1, 0); screen.newline(); }
        ++source.order_revision;
        const auto mutation_ns = nanos(started);
        const auto n = screen.retained_size(), h = screen.history.size();
        const std::vector<size_t> ranks{0, n/4, n/2, 3*n/4, n-1, h, h+1, h+2, h+3, h+4, h+5, 0, n/2, n-1};
        std::vector<uint64_t> ids;
        for (const auto rank : ranks) ids.push_back(screen.retained_line(rank).id);
        ids.push_back(0); ids.push_back((std::numeric_limits<uint64_t>::max)());
        started = Clock::now();
        const auto first = adapter.locate(ids[0]);
        const auto first_ns = nanos(started);
        check(first == std::optional<size_t>(ranks[0]), "cost first-lookup oracle failed");
        started = Clock::now();
        std::array<std::optional<size_t>, 16> results;
        for (size_t query = 1; query < ids.size(); ++query) results[query] = adapter.locate(ids[query]);
        const auto rest_ns = nanos(started);
        for (size_t query = 1; query < ids.size(); ++query) {
            if (query < ranks.size()) check(results[query] == std::optional<size_t>(ranks[query]), "cost position oracle failed");
            else check(!results[query], "cost missing-ID oracle failed");
            checksum += results[query] ? *results[query] + 1 : 0;
        }
        std::cout << "{\"adapter\":\"" << adapter_name << "\",\"history\":" << history
                  << ",\"workflow\":\"" << workflow << "\",\"sample\":" << sample << ",\"round\":" << round
                  << ",\"mutation_ns\":" << mutation_ns << ",\"first_lookup_ns\":" << first_ns
                  << ",\"remaining_15_lookups_ns\":" << rest_ns << ",\"lookups_total\":" << adapter.counts.lookups
                  << ",\"probes_total\":" << adapter.counts.probes << ",\"rebuilds_total\":" << adapter.counts.rebuilds
                  << ",\"table_payload_bytes\":" << adapter.counts.table_payload_bytes
                  << ",\"rss_bytes\":null,\"static_searches\":0,\"checksum\":" << checksum << "}\n";
    }
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--existing-red") return existing_red();
        if (argc == 2 && std::string_view(argv[1]) == "--correctness") {
            correctness<order::ScanAdapter>("scan"); correctness<order::LazyTableAdapter>("lazy_table"); return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "--cost") {
            const auto* permit = std::getenv("TERMFORGE_R164_COST_AUTHORIZED");
            check(permit && std::string_view(permit) == argv[5], "cost requires the reviewed plan runner permit");
            const size_t history = std::stoull(argv[3]);
            const std::string workflow(argv[4]);
            if (std::string_view(argv[2]) == "scan") cost<order::ScanAdapter>(history, workflow, "scan");
            else if (std::string_view(argv[2]) == "lazy_table") cost<order::LazyTableAdapter>(history, workflow, "lazy_table");
            else throw std::runtime_error("unplanned adapter");
            return 0;
        }
        throw std::runtime_error("usage: --existing-red | --correctness | --cost adapter history workflow permit");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 2;
    }
}
