// A scenario phase waits for each independently scheduled session timer.
#pragma once
#include <array>
#include <cstdint>

namespace tf_shell_test {
class TwoSessionTickGate {
    std::array<int,2> baseline_{};
    bool armed_ = false;
public:
    void begin(std::array<int,2> ticks) { baseline_=ticks; armed_=true; }
    bool ready(std::array<int,2> ticks) const {
        if(!armed_) return false;
        for(std::size_t index=0;index<ticks.size();++index)
            if(baseline_[index]<0 || ticks[index]<baseline_[index] ||
               static_cast<int64_t>(ticks[index])-baseline_[index]<3) return false;
        return true;
    }
};
}
