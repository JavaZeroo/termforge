// Explicit synthetic-consumer coordination; disabled during ordinary use.
#pragma once
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace tf_shell_test {
class OutputRecordCheckpoint {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr size_t byte_limit=256;
private:
    bool armed_=false,held_=false;
    size_t consumed_=0;
    Clock::time_point deadline_{};
public:
    void arm(bool at_boundary,Clock::time_point deadline) {
        if(armed_) return;
        if(deadline<=Clock::now()) throw std::invalid_argument("output checkpoint deadline expired");
        deadline_=deadline;armed_=true;held_=at_boundary;consumed_=0;
    }
    size_t read_budget(size_t normal) const {
        if(!armed_) return normal;
        if(Clock::now()>=deadline_) throw std::runtime_error("output checkpoint deadline expired");
        if(!held_ && consumed_>=byte_limit) throw std::runtime_error("output checkpoint record bound exceeded");
        return held_?0:1;
    }
    void consumed(std::string_view bytes) {
        if(!armed_) return;
        if(held_ || bytes.size()!=1) throw std::runtime_error("output checkpoint read boundary violated");
        if(++consumed_>byte_limit) throw std::runtime_error("output checkpoint record bound exceeded");
        if(bytes.front()=='\n') held_=true;
    }
    bool held() const { return held_; }
    bool active() const { return armed_; }
    void release() noexcept { armed_=held_=false;consumed_=0; }
};
} // namespace tf_shell_test
