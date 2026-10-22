// Bounded queues: output applies backpressure; UI input rejects visibly when full.
#pragma once
#include "../conpty/session.hpp"
#include <deque>

class OutputQueue {
    std::mutex mutex_;
    std::condition_variable room_;
    std::deque<std::string> chunks_;
    size_t bytes_ = 0;
    bool stopped_ = false;
public:
    static constexpr size_t capacity = 256 * 1024;
    size_t high_water = 0;
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
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> chunks_;
    size_t bytes_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    std::thread writer_;
public:
    static constexpr size_t capacity = 16 * 1024;
    size_t high_water = 0, rejected = 0;
    explicit InputQueue(tf_conpty::Session& session) : session_(session) {}
    ~InputQueue() { stop(); }
    void start() {
        writer_ = std::thread([this] {
            try {
                for (;;) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || !chunks_.empty(); });
                    if (stopping_) break;
                    auto bytes = std::move(chunks_.front()); chunks_.pop_front(); bytes_ -= bytes.size();
                    lock.unlock(); session_.send(bytes);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) error_ = std::current_exception();
            }
        });
    }
    bool send(const std::string& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (stopping_ || bytes.empty() || bytes.size() > 1024 || bytes_ + bytes.size() > capacity) { ++rejected; return false; }
        chunks_.push_back(bytes); bytes_ += bytes.size(); high_water = (std::max)(high_water, bytes_);
        ready_.notify_one(); return true;
    }
    void stop() noexcept {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; chunks_.clear(); bytes_ = 0; ready_.notify_all(); }
        if (writer_.joinable()) {
            // Retry covers the race where cancellation runs just before WriteFile.
            while (WaitForSingleObject(writer_.native_handle(), 10) == WAIT_TIMEOUT)
                CancelSynchronousIo(writer_.native_handle());
            writer_.join();
        }
    }
};
