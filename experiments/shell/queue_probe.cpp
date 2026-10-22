#include "queues.hpp"
#include <future>
#include <iostream>
static void check(bool condition) { if (!condition) throw std::runtime_error("queue contract failed"); }
int main() {
    try {
        OutputQueue output;
        for (int i = 0; i < 4; ++i) output.push(std::string(65536, 'x'));
        auto pending = std::async(std::launch::async, [&] { output.push(std::string(65536, 'y')); });
        check(pending.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        check(output.take(8192) == std::string(8192, 'x'));
        check(pending.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        check(output.take(57344) == std::string(57344, 'x'));
        pending.get();
        check(output.take(OutputQueue::capacity) == std::string(3 * 65536, 'x') + std::string(65536, 'y'));
        check(output.high_water == OutputQueue::capacity);
        for (int i = 0; i < 4; ++i) output.push(std::string(65536, 'x'));
        auto cancelled = std::async(std::launch::async, [&] { output.push("z"); });
        output.stop(); cancelled.get();
        check(output.take(OutputQueue::capacity) == std::string(OutputQueue::capacity, 'x'));
        tf_conpty::Session unstarted;
        InputQueue input(unstarted);
        for (int i = 0; i < 16; ++i) check(input.send(std::string(1024, 'a')));
        check(!input.send("b")); check(input.high_water == InputQueue::capacity);
        input.stop(); check(!input.send("c")); check(input.rejected == 2);
        std::cout << "bounded queues, backpressure, ordering and stop verified\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
