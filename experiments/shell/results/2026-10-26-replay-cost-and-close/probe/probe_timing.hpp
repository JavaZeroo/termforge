#pragma once
#include <array>
#include <chrono>
#include <cstdint>
inline std::array<uint64_t, 6> probe_ns{}, probe_max{}, probe_count{};
struct ProbeTimer {
    using Clock = std::chrono::steady_clock;
    size_t phase;
    Clock::time_point start = Clock::now();
    explicit ProbeTimer(size_t value) : phase(value) {}
    ~ProbeTimer() {
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        probe_ns[phase] += elapsed; ++probe_count[phase];
        if (elapsed > probe_max[phase]) probe_max[phase] = elapsed;
    }
};
