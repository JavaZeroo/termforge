// Independent mechanism experiment. No WindTerm code or runtime dependency.
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <iterator>
#include "../core/terminal.hpp"

using Clock = std::chrono::steady_clock;
struct Block { std::vector<unsigned char> bytes; Clock::time_point queued; };
static uint64_t ticks(FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
static double cpu_ms() {
    FILETIME a{}, b{}, k{}, u{};
    if (!GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u)) std::abort();
    return double(ticks(k) + ticks(u)) / 10000;
}

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) { std::cerr << "receive chunk each|edge stream|burst|slow|yield bytes capacity [ASCII input file]\n"; return 2; }
    size_t chunk, total, capacity;
    try { chunk = std::stoull(argv[1]); total = std::stoull(argv[4]); capacity = std::stoull(argv[5]); }
    catch (...) { return 2; }
    const std::string policy = argv[2], mode = argv[3];
    if ((chunk != 512 && chunk != 4096 && chunk != 65536) || total == 0 ||
        total > 64 * 1024 * 1024 || capacity < chunk || capacity > 1024 * 1024 ||
        (policy != "each" && policy != "edge") ||
        (mode != "stream" && mode != "burst" && mode != "slow" && mode != "yield")) return 2;
    // Reproducible xorshift32 fixture, generated before the timed region.
    std::vector<unsigned char> expected(total);
    uint32_t state = 0x12345678;
    for (auto& byte : expected) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = static_cast<unsigned char>(state); }
    const bool parse = argc == 7;
    if (parse) {
        std::ifstream input(argv[6], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() != static_cast<std::streamoff>(total)) return 2;
        input.seekg(0); input.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(total));
        if (!input) return 2;
    }
    termforge::Parser parser;
    std::string parser_error;
    double parse_ms = 0;
    HANDLE read_pipe{}, write_pipe{};
    if (!CreatePipe(&read_pipe, &write_pipe, nullptr, 65536)) return 3;
    std::mutex mutex;
    std::condition_variable ready, room;
    std::deque<Block> queue;
    size_t queued = 0, peak = 0, consumed = 0, reads = 0, notifications = 0;
    size_t waits = 0, backpressure = 0, min_read = chunk, max_read = 0;
    bool done = false, valid = true;
    DWORD writer_error = 0, reader_error = 0;
    std::vector<double> latencies;
    const auto start = Clock::now();
    const double cpu_start = cpu_ms();
    std::thread consumer([&] {
        for (;;) {
            Block block;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (queue.empty() && !done) ++waits;
                ready.wait(lock, [&] { return !queue.empty() || done; });
                if (queue.empty()) break;
                block = std::move(queue.front()); queue.pop_front();
                queued -= block.bytes.size();
            }
            room.notify_one();
            if (mode == "slow") std::this_thread::sleep_for(std::chrono::microseconds(100));
            if (mode == "yield") std::this_thread::yield();
            if (consumed + block.bytes.size() > expected.size() ||
                std::memcmp(expected.data() + consumed, block.bytes.data(), block.bytes.size()) != 0) valid = false;
            consumed += block.bytes.size();
            if (parse && parser_error.empty()) {
                const auto parse_start = Clock::now();
                try { parser.feed(std::string_view(reinterpret_cast<const char*>(block.bytes.data()), block.bytes.size())); }
                catch (const std::exception& e) { parser_error = e.what(); }
                parse_ms += std::chrono::duration<double, std::milli>(Clock::now() - parse_start).count();
            }
            latencies.push_back(std::chrono::duration<double, std::micro>(Clock::now() - block.queued).count());
        }
    });
    std::thread writer([&] {
        size_t offset = 0;
        while (offset < total) {
            DWORD count = 0;
            const DWORD size = static_cast<DWORD>(std::min<size_t>(16384, total - offset));
            if (!WriteFile(write_pipe, expected.data() + offset, size, &count, nullptr) || count == 0) {
                writer_error = GetLastError(); if (!writer_error) writer_error = ERROR_WRITE_FAULT; break;
            }
            offset += count;
            if (mode == "burst" && offset % 65536 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CloseHandle(write_pipe);
    });
    for (;;) {
        Block block{std::vector<unsigned char>(chunk), {}};
        DWORD count = 0;
        if (!ReadFile(read_pipe, block.bytes.data(), static_cast<DWORD>(chunk), &count, nullptr)) {
            reader_error = GetLastError();
            if (reader_error == ERROR_BROKEN_PIPE) reader_error = 0;
            break;
        }
        if (!count) break;
        ++reads; min_read = std::min<size_t>(min_read, count); max_read = std::max<size_t>(max_read, count);
        block.bytes.resize(count);
        bool signal;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (queued + count > capacity) ++backpressure;
            room.wait(lock, [&] { return queued + count <= capacity; });
            signal = policy == "each" || queue.empty();
            block.queued = Clock::now();
            queued += count; peak = std::max(peak, queued);
            queue.push_back(std::move(block));
        }
        if (signal) { ++notifications; ready.notify_one(); }
        if (mode == "yield") std::this_thread::yield();
    }
    CloseHandle(read_pipe);
    { std::lock_guard<std::mutex> lock(mutex); done = true; }
    ready.notify_one(); // EOF notification is excluded from data-notification count.
    writer.join(); consumer.join();
    if (parse && parser_error.empty()) {
        try { parser.finish(); } catch (const std::exception& e) { parser_error = e.what(); }
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    const double cpu = cpu_ms() - cpu_start;
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) return 4;
    std::sort(latencies.begin(), latencies.end());
    auto quantile = [&](double q) { return latencies.empty() ? 0 : latencies[static_cast<size_t>((latencies.size() - 1) * q)]; };
    valid = valid && consumed == total && !writer_error && !reader_error && peak <= capacity && parser_error.empty();
    std::cout << "{\"valid\":" << (valid ? "true" : "false")
        << ",\"chunk\":" << chunk << ",\"policy\":\"" << policy << "\",\"mode\":\"" << mode
        << "\",\"bytes\":" << total << ",\"consumed\":" << consumed << ",\"capacity\":" << capacity
        << ",\"reads\":" << reads << ",\"min_read\":" << min_read << ",\"max_read\":" << max_read
        << ",\"notifications\":" << notifications << ",\"consumer_waits\":" << waits
        << ",\"backpressure_waits\":" << backpressure << ",\"peak_queued_bytes\":" << peak
        << ",\"peak_working_set_bytes\":" << memory.PeakWorkingSetSize
        << ",\"elapsed_seconds\":" << elapsed << ",\"cpu_ms\":" << cpu
        << ",\"mib_per_second\":" << total / 1048576.0 / elapsed
        << ",\"queue_to_validated_block_p50_us\":" << quantile(.5)
        << ",\"queue_to_validated_block_p95_us\":" << quantile(.95)
        << ",\"writer_error\":" << writer_error << ",\"reader_error\":" << reader_error;
    if (parse) {
        std::cout << ",\"parse_ms\":" << parse_ms << ",\"parser_error\":";
        termforge::Screen::quoted(std::cout, parser_error);
        if (parser_error.empty()) { std::cout << ",\"screen\":"; parser.screen.json(std::cout); }
    }
    std::cout << "}\n";
    return valid ? 0 : 1;
}
