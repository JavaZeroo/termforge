#include "scenario_tick_gate.hpp"
#include <climits>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* detail) { if(!value) throw std::runtime_error(detail); }

int main() {
    try {
        tf_shell_test::TwoSessionTickGate gate;
        check(!gate.ready({3,3}),"an unstarted phase has no timer evidence");
        // The real CI forward snapshot was bottom=14/main=11. At reverse,
        // main had three ticks but bottom only two; recording was premature.
        gate.begin({14,11});
        check(!gate.ready({16,14}),"the captured skew must not pass on main ticks alone");
        check(gate.ready({17,14}),"each session meeting its own three ticks is sufficient");
        check(!gate.ready({17,13}),"main must also reach its independent threshold");
        check(!gate.ready({13,14}),"a regressed session counter is not progress");
        gate.begin({100,7});
        check(!gate.ready({102,100}),"a faster main cannot cover a slow bottom");
        check(!gate.ready({103,9}),"a new phase replaces both prior baselines");
        check(gate.ready({103,10}),"different absolute counters use independent baselines");
        gate.begin({INT_MAX-3,INT_MAX-3});
        check(gate.ready({INT_MAX,INT_MAX}),"threshold arithmetic must not overflow");
        gate.begin({-1,0});
        check(!gate.ready({3,3}),"negative baseline is invalid evidence");
        std::cout<<"scenario two-session timer checks passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
