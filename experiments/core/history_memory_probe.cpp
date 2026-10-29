// Diagnostic only (issue #164 remainder): retained-history memory and a single
// bounded Search at fixed history sizes on the production Screen/TextSearch.
// It reports what it measured and asserts nothing about performance targets.
#include "screen.hpp"
#include "terminal.hpp"
#include "text_document.hpp"
#include "text_search.hpp"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

using namespace termforge;

namespace {
struct Usage { SIZE_T working = 0, peak_working = 0, private_bytes = 0; };

Usage current_usage() {
    using Fn = BOOL(WINAPI*)(HANDLE, PROCESS_MEMORY_COUNTERS_EX*, DWORD);
    static const auto fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!fn || !fn(GetCurrentProcess(), &counters, sizeof(counters))) return {};
    return {counters.WorkingSetSize, counters.PeakWorkingSetSize, counters.PrivateUsage};
}
long long micros(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
}
} // namespace

int main(int argc, char** argv) {
    size_t history = 0, lines = 0;
    std::string query = "needle";
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--history" && index + 1 < argc) history = static_cast<size_t>(std::stoul(argv[++index]));
        else if (arg == "--lines" && index + 1 < argc) lines = static_cast<size_t>(std::stoul(argv[++index]));
        else if (arg == "--query" && index + 1 < argc) query = argv[++index];
        else { std::cerr << "usage: history_memory_probe --history N --lines M [--query text]\n"; return 2; }
    }
    if (!history || history > 10000 || !lines) { std::cerr << "history 1..10000 and lines>0 required\n"; return 2; }

    const Usage before = current_usage();
    Parser parser(24, 80, history);
    // Fill the retained history: the live screen keeps 24 rows, the rest evict
    // into the bounded history in document order.
    const auto fill_begin = std::chrono::steady_clock::now();
    std::string batch;
    batch.reserve(1 << 20);
    for (size_t line = 0; line < lines; ++line) {
        char prefix[16];
        std::snprintf(prefix, sizeof(prefix), "R%06zu ", line);
        std::string text = prefix;
        // One hit every 1000 rows, otherwise harmless filler.
        if (line % 1000 == 0) text += query;
        else text += "payload-";
        while (text.size() < 78) text += static_cast<char>('a' + (line % 26));
        text += "\r\n";
        batch += text;
    }
    parser.feed(batch);
    const auto fill_end = std::chrono::steady_clock::now();
    Sleep(150); // Let the allocator settle before the snapshot is taken.
    const Usage after_fill = current_usage();

    const TextDocument document(parser.screen);
    TextSearch search;
    const auto search_begin = std::chrono::steady_clock::now();
    const auto generation = search.start(document, query);
    size_t steps = 0, work = 0;
    while (search.status() == SearchStatus::running && steps < 1000000) {
        const auto progress = search.step(document, generation, 4096);
        work += progress.work_used;
        ++steps;
    }
    const auto search_end = std::chrono::steady_clock::now();

    std::cout << "{\"history_limit\":" << history
              << ",\"lines_fed\":" << lines
              << ",\"retained\":" << document.size()
              << ",\"history_rows\":" << parser.screen.history_size()
              << ",\"query\":\"" << query << "\""
              << ",\"search_status\":" << static_cast<int>(search.status())
              << ",\"search_matches\":" << search.matches().size()
              << ",\"search_steps\":" << steps
              << ",\"search_work\":" << work
              << ",\"fill_us\":" << micros(fill_begin, fill_end)
              << ",\"search_us\":" << micros(search_begin, search_end)
              << ",\"working_before\":" << before.working
              << ",\"working_after\":" << after_fill.working
              << ",\"private_before\":" << before.private_bytes
              << ",\"private_after\":" << after_fill.private_bytes
              << ",\"peak_working\":" << after_fill.peak_working
              << "}" << std::endl;
    return 0;
}
