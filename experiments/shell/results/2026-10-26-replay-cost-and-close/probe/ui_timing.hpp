#pragma once
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
struct UiProfile {
    std::array<uint64_t,17> ns{}, count{}, maximum{};
    ~UiProfile() {
        const char* path=std::getenv("TERMFORGE_DIAGNOSTIC_PHASE_OUT");
        if(!path) return;
        std::ofstream out(path);
        out<<"{\"phases\":[";
        for(size_t i=0;i<ns.size();++i) {
            if(i) out<<',';
            out<<"{\"id\":"<<i<<",\"ns\":"<<ns[i]<<",\"max_ns\":"<<maximum[i]<<",\"count\":"<<count[i]<<'}';
        }
        out<<"]}\n";
    }
};
inline UiProfile ui_profile;
struct UiPhase {
    using Clock=std::chrono::steady_clock;
    size_t phase; Clock::time_point start=Clock::now();
    explicit UiPhase(size_t p):phase(p) {}
    ~UiPhase() {
        const auto elapsed=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        ui_profile.ns[phase]+=elapsed; ++ui_profile.count[phase];
        if(elapsed>ui_profile.maximum[phase]) ui_profile.maximum[phase]=elapsed;
    }
};
