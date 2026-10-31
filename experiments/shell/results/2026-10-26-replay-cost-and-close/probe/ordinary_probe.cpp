#define NOMINMAX
#include <windows.h>
#include "candidate_terminal.hpp"
#include "text_projection.hpp"
#include "probe_timing.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <sstream>

uint64_t cpu_ns() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("GetProcessTimes failed");
    const auto value = [](const FILETIME& ft) { return (uint64_t(ft.dwHighDateTime)<<32) | ft.dwLowDateTime; };
    return (value(kernel)+value(user))*100;
}
int main(int argc, char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: phase_probe input.bin state.json");
        std::ifstream input(argv[1], std::ios::binary);
        if(!input) throw std::runtime_error("input missing");
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        if(bytes.size()!=2048000) throw std::runtime_error("unexpected workload");
        termforge::CandidateTerminal terminal(17,74,100);
        probe_ns.fill(0); probe_count.fill(0); probe_max.fill(0);
        const auto cpu_start=cpu_ns(); const auto start=ProbeTimer::Clock::now();
        for(size_t i=0;i<bytes.size();i+=8192)
            terminal.feed(std::string_view(bytes).substr(i,8192));
        const auto wall_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(ProbeTimer::Clock::now()-start).count();
        const auto cpu_elapsed=cpu_ns()-cpu_start;
        terminal.finish();
        const auto& screen=terminal.screen;
        if(screen.row!=16 || screen.column!=0 || screen.history.size()!=100 || screen.retained_size()!=117 || screen.discarded!=31884)
            throw std::runtime_error("final cursor/history mismatch");
        for(size_t r=0;r<116;++r) {
            std::ostringstream expected;
            expected<<"TF_R108_F1_"<<std::setw(5)<<std::setfill('0')<<(31884+r)<<" \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81";
            auto wanted=expected.str(); wanted.resize(62,'.');
            if(termforge::project_line(screen.retained_line(r)).text != wanted)
                throw std::runtime_error("retained text mismatch");
        }
        std::ofstream state(argv[2]); screen.json(state);
        if(!state) throw std::runtime_error("state write failed");
        std::cout<<"{\"bytes\":"<<bytes.size()<<",\"cpu_ns\":"<<cpu_elapsed<<",\"wall_ns\":"<<wall_ns<<",\"valid\":true,\"phases\":[";
        for(size_t i=0;i<probe_ns.size();++i) {
            if(i) std::cout<<',';
            std::cout<<"{\"id\":"<<i<<",\"ns\":"<<probe_ns[i]<<",\"max_ns\":"<<probe_max[i]<<",\"count\":"<<probe_count[i]<<'}';
        }
        std::cout<<"]}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 2; }
}
