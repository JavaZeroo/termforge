// Pure coordination tests. Synthetic acknowledgements prepare protocol state;
// these tests never establish real transport submission or OS backpressure.
#include "sender_fence.hpp"
#include <future>
#include <iostream>
#include <thread>

namespace {
using Fence = tf_shell_test::SenderFence;
using Transfer = tf_shell::PasteTransfer;
using namespace std::chrono_literals;

void check(bool condition, const char* detail) {
    if (!condition) throw std::runtime_error(detail);
}
template<class F> void rejects(F function, const char* detail) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, detail);
}
std::shared_ptr<Transfer> transfer(size_t bytes = 4 * 1024 * 1024) {
    tf_shell::PreparedPaste prepared;
    prepared.bytes.assign(bytes, 'a');
    return std::make_shared<Transfer>(7, std::move(prepared));
}
std::shared_ptr<Fence> fence(const std::shared_ptr<Transfer>& paste,
                             Fence::Clock::duration duration = 2s) {
    return std::make_shared<Fence>(paste, 19, Fence::Clock::now() + duration);
}
// Unit setup only: prepare the exact real-PasteTransfer state reached after
// sixteen complete 1024B writes, then claim the seventeenth chunk.
std::string claimed_chunk(const std::shared_ptr<Transfer>& paste) {
    for (size_t i = 0; i < 16; ++i) {
        const auto bytes = paste->take_chunk();
        check(bytes.size() == 1024, "unit prefix chunk differs");
        paste->acknowledge_written(bytes.size());
        paste->finish_chunk();
    }
    return paste->take_chunk();
}
std::future<bool> wait(const std::shared_ptr<Fence>& gate, const std::shared_ptr<Transfer>& paste,
                        size_t owned_bytes) {
    return std::async(std::launch::async, [gate, paste, owned_bytes] {
        try { gate->before_send(paste, owned_bytes); return true; }
        catch (const std::exception&) { return false; }
    });
}
void held(const std::shared_ptr<Fence>& gate) {
    const auto deadline = Fence::Clock::now() + 1s;
    while (gate->snapshot(gate->ticket()).state != Fence::State::held) {
        check(Fence::Clock::now() < deadline, "unit writer did not reach held state");
        std::this_thread::yield();
    }
}
void identities_and_admission() {
    auto paste = transfer();
    rejects([&] { Fence invalid(paste, 0, Fence::Clock::now() + 1s); }, "zero generation accepted");
    rejects([&] { Fence invalid(paste, 1, Fence::Clock::now()); }, "expired deadline accepted");
    rejects([&] { Fence invalid({}, 1, Fence::Clock::now() + 1s); }, "missing transfer accepted");
    auto gate = fence(paste);
    auto ticket = gate->ticket();
    check(!gate->release(ticket), "release before held accepted");
    check(!gate->cancelled(ticket), "fake cancellation accepted");
    auto other = transfer();
    check(!gate->attach(other), "different transfer admitted");
    check(gate->attach(paste) && !gate->attach(paste), "attachment is not single use");
    ++ticket.generation;
    check(!gate->release(ticket) && !gate->abort(ticket), "wrong generation changed fence");
    rejects([&] { gate->snapshot(ticket); }, "wrong generation observed fence");
    ticket = gate->ticket(); ticket.transfer = other.get();
    rejects([&] { gate->snapshot(ticket); }, "wrong transfer observed fence");
}
void threshold_and_release() {
    auto paste = transfer(); auto gate = fence(paste);
    check(gate->attach(paste), "attach failed");
    const auto first = paste->take_chunk();
    gate->before_send(paste, first.size());
    check(!gate->snapshot(gate->ticket()).ever_held, "fence lowered sent floor");
    paste->acknowledge_written(first.size()); paste->finish_chunk();
    for (size_t i = 1; i < 16; ++i) {
        const auto chunk = paste->take_chunk();
        paste->acknowledge_written(chunk.size()); paste->finish_chunk();
    }
    const auto current = paste->take_chunk();
    auto worker = wait(gate, paste, current.size()); held(gate);
    const auto before = gate->snapshot(gate->ticket());
    check(before.ever_held && before.owned_bytes == 1024 && before.held_progress.sent == 16384 &&
          before.held_progress.accepted == 17408 && before.held_progress.in_flight == 1024,
          "held snapshot does not describe real transfer state");
    auto wrong = gate->ticket(); ++wrong.generation;
    check(!gate->release(wrong) && worker.wait_for(0ms) == std::future_status::timeout,
          "wrong generation released held writer");
    check(gate->release(gate->ticket()) && !gate->release(gate->ticket()), "release duplicate accepted");
    check(worker.get(), "valid release failed");
    check(paste->progress().sent == 16384, "fence fabricated write acknowledgement");
    paste->acknowledge_written(current.size()); paste->finish_chunk();
    check(gate->snapshot(gate->ticket()).wait_finished, "release did not finish wait");
}
void cancellation_preserves_claimed_chunk(bool before_wait) {
    auto paste = transfer(); auto gate = fence(paste);
    check(gate->attach(paste), "cancel attach failed");
    const auto current = claimed_chunk(paste);
    std::future<bool> worker;
    if (!before_wait) { worker = wait(gate, paste, current.size()); held(gate); }
    paste->cancel();
    check(gate->cancelled(gate->ticket()), "real cancellation did not wake fence");
    if (before_wait) gate->before_send(paste, current.size());
    else check(worker.get(), "cancellation aborted claimed chunk instead of allowing it");
    check(paste->progress().state == tf_shell::PasteState::cancelling && paste->progress().sent == 16384,
          "cancellation prematurely completed an unsubmitted chunk");
    paste->acknowledge_written(current.size()); paste->finish_chunk();
    const auto after = paste->progress();
    check(after.state == tf_shell::PasteState::cancelled && after.sent == after.accepted &&
          after.sent == 17408 && !after.in_flight && after.retained_capacity <= 32,
          "cancel accounting or capacity differs");
}
void abort_wakes(Fence::AbortReason reason) {
    auto paste = transfer(); auto gate = fence(paste);
    check(gate->attach(paste), "abort attach failed");
    const auto current = claimed_chunk(paste);
    auto worker = wait(gate, paste, current.size()); held(gate);
    check(gate->abort(gate->ticket(), reason) && !gate->abort(gate->ticket(), reason), "abort duplicate accepted");
    check(worker.wait_for(1s) == std::future_status::ready && !worker.get(), "abort did not finish wait as failure");
    check(gate->snapshot(gate->ticket()).abort_reason == reason && paste->progress().sent == 16384,
          "abort lost reason or fabricated acknowledgement");
    paste->fail("unit writer aborted before submission");
    check(!paste->progress().in_flight && paste->progress().retained_capacity <= 32, "writer failure did not release payload");
}
void no_go_times_out() {
    auto paste = transfer(); auto gate = fence(paste, 1s);
    check(gate->attach(paste), "timeout attach failed");
    const auto current = claimed_chunk(paste);
    auto worker = wait(gate, paste, current.size());
    check(worker.wait_for(2s) == std::future_status::ready && !worker.get(), "missing GO waited without bound");
    const auto after = gate->snapshot(gate->ticket());
    check(after.state == Fence::State::timed_out && after.ever_held && after.wait_finished &&
          !gate->release(gate->ticket()), "timeout was converted to release");
}
void premature_completion_is_rejected() {
    auto paste = transfer(17 * 1024); auto gate = fence(paste);
    check(gate->attach(paste), "small large-paste attach failed");
    const auto current = claimed_chunk(paste);
    rejects([&] { gate->before_send(paste, current.size()); }, "last accepted chunk was treated as a partial transfer");
    check(!gate->snapshot(gate->ticket()).ever_held, "completed admission manufactured held state");
}
} // namespace

int main() {
    try {
        identities_and_admission(); threshold_and_release();
        cancellation_preserves_claimed_chunk(false); cancellation_preserves_claimed_chunk(true);
        abort_wakes(Fence::AbortReason::stop); abort_wakes(Fence::AbortReason::control_eof);
        no_go_times_out(); premature_completion_is_rejected();
        std::cout << "sender fence pure protocol checks passed; no transport or OS backpressure claim\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
