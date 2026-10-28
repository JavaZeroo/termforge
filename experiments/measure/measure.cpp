// Original single-thread diagnostic. See README for the v2 continuous interval.
#include "../core/terminal.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#ifndef TERMFORGE_MEASURE_ALLOCATOR_WRAPPER
#define TERMFORGE_MEASURE_ALLOCATOR_WRAPPER 1
#endif
static_assert(TERMFORGE_MEASURE_ALLOCATOR_WRAPPER == 0 || TERMFORGE_MEASURE_ALLOCATOR_WRAPPER == 1,
              "allocator wrapper flag must be 0 or 1");
static constexpr bool allocator_wrapper_compiled = TERMFORGE_MEASURE_ALLOCATOR_WRAPPER != 0;
#ifdef TERMFORGE_RECOVERY_MEASURE
static constexpr auto parser_mode = termforge::ParseMode::recovery;
#else
static constexpr auto parser_mode = termforge::ParseMode::strict;
#endif
static bool count_allocations = false;
static size_t allocations = 0, allocated_bytes = 0;
#if TERMFORGE_MEASURE_ALLOCATOR_WRAPPER
void* operator new(size_t size) {
    if (void* p = std::malloc(size ? size : 1)) {
        if (count_allocations) { ++allocations; allocated_bytes += size; }
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
#endif
using Clock = std::chrono::steady_clock;
static constexpr double cpu_poll_seconds = .25;
static double cpu_seconds() {
#ifdef _WIN32
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) throw std::runtime_error("GetProcessTimes failed");
    auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return double(value(kernel) + value(user)) / 1e7;
#else
    const auto value = std::clock();
    if (value == static_cast<std::clock_t>(-1)) throw std::runtime_error("process CPU clock unavailable");
    return double(value) / CLOCKS_PER_SEC;
#endif
}
static void clock_identity() {
#ifdef _WIN32
    std::cout << "\"cpu_clock\":\"GetProcessTimes kernel plus user\",\"cpu_representation_unit_seconds\":1e-7";
#else
    std::cout << "\"cpu_clock\":\"std::clock process CPU\",\"cpu_representation_unit_seconds\":" << 1.0 / CLOCKS_PER_SEC;
#endif
    std::cout << ",\"wall_clock\":\"std::chrono::steady_clock\",\"wall_clock_is_steady\":" << (Clock::is_steady ? "true" : "false")
        << ",\"wall_representation_unit_seconds\":" << double(Clock::period::num) / Clock::period::den;
}

struct Interval {
    size_t iterations = 0, wall_polls = 0, cpu_polls = 0;
    double wall = 0, cpu = 0;
};
// CPU is ONE end-minus-start value for the entire uninterrupted interval. Gates
// are part of that interval, never subtracted. No per-iteration CPU differences.
template<class Work> static Interval measure(double duration, size_t group, Work work) {
    Interval result;
    const double cpu_start = cpu_seconds();
    const auto started = Clock::now();
    double last_cpu_poll_wall = 0;
    for (;;) {
        for (size_t i = 0; i < group; ++i) { work(); ++result.iterations; }
        result.wall = std::chrono::duration<double>(Clock::now() - started).count();
        ++result.wall_polls;
        if (result.wall >= 60) throw std::runtime_error("continuous interval exceeded 60-second budget");
        if (result.wall - last_cpu_poll_wall >= cpu_poll_seconds) {
            const double current_cpu = cpu_seconds();
            ++result.cpu_polls;
            last_cpu_poll_wall = result.wall;
            if (current_cpu >= cpu_start + duration && result.wall >= duration) break;
        }
    }
    // Include the successful gate as well; the endpoints have one clock-call
    // skew, which clock calibration reports rather than pretending to remove.
    result.cpu = cpu_seconds() - cpu_start;
    result.wall = std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}
static void interval_json(const Interval& value, size_t group) {
    std::cout << "\"iterations\":" << value.iterations << ",\"interval_wall_seconds\":" << value.wall
        << ",\"interval_cpu_seconds\":" << value.cpu << ",\"wall_gate_polls\":" << value.wall_polls
        << ",\"cpu_gate_polls\":" << value.cpu_polls << ",\"wall_gate_every_iterations\":" << group
        << ",\"cpu_gate_min_wall_spacing_seconds\":" << cpu_poll_seconds;
}
static double duration_arg(const char* argument) {
    size_t used = 0;
    const std::string text(argument);
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value) || value < .1 || value > 10) throw std::runtime_error("invalid duration");
    return value;
}
static size_t integer_arg(const char* argument, size_t maximum) {
    const std::string text(argument);
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid integer");
    const auto value = std::stoull(text);
    if (value > maximum) throw std::runtime_error("integer out of range");
    return static_cast<size_t>(value);
}
static uint64_t hash_state(const termforge::Screen& screen) {
    std::ostringstream json; screen.json(json);
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : json.str()) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}

