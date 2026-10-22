// Single-thread core cost probe. Ordinary allocation counts are diagnostic, not product RSS.
#include "../core/terminal.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <new>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

static bool count_allocations = false;
static size_t allocations = 0, allocated_bytes = 0;
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
using Clock = std::chrono::steady_clock;
static double cpu_seconds() {
#ifdef _WIN32
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) throw std::runtime_error("GetProcessTimes failed");
    auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return double(value(kernel) + value(user)) / 1e7;
#else
    return double(std::clock()) / CLOCKS_PER_SEC;
#endif
}
static uint64_t hash_state(const termforge::Screen& screen) {
    std::ostringstream json; screen.json(json);
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : json.str()) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}
int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("core_measure parse|state chunk observe(0|1) min_cpu_seconds");
        const std::string route = argv[1];
        const size_t chunk = std::stoull(argv[2]);
        const int observe = std::stoi(argv[3]);
        const double duration = std::stod(argv[4]);
        if ((route != "parse" && route != "state") || chunk == 0 || chunk > 1048576 ||
            (observe != 0 && observe != 1) || !(duration >= .1 && duration <= 10)) throw std::runtime_error("invalid arguments");
        // Fixed fixture, prepared before all measured intervals. 1,000 x 78 payload bytes.
        const std::string line(76, 'x');
        std::string payload;
        for (int i = 0; i < 1000; ++i) payload += line + "\r\n";
        auto state_only = [&line](termforge::Screen& screen) {
            for (int i = 0; i < 1000; ++i) {
                for (char c : line) screen.put(static_cast<unsigned char>(c), std::string(1, c));
                screen.move(screen.row, 0); screen.newline();
            }
        };
        // Independent expected physical rows for this simple fixture.
        auto correct = [&line](const termforge::Screen& s) {
            if (s.row != 23 || s.column != 0 || s.wrap_pending || s.discarded != 877 || s.history.size() != 100) return false;
            for (int r = 0; r < 24; ++r) for (int c = 0; c < 80; ++c) {
                const auto& cell = s.lines[r][c];
                if (cell.text != (r < 23 && c < static_cast<int>(line.size()) ? "x" : " ") || cell.width != 1 || !cell.style.plain()) return false;
            }
            for (const auto& row : s.history) for (int c = 0; c < 80; ++c)
                if (row[c].text != (c < 76 ? "x" : " ") || row[c].width != 1 || !row[c].style.plain()) return false;
            return true;
        };
        termforge::Parser warmup;
        warmup.feed(payload); warmup.finish();
        if (!correct(warmup.screen)) throw std::runtime_error("warmup oracle failed");
        const auto expected_hash = hash_state(warmup.screen);
        size_t rounds = 0;
        double work_seconds = 0, checks_seconds = 0, cpu_work = 0;
        const auto overall = Clock::now();
        // Per-round CPU deltas exclude oracle/serialization and destruction; include parser construction.
        while (cpu_work < duration || work_seconds < duration) {
            const auto started = Clock::now();
            const double cpu_start = cpu_seconds();
            count_allocations = observe != 0;
            termforge::Parser parser;
            if (route == "parse") {
                for (size_t offset = 0; offset < payload.size(); offset += chunk)
                    parser.feed(std::string_view(payload).substr(offset, chunk));
                parser.finish();
            } else state_only(parser.screen);
            count_allocations = false;
            cpu_work += cpu_seconds() - cpu_start;
            work_seconds += std::chrono::duration<double>(Clock::now() - started).count();
            const auto checks = Clock::now();
            if (!correct(parser.screen) || hash_state(parser.screen) != expected_hash) throw std::runtime_error("state oracle failed");
            checks_seconds += std::chrono::duration<double>(Clock::now() - checks).count();
            ++rounds;
            if (std::chrono::duration<double>(Clock::now() - overall).count() > 60) throw std::runtime_error("time budget exceeded");
        }
        size_t peak = 0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) throw std::runtime_error("memory query failed");
        peak = memory.PeakWorkingSetSize;
#endif
        std::cout << "{\"valid\":true,\"route\":\"" << route << "\",\"chunk\":" << chunk
            << ",\"observe\":" << observe << ",\"rounds\":" << rounds << ",\"bytes_per_round\":" << payload.size()
            << ",\"work_seconds\":" << work_seconds << ",\"work_cpu_seconds\":" << cpu_work
            << ",\"oracle_seconds\":" << checks_seconds << ",\"state_fnv1a64\":\"" << expected_hash << "\""
            << ",\"ordinary_allocations\":";
        if (observe) std::cout << allocations; else std::cout << "null";
        std::cout << ",\"ordinary_allocated_bytes\":";
        if (observe) std::cout << allocated_bytes; else std::cout << "null";
        std::cout << ",\"peak_working_set_bytes\":";
        if (peak) std::cout << peak; else std::cout << "null";
        std::cout << ",\"receive_seconds\":null,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
