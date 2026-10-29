// Fixed immutable-snapshot Search diagnostic. No production parser/UI entry.
#include "document.hpp"
#if !defined(R164_SELECTION_HEADER) || !defined(R164_SEARCH_HEADER)
#error "Bind the reviewed absolute generated caller headers through CMake"
#endif
#include R164_SELECTION_HEADER
#include R164_SEARCH_HEADER
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace termforge;
namespace order = termforge::line_order;
using Clock = std::chrono::steady_clock;
static constexpr size_t step_budget = 64, hit_count = 16, result_limit = 64;
static void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
static long long elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
static void flush_output() {
    std::cout.flush(); check(static_cast<bool>(std::cout), "static Search evidence output failed");
}
struct Fixture {
    Screen screen;
    std::vector<TextRange> hits;
    TextPosition after_last;
    explicit Fixture(size_t history) : screen(6, 8, history) {
        const auto n = history + 6;
        for (size_t rank = 0; rank < n; ++rank) {
            bool hit = false;
            for (size_t j = 0; j < hit_count; ++j) hit = hit || rank == j * (n - 1) / 15;
            screen.move(screen.row, 0);
            for (const unsigned char byte : std::string_view(hit ? "needle." : "..x...."))
                screen.put(byte, std::string(1, static_cast<char>(byte)));
            if (rank + 1 < n) screen.newline();
        }
        // These two visible rows are non-hits at both fixed history sizes.
        std::swap(screen.lines[2], screen.lines[3]);
        check(screen.history.size() == history && screen.retained_size() == n, "fixture retained shape differs");
        std::vector<uint64_t> ids;
        for (size_t rank = 0; rank < n; ++rank) {
            const auto& row = screen.retained_line(rank);
            const auto projected = project_line(row);
            bool hit = false;
            for (size_t j = 0; j < hit_count; ++j) hit = hit || rank == j * (n - 1) / 15;
            check(projected.text == (hit ? "needle." : "..x....") && projected.spans.size() == 7,
                  "literal fixture text/projection differs");
            check(row.id && row.version && row.ending == (rank + 1 < n ? LineEnding::hard_break : LineEnding::none),
                  "literal fixture identity/hard-boundary differs");
            for (size_t column = 0; column < 7; ++column) {
                const auto& span = projected.spans[column];
                check(span.byte_begin == column && span.byte_end == column + 1 &&
                      span.column_begin == column && span.column_end == column + 1, "literal ASCII cell oracle differs");
            }
            ids.push_back(row.id);
            if (hit) hits.push_back({{row.id, row.version, 0, screen.buffer_epoch()},
                                     {row.id, row.version, 6, screen.buffer_epoch()}});
        }
        check(hits.size() == hit_count && !std::is_sorted(ids.begin(), ids.end()), "fixture is not the fixed unordered snapshot");
        std::sort(ids.begin(), ids.end());
        check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "fixture identities are not unique");
        const auto& last = screen.retained_line(n - 1);
        after_last = {last.id, last.version, 7, screen.buffer_epoch()};
    }
};
static void range_json(const TextRange& range) {
    const auto& a = range.begin; const auto& b = range.end;
    std::cout << '[' << a.line_id << ',' << a.version << ',' << a.column << ',' << a.epoch << ','
              << b.line_id << ',' << b.version << ',' << b.column << ',' << b.epoch << ']';
}
static bool same_ranges(const std::vector<SearchMatch>& actual, const std::vector<TextRange>& expected) {
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (actual[i].range.begin != expected[i].begin || actual[i].range.end != expected[i].end) return false;
    return true;
}
static void fixture_json(const Fixture& fixture, const char* adapter, size_t history) {
    std::cout << "{\"schema\":\"termforge-r164-static-search-fixture-v1\",\"adapter\":\"" << adapter
              << "\",\"history\":" << history << ",\"retained_rows\":" << fixture.screen.retained_size()
              << ",\"epoch\":" << fixture.screen.buffer_epoch() << ",\"order_revision\":1,\"sorted_proven\":false,\"rows\":[";
    for (size_t i = 0; i < fixture.screen.retained_size(); ++i) {
        const auto& row = fixture.screen.retained_line(i);
        if (i) std::cout << ',';
        std::cout << '[' << row.id << ',' << row.version << ']';
    }
    std::cout << "],\"hit_ranks\":[";
    for (size_t j = 0; j < hit_count; ++j) {
        if (j) std::cout << ',';
        std::cout << j * (fixture.screen.retained_size() - 1) / 15;
    }
    std::cout << "],\"oracle_ranges\":[";
    for (size_t j = 0; j < fixture.hits.size(); ++j) { if (j) std::cout << ','; range_json(fixture.hits[j]); }
    std::cout << "],\"rss_bytes\":null}\n"; flush_output();
}
struct Work {
    long long ns = 0, max_step_ns = -1;
    size_t calls = 0, units = 0, bytes = 0, rows = 0;
    bool accepted = true;
};
template<class Adapter> static Work scan(R164Search<Adapter>& search, const order::Document<Adapter>& doc,
                                        uint64_t generation, size_t maximum_calls) {
    Work work; work.max_step_ns = 0;
    while (search.status() == SearchStatus::running && work.calls < maximum_calls) {
        const auto began = Clock::now();
        const auto progress = search.step(doc, generation, step_budget);
        const auto ns = elapsed(began);
        work.ns += ns; work.max_step_ns = (std::max)(work.max_step_ns, ns); ++work.calls;
        work.units += progress.work_used; work.bytes += progress.bytes_scanned; work.rows += progress.rows_loaded;
        work.accepted = work.accepted && progress.accepted && progress.work_used > 0 && progress.work_used <= step_budget;
        if (!work.accepted) break;
    }
    return work;
}
static void stage_json(const char* adapter, size_t history, int sample, const char* stage, const Work& work,
                       const order::Counters& before, const order::Counters& after, SearchStatus status,
                       const std::vector<SearchMatch>& results, bool oracle) {
    std::cout << "{\"schema\":\"termforge-r164-static-search-stage-v1\",\"adapter\":\"" << adapter
              << "\",\"history\":" << history << ",\"sample\":" << sample << ",\"stage\":\"" << stage
              << "\",\"elapsed_ns\":" << work.ns << ",\"max_step_ns\":";
    if (work.max_step_ns < 0) std::cout << "null"; else std::cout << work.max_step_ns;
    std::cout << ",\"step_calls\":" << work.calls << ",\"work_used\":" << work.units << ",\"bytes_scanned\":" << work.bytes
              << ",\"rows_loaded\":" << work.rows << ",\"lookups\":" << after.lookups - before.lookups
              << ",\"probes\":" << after.probes - before.probes << ",\"rebuilds\":" << after.rebuilds - before.rebuilds
              << ",\"table_payload_bytes\":" << after.table_payload_bytes
              << ",\"search_status\":" << static_cast<int>(status) << ",\"result_ranges\":[";
    for (size_t i = 0; i < results.size(); ++i) { if (i) std::cout << ','; range_json(results[i].range); }
    std::cout << "],\"oracle_passed\":" << (oracle ? "true" : "false") << ",\"rss_bytes\":null}\n";
    flush_output(); check(oracle, "static Search stage oracle/bound failed; retain raw and stop");
}
template<class Adapter> static void run(size_t history, const char* name) {
    const Fixture fixture(history);
    const auto n = fixture.screen.retained_size(), units = 10 * n - 1, calls = (units + step_budget - 1) / step_budget;
    const auto lookup_bound = 23 * n + 4 * calls + 152;
    fixture_json(fixture, name, history);
    for (int sample = -1; sample < 3; ++sample) {
        order::Source source(fixture.screen); source.order_revision = 1; source.sorted_proven = false;
        Adapter adapter(source); order::Document<Adapter> doc(source, adapter);
        R164Search<Adapter> absent, needle;
        for (int query = 0; query < 2; ++query) {
            auto& search = query ? needle : absent;
            std::string text = query ? "needle" : "absent!";
            const auto before_start = adapter.counts; auto began = Clock::now();
            const auto generation = search.start(doc, std::move(text), {true, result_limit});
            Work start; start.ns = elapsed(began);
            stage_json(name, history, sample, query ? "warm_needle_start" : "cold_absent_start", start,
                       before_start, adapter.counts, search.status(), search.matches(),
                       search.status() == SearchStatus::running && search.matches().empty());
            const auto before_scan = adapter.counts;
            const auto work = scan(search, doc, generation, calls);
            const bool correct = work.accepted && work.calls == calls && work.units == units && work.bytes == 8 * n - 1 &&
                work.rows == n && search.status() == SearchStatus::complete &&
                same_ranges(search.matches(), query ? fixture.hits : std::vector<TextRange>{});
            stage_json(name, history, sample, query ? "warm_needle_step" : "cold_absent_step", work,
                       before_scan, adapter.counts, search.status(), search.matches(), correct);
        }
        auto before = adapter.counts; auto began = Clock::now();
        const auto last = needle.match(doc, hit_count - 1);
        Work match; match.ns = elapsed(began);
        const std::vector<SearchMatch> returned = last ? std::vector<SearchMatch>{*last} : std::vector<SearchMatch>{};
        stage_json(name, history, sample, "last_match", match, before, adapter.counts, needle.status(), returned,
                   same_ranges(returned, {fixture.hits.back()}) && adapter.counts.lookups - before.lookups == n + 5);
        before = adapter.counts; began = Clock::now();
        const auto navigation = needle.navigate_no_wrap(doc, SearchDirection::next, fixture.after_last);
        Work navigate; navigate.ns = elapsed(began);
        const std::vector<SearchMatch> navigated = navigation ? std::vector<SearchMatch>{navigation->match} : std::vector<SearchMatch>{};
        stage_json(name, history, sample, "navigate_no_wrap", navigate, before, adapter.counts, needle.status(), navigated,
                   !navigation && adapter.counts.lookups - before.lookups == 16 * (n + 9) + 1 && adapter.counts.lookups <= lookup_bound &&
                   adapter.counts.probes <= lookup_bound * n);
    }
    // Object destruction, fixture creation, literal checks and JSON are outside
    // the per-call endpoints; their whole-process cost is not claimed measured.
}
int main(int argc, char** argv) {
    try {
        check(sizeof(size_t) == 8 && argc == 5 && std::string_view(argv[1]) == "--static-search-cost", "expected fixed x64 static Search recipe");
        const char* permit = std::getenv("TERMFORGE_R164_STATIC_SEARCH_COST_AUTHORIZED");
        check(permit && std::string_view(permit).size() == 32 && std::string_view(permit) == argv[4], "independent static Search authorization missing");
        check(std::all_of(permit, permit + 32, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }), "invalid permit");
        const std::string_view size(argv[3]);
        check(size == "100" || size == "1000", "unplanned history size");
        const size_t history = size == "100" ? 100u : 1000u;
        const std::string_view adapter(argv[2]);
        if (adapter == "scan") run<order::ScanAdapter>(history, "scan");
        else if (adapter == "lazy_table") run<order::LazyTableAdapter>(history, "lazy_table");
        else throw std::runtime_error("unplanned adapter");
        flush_output(); return 0;
    } catch (const std::exception& error) {
        std::cerr << "static Search cost: " << error.what() << '\n'; return 1;
    }
}