#ifdef TERMFORGE_RECOVERY_MEASURE
static uint64_t hash_payload(std::string_view bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}
// Exercise the selected parser, outside every measured interval. A build that
// merely reports recovery while constructing a strict parser must fail here.
static void check_recovery_mode() {
    termforge::Parser probe(1, 3, 0, parser_mode);
    probe.feed(std::string_view("\xff" "A", 2)); probe.finish();
    if (probe.screen.lines[0][0].text != "\xef\xbf\xbd" || probe.screen.lines[0][1].text != "A" ||
        probe.screen.column != 2 || probe.diagnostics().malformed_utf8 != 1)
        throw std::runtime_error("recovery parser identity probe failed");
}
static void recovery_fields(const char* kind) {
    std::cout << "\"protocol\":\"termforge-recovery-core-measure-v1\",\"kind\":\"" << kind
        << "\",\"parse_mode\":\"recovery\",\"rows\":24,\"columns\":80,\"history_limit\":100"
        << ",\"allocator_wrapper_compiled\":" << (allocator_wrapper_compiled ? "true" : "false")
        << ",\"scope\":\"core replay only\",\"recovery_probe_passed\":true";
}
#else
static void calibrate(double duration, bool on_first) {
    // Repeated clock-read loop: includes two wall reads, CPU read, bookkeeping
    // and periodic gates. Its average is NOT an isolated GetProcessTimes cost.
    size_t positive_steps = 0;
    double min_cpu_step = std::numeric_limits<double>::max(), max_cpu_step = 0;
    double min_wall_step = std::numeric_limits<double>::max(), max_wall_step = 0;
    double previous_cpu = cpu_seconds();
    const auto clock_interval = measure(duration, 1024, [&] {
        const auto before = Clock::now();
        const double current = cpu_seconds();
        const double wall_step = std::chrono::duration<double>(Clock::now() - before).count();
        if (wall_step > 0) min_wall_step = std::min(min_wall_step, wall_step);
        max_wall_step = std::max(max_wall_step, wall_step);
        const double step = current - previous_cpu;
        if (step < 0) throw std::runtime_error("CPU clock went backwards");
        if (step > 0) { ++positive_steps; min_cpu_step = std::min(min_cpu_step, step); max_cpu_step = std::max(max_cpu_step, step); }
        previous_cpu = current;
    });
    struct AllocationSample { bool observed; Interval interval; size_t calls, bytes; } samples[2];
    for (size_t index = 0; index < 2; ++index) {
        const bool observed = index == 0 ? on_first : !on_first;
        allocations = allocated_bytes = 0;
        count_allocations = observed;
        const auto interval = measure(duration, 4096, [] {
            void* memory = ::operator new(64);
            // Volatile write keeps the tiny synthetic allocation observable.
            *static_cast<volatile unsigned char*>(memory) = 42;
            ::operator delete(memory);
        });
        count_allocations = false;
        if (observed && (allocations != interval.iterations || allocated_bytes != 64 * interval.iterations))
            throw std::runtime_error("allocation calibration count mismatch");
        samples[index] = {observed, interval, allocations, allocated_bytes};
    }
    std::cout << "{\"schema_version\":2,\"kind\":\"calibration\",\"valid\":true,\"cpu_floor_seconds\":" << duration << ',';
    clock_identity();
    std::cout << ",\"clock_read_loop\":{"; interval_json(clock_interval, 1024);
    std::cout << ",\"positive_cpu_steps\":" << positive_steps << ",\"min_observed_cpu_step_seconds\":";
    if (positive_steps) std::cout << min_cpu_step; else std::cout << "null";
    std::cout << ",\"max_observed_cpu_step_seconds\":" << max_cpu_step << ",\"min_observed_read_span_seconds\":";
    if (min_wall_step != std::numeric_limits<double>::max()) std::cout << min_wall_step; else std::cout << "null";
    std::cout << ",\"max_observed_read_span_seconds\":" << max_wall_step << "},\"allocation_loop\":[";
    for (size_t index = 0; index < 2; ++index) {
        const auto& sample = samples[index];
        if (index) std::cout << ',';
        std::cout << "{\"observe\":" << sample.observed << ','; interval_json(sample.interval, 4096);
        std::cout << ",\"allocation_request_bytes\":64,\"ordinary_allocations\":";
        if (sample.observed) std::cout << sample.calls; else std::cout << "null";
        std::cout << ",\"ordinary_allocated_bytes\":";
        if (sample.observed) std::cout << sample.bytes; else std::cout << "null";
        std::cout << '}';
    }
    std::cout << "],\"counter_off_is_uninstrumented\":false,\"calibration_is_subtracted\":false}\n";
}
#endif

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
#ifdef TERMFORGE_RECOVERY_MEASURE
        if (argc == 2 && std::string(argv[1]) == "--identity") {
            check_recovery_mode();
            std::cout << '{'; recovery_fields("identity");
            std::cout << ",\"routes\":[\"project\",\"interactive\"],"
                "\"observe_0\":\"feed without per-round projection\","
                "\"observe_1\":\"feed with per-round projection and hash\"}\n";
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "calibrate")
            throw std::runtime_error("legacy allocation calibration is unavailable in the recovery protocol");
        if (argc != 5) throw std::runtime_error("recovery measure: --identity OR project|interactive chunk observe(0|1) min_cpu_seconds");
