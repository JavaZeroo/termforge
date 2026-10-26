// Bounded queues: output applies backpressure; UI input rejects visibly when full.
#pragma once
#include "../conpty/session.hpp"
#include "paste.hpp"
#include "sender_fence.hpp"
#include <deque>
#include <memory>

class OutputQueue {
    mutable std::mutex mutex_;
    std::condition_variable room_;
    std::deque<std::string> chunks_;
    size_t bytes_ = 0;
    bool stopped_ = false;
public:
    struct Snapshot {
        size_t queued_bytes = 0, high_water_bytes = 0;
        bool stopped = false;
    };
    static constexpr size_t capacity = 256 * 1024;
    // Legacy counters are safe to read only after the producer has joined.
    size_t high_water = 0;
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {bytes_, high_water, stopped_};
    }
    void push(std::string_view bytes) {
        if (bytes.size() > 65536) throw std::runtime_error("output chunk too large");
        std::unique_lock<std::mutex> lock(mutex_);
        room_.wait(lock, [&] { return stopped_ || bytes_ + bytes.size() <= capacity; });
        if (stopped_) return;
        chunks_.emplace_back(bytes); bytes_ += bytes.size();
        high_water = (std::max)(high_water, bytes_);
    }
    std::string take(size_t budget) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out;
        while (!chunks_.empty() && out.size() < budget) {
            auto& first = chunks_.front();
            const auto n = (std::min)(budget - out.size(), first.size());
            out.append(first, 0, n); first.erase(0, n); bytes_ -= n;
            if (first.empty()) chunks_.pop_front();
        }
        room_.notify_one(); return out;
    }
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_); stopped_ = true; room_.notify_all();
    }
};

class InputQueue {
    tf_conpty::Session& session_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> chunks_;
    size_t bytes_ = 0;
    size_t in_flight_bytes_ = 0;
    bool stopping_ = false, started_ = false, joined_ = false;
    std::exception_ptr error_;
    std::thread writer_;
    std::shared_ptr<tf_shell::PasteTransfer> paste_;
    std::shared_ptr<tf_shell_test::SenderFence> sender_fence_;
public:
    struct Snapshot {
        size_t queued_bytes = 0, in_flight_bytes = 0, high_water_bytes = 0, rejected = 0;
        bool stopping = false, started = false, joined = false, failed = false;
    };
    static constexpr size_t capacity = 16 * 1024;
    // The legacy high water is max(queued keystroke bytes, individual paste
    // chunk bytes); it is not queued + in-flight or a total retained budget.
    // Legacy counters are safe to read only after the writer has joined.
    size_t high_water = 0, rejected = 0;
    explicit InputQueue(tf_conpty::Session& session) : session_(session) {}
    ~InputQueue() { stop(); }
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        // in_flight_bytes is the worker-owned string payload, not the bytes
        // remaining in WriteFile or any hidden console/kernel pipe buffering.
        return {bytes_, in_flight_bytes_, high_water, rejected, stopping_, started_, joined_, error_ != nullptr};
    }
    void start() {
        std::lock_guard<std::mutex> starting(mutex_);
        writer_ = std::thread([this] {
            std::shared_ptr<tf_shell::PasteTransfer> writing_paste;
            try {
                for (;;) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || !chunks_.empty() ||
                        (paste_ && !tf_shell::paste_terminal(paste_->progress().state)); });
                    if (stopping_) break;
                    std::string bytes;
                    std::shared_ptr<tf_shell_test::SenderFence> fence;
                    writing_paste.reset();
                    if (!chunks_.empty()) {
                        bytes = std::move(chunks_.front()); chunks_.pop_front(); bytes_ -= bytes.size();
                    } else {
                        writing_paste = paste_; bytes = writing_paste->take_chunk();
                        fence = sender_fence_;
                        high_water = (std::max)(high_water, bytes.size());
                    }
                    in_flight_bytes_ = bytes.size();
                    lock.unlock();
                    if (bytes.empty()) continue; // Cancelled between the predicate and take.
                    if (writing_paste) {
                        if (fence) {
                            fence->before_send(writing_paste, bytes.size());
                            // A CV wait is not cancelled by CancelSynchronousIo.
                            // stop() first publishes stopping and wakes it; do
                            // not start a new write after that observation.
                            std::lock_guard<std::mutex> stopped(mutex_);
                            if (stopping_) throw std::runtime_error("input stopped at sender fence");
                        }
                        session_.send(bytes, [&](size_t written) { writing_paste->acknowledge_written(written); });
                        writing_paste->finish_chunk();
                    } else session_.send(bytes);
                    bytes.clear(); // Payload is released; string allocation is not measured here.
                    lock.lock(); in_flight_bytes_ = 0;
                }
            } catch (const std::exception& error) {
                if (writing_paste) writing_paste->fail(error.what());
                std::lock_guard<std::mutex> lock(mutex_);
                in_flight_bytes_ = 0;
                if (!stopping_) error_ = std::current_exception();
                if (paste_) paste_->fail(error.what());
            } catch (...) {
                if (writing_paste) writing_paste->fail("unknown input writer failure");
                std::lock_guard<std::mutex> lock(mutex_);
                in_flight_bytes_ = 0;
                if (!stopping_) error_ = std::current_exception();
                if (paste_) paste_->fail("unknown input writer failure");
            }
        });
        started_ = true;
    }
    bool send(const std::string& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (stopping_ || bytes.empty() || bytes.size() > 1024 || bytes_ + bytes.size() > capacity ||
            (paste_ && !tf_shell::paste_terminal(paste_->progress().state))) { ++rejected; return false; }
        chunks_.push_back(bytes); bytes_ += bytes.size(); high_water = (std::max)(high_water, bytes_);
        ready_.notify_one(); return true;
    }
    // The queue is already bound to one Session. The UI retains the transfer
    // handle and target identity; later tab switches never retarget this queue.
    // Previously accepted keystrokes drain first. New keystrokes are rejected
    // while pasting, so they cannot interleave with clipboard bytes.
    bool begin_paste(const std::shared_ptr<tf_shell::PasteTransfer>& paste,
                     const std::shared_ptr<tf_shell_test::SenderFence>& fence = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (stopping_ || !paste || (paste_ && !tf_shell::paste_terminal(paste_->progress().state))) return false;
        if (fence && !fence->attach(paste)) return false;
        if (!paste->claim_writer()) {
            if (fence) fence->abort(fence->ticket());
            return false;
        }
        paste_ = paste; sender_fence_ = fence; ready_.notify_one(); return true;
    }
    void cancel_paste() {
        std::shared_ptr<tf_shell_test::SenderFence> fence;
        { std::lock_guard<std::mutex> lock(mutex_);
          if (paste_) paste_->cancel();
          fence = sender_fence_; ready_.notify_all(); }
        if (fence) fence->cancelled(fence->ticket());
    }
    void stop() noexcept {
        std::shared_ptr<tf_shell_test::SenderFence> fence;
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; chunks_.clear(); bytes_ = 0;
          if (paste_) paste_->cancel(); fence = sender_fence_; ready_.notify_all(); }
        if (fence) fence->abort(fence->ticket(), tf_shell_test::SenderFence::AbortReason::stop);
        if (writer_.joinable()) {
            // Retry covers the race where cancellation runs just before WriteFile.
            while (WaitForSingleObject(writer_.native_handle(), 10) == WAIT_TIMEOUT)
                CancelSynchronousIo(writer_.native_handle());
            writer_.join();
            std::lock_guard<std::mutex> lock(mutex_); joined_ = true;
        }
    }
};
