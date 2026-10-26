// Opt-in, one-transfer test coordination. A held payload is not OS backpressure.
#pragma once
#include "paste.hpp"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <utility>

namespace tf_shell_test {
class SenderFence {
public:
    using Clock = std::chrono::steady_clock;
    enum class State { armed, held, released, cancelled, aborted, timed_out, failed };
    enum class AbortReason { none, stop, control_eof };
    struct Ticket {
        const SenderFence* owner = nullptr;
        const tf_shell::PasteTransfer* transfer = nullptr;
        uint64_t generation = 0;
    };
    struct Snapshot {
        uint64_t generation = 0;
        State state = State::armed;
        AbortReason abort_reason = AbortReason::none;
        State wait_exit_state = State::armed;
        AbortReason wait_exit_reason = AbortReason::none;
        bool attached = false, ever_held = false, wait_finished = false;
        size_t owned_bytes = 0;
        tf_shell::PasteProgress held_progress;
    };
    static constexpr size_t minimum_sent = 16384;
private:
    const std::shared_ptr<const tf_shell::PasteTransfer> transfer_;
    const uint64_t generation_;
    const Clock::time_point deadline_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Snapshot snapshot_;

    bool matches(Ticket ticket) const noexcept {
        return ticket.owner == this && ticket.transfer == transfer_.get() && ticket.generation == generation_;
    }
    [[noreturn]] static void rejected_wait(State state) {
        if (state == State::timed_out) throw std::runtime_error("sender fence deadline expired");
        throw std::runtime_error("sender fence aborted or invalid");
    }
public:
    SenderFence(std::shared_ptr<const tf_shell::PasteTransfer> transfer, uint64_t generation,
                Clock::time_point deadline)
        : transfer_(std::move(transfer)), generation_(generation), deadline_(deadline) {
        if (!transfer_ || !generation_ || deadline_ <= Clock::now())
            throw std::invalid_argument("sender fence identity or deadline invalid");
        const auto progress = transfer_->progress();
        if (progress.state != tf_shell::PasteState::ready || progress.accepted || progress.sent ||
            progress.in_flight || progress.total <= minimum_sent)
            throw std::invalid_argument("sender fence must precede a large paste admission");
        snapshot_.generation = generation_;
    }
    Ticket ticket() const noexcept { return {this, transfer_.get(), generation_}; }
    Snapshot snapshot(Ticket ticket) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!matches(ticket)) throw std::invalid_argument("sender fence ticket differs");
        return snapshot_;
    }
    // Queue admission only. Failed admission leaves no worker waiting and must
    // not replace an existing queue binding or claim a mismatched transfer.
    bool attach(const std::shared_ptr<tf_shell::PasteTransfer>& transfer) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (transfer.get() != transfer_.get() || snapshot_.attached || snapshot_.state != State::armed)
            return false;
        const auto progress = transfer_->progress();
        if (progress.state != tf_shell::PasteState::ready || progress.accepted || progress.sent || progress.in_flight)
            return false;
        if (Clock::now() >= deadline_) { snapshot_.state = State::timed_out; return false; }
        snapshot_.attached = true;
        return true;
    }
    bool release(Ticket ticket) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!matches(ticket) || snapshot_.state != State::held) return false;
        if (Clock::now() >= deadline_) {
            snapshot_.state = State::timed_out;
            changed_.notify_all();
            return false;
        }
        snapshot_.state = State::released;
        changed_.notify_all();
        return true;
    }
    bool abort(Ticket ticket, AbortReason reason = AbortReason::control_eof) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!matches(ticket) || reason == AbortReason::none || snapshot_.state == State::aborted ||
            snapshot_.state == State::timed_out || snapshot_.state == State::failed) return false;
        snapshot_.state = State::aborted;
        snapshot_.abort_reason = reason;
        changed_.notify_all();
        return true;
    }
    // A real PasteTransfer::cancel must occur first. Wake permits only its
    // already claimed chunk to use the unchanged send/ack/finish path.
    bool cancelled(Ticket ticket) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!matches(ticket) || (snapshot_.state != State::armed && snapshot_.state != State::held)) return false;
        if (Clock::now() >= deadline_) {
            snapshot_.state = State::timed_out;
            changed_.notify_all();
            return false;
        }
        const auto state = transfer_->progress().state;
        if (state != tf_shell::PasteState::cancelling && state != tf_shell::PasteState::cancelled) return false;
        snapshot_.state = State::cancelled;
        changed_.notify_all();
        return true;
    }
    // Exactly one real writer calls this after take_chunk/in-flight publication
    // and after releasing the queue lock, immediately before Session::send.
    void before_send(const std::shared_ptr<tf_shell::PasteTransfer>& transfer, size_t owned_bytes) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (transfer.get() != transfer_.get() || !snapshot_.attached)
            throw std::logic_error("sender fence writer identity differs");
        if (snapshot_.state == State::released || snapshot_.state == State::cancelled) return;
        if (snapshot_.state != State::armed) rejected_wait(snapshot_.state);
        if (Clock::now() >= deadline_) {
            snapshot_.state = State::timed_out;
            rejected_wait(snapshot_.state);
        }
        const auto progress = transfer_->progress();
        if (progress.state == tf_shell::PasteState::cancelling) return;
        if (tf_shell::paste_terminal(progress.state)) {
            snapshot_.state = State::failed;
            rejected_wait(snapshot_.state);
        }
        if (progress.sent < minimum_sent) return;
        if (!owned_bytes || owned_bytes > tf_shell::PasteTransfer::chunk_limit ||
            progress.in_flight != owned_bytes || progress.accepted != progress.sent + owned_bytes ||
            progress.accepted >= progress.total || progress.state != tf_shell::PasteState::sending) {
            snapshot_.state = State::failed;
            rejected_wait(snapshot_.state);
        }
        snapshot_.state = State::held;
        snapshot_.ever_held = true;
        snapshot_.held_progress = progress;
        snapshot_.owned_bytes = owned_bytes;
        const bool woken = changed_.wait_until(lock, deadline_, [&] { return snapshot_.state != State::held; });
        if (!woken) snapshot_.state = State::timed_out;
        snapshot_.wait_finished = true;
        snapshot_.wait_exit_state = snapshot_.state;
        snapshot_.wait_exit_reason = snapshot_.abort_reason;
        if (snapshot_.state != State::released && snapshot_.state != State::cancelled) rejected_wait(snapshot_.state);
    }
};
} // namespace tf_shell_test