#else
        if ((argc == 3 || argc == 4) && std::string(argv[1]) == "calibrate") {
            calibrate(duration_arg(argv[2]), argc == 4 && integer_arg(argv[3], 1) != 0);
            return 0;
        }
        if (argc != 5) throw std::runtime_error("core_measure parse|project|interactive|state chunk observe(0|1) min_cpu_seconds OR calibrate min_cpu_seconds [observe_first(0|1)]");
#endif
        const std::string route = argv[1];
        const size_t chunk = integer_arg(argv[2], 1048576);
        const bool observe = integer_arg(argv[3], 1) != 0;
        const double duration = duration_arg(argv[4]);
        if ((route != "parse" && route != "project" && route != "interactive" && route != "state") || chunk == 0)
            throw std::runtime_error("invalid arguments");
#ifdef TERMFORGE_RECOVERY_MEASURE
        if (route != "project" && route != "interactive") throw std::runtime_error("unsupported recovery replay route");
        check_recovery_mode();
#endif
        const std::string line(76, 'x');
        const std::string action = "abcdefgh\x1b[D\x1b[D\x1b[DZ\bQ\r\n";
        std::string payload;
        for (int i = 0; i < 1000; ++i) payload += route == "interactive" ? action : line + "\r\n";
        auto run = [&](termforge::Parser& parser) {
            if (route != "state") {
                for (size_t offset = 0; offset < payload.size(); offset += chunk)
                    parser.feed(std::string_view(payload).substr(offset, chunk));
                parser.finish();
            } else {
                for (int i = 0; i < 1000; ++i) {
                    for (char c : line) parser.screen.put(static_cast<unsigned char>(c), std::string(1, c));
                    parser.screen.move(parser.screen.row, 0); parser.screen.newline();
                }
            }
        };
        auto correct = [&line, &route](const termforge::Screen& s) {
            if (s.row != 23 || s.column != 0 || s.wrap_pending || s.discarded != 877 || s.history.size() != 100) return false;
            if (route == "interactive") {
                const auto matches = [](const termforge::Line& row) {
                    const std::string expected = "abcdeQgh";
                    for (int c = 0; c < 80; ++c) if (row[c].text != (c < 8 ? expected.substr(c, 1) : " ") ||
                        row[c].width != 1 || !row[c].style.plain()) return false;
                    return true;
                };
                for (int r = 0; r < 23; ++r) if (!matches(s.lines[r])) return false;
                for (const auto& row : s.history) if (!matches(row)) return false;
                for (int c = 0; c < 80; ++c) if (s.lines.back()[c].text != " " ||
                    s.lines.back()[c].width != 1 || !s.lines.back()[c].style.plain()) return false;
                return true;
            }
            for (int r = 0; r < 24; ++r) for (int c = 0; c < 80; ++c) {
                const auto& cell = s.lines[r][c];
                if (cell.text != (r < 23 && c < static_cast<int>(line.size()) ? "x" : " ") || cell.width != 1 || !cell.style.plain()) return false;
            }
            for (const auto& row : s.history) for (int c = 0; c < 80; ++c)
                if (row[c].text != (c < 76 ? "x" : " ") || row[c].width != 1 || !row[c].style.plain()) return false;
            return true;
        };
        const auto oracle_start = Clock::now();
        uint64_t expected_hash = 0;
        {
            termforge::Parser warmup(24, 80, 100, parser_mode); run(warmup);
            if (!correct(warmup.screen)) throw std::runtime_error("warmup oracle failed");
            expected_hash = hash_state(warmup.screen);
        }
        const double preparation = std::chrono::duration<double>(Clock::now() - oracle_start).count();
        // Emplace destroys the prior round before constructing the next. The
        // final state stays alive for the oracle OUTSIDE the measured interval.
        std::optional<termforge::Parser> parser;
        allocations = allocated_bytes = 0;
        count_allocations = observe && allocator_wrapper_compiled;
        const auto interval = measure(duration, 8, [&] {
            parser.emplace(24, 80, 100, parser_mode); run(*parser);
            if ((route == "project" || route == "interactive") && observe && hash_state(parser->screen) != expected_hash)
                throw std::runtime_error("measured projection oracle failed");
        });
        count_allocations = false;
        const auto checks = Clock::now();
        if (!correct(parser->screen) || hash_state(parser->screen) != expected_hash) throw std::runtime_error("final state oracle failed");
        const double oracle_seconds = std::chrono::duration<double>(Clock::now() - checks).count();
        size_t peak = 0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) throw std::runtime_error("memory query failed");
        peak = memory.PeakWorkingSetSize;
#endif
#ifdef TERMFORGE_RECOVERY_MEASURE
        std::cout << '{'; recovery_fields("core_cost");
        std::cout << ",\"valid\":true,\"workload_id\":\"" << (route == "project" ? "ascii-project-78000-v1" : "ascii-interactive-22000-v1")
            << "\",\"payload_fnv1a64\":\"" << hash_payload(payload)
            << "\",\"observer_semantics\":\"" << (observe ? "feed with per-round projection and hash" : "feed without per-round projection")
            << "\",\"route\":\"" << route << "\",\"chunk\":" << chunk
#else
        std::cout << "{\"schema_version\":2,\"kind\":\"core_cost\",\"valid\":true,\"route\":\"" << route << "\",\"chunk\":" << chunk
#endif
            << ",\"observe\":" << observe << ",\"rounds\":" << interval.iterations << ",\"bytes_per_round\":" << payload.size()
            << ",\"cpu_floor_seconds\":" << duration << ',';
        interval_json(interval, 8); std::cout << ','; clock_identity();
        std::cout << ",\"warmup_and_oracle_seconds\":" << preparation << ",\"final_oracle_seconds\":" << oracle_seconds
            << ",\"oracle_scope\":\"" << ((route == "project" || route == "interactive") && observe
                ? "warmup and final; every measured projection also hashed" : "warmup and final iteration") << "\""
            << ",\"state_fnv1a64\":\"" << expected_hash << "\",\"ordinary_allocations\":";
        if (observe && allocator_wrapper_compiled) std::cout << allocations; else std::cout << "null";
        std::cout << ",\"ordinary_allocated_bytes\":";
        if (observe && allocator_wrapper_compiled) std::cout << allocated_bytes; else std::cout << "null";
        std::cout << ",\"peak_working_set_bytes\":";
        if (peak) std::cout << peak; else std::cout << "null";
        std::cout << ",\"cpu_interval_differences\":1,\"counted_parser_destructions\":" << interval.iterations - 1
#ifndef TERMFORGE_RECOVERY_MEASURE
            << ",\"counter_off_is_uninstrumented\":false"
#endif
            << ",\"receive_seconds\":null,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& e) { count_allocations = false; std::cerr << e.what() << '\n'; return 2; }
}
