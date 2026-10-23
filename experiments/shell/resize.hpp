// One in-flight resize plus one coalesced request. UI keeps draining output
// while the console host processes resize. Join before closing its HPCON.
#pragma once
#include "../conpty/session.hpp"
class ResizeWorker {
    tf_conpty::Session& session_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    int rows_ = 24, columns_ = 80;
    unsigned requested_ = 0, completed_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
public:
    explicit ResizeWorker(tf_conpty::Session& session) : session_(session) {}
    ~ResizeWorker() { stop(); }
    void start() {
        thread_ = std::thread([this] {
            try {
                for (;;) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || requested_ != completed_; });
                    if (stopping_) return;
                    const auto generation = requested_;
                    const int rows = rows_, columns = columns_;
                    lock.unlock(); session_.resize(static_cast<short>(columns), static_cast<short>(rows));
                    lock.lock(); completed_ = generation;
                }
            } catch (...) { std::lock_guard<std::mutex> lock(mutex_); error_ = std::current_exception(); }
        });
    }
    void request(int rows, int columns) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        rows_ = rows; columns_ = columns; ++requested_; ready_.notify_one();
    }
    bool settled() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        return requested_ == completed_;
    }
    void stop() noexcept {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; ready_.notify_one(); }
        if (thread_.joinable()) thread_.join();
    }
};
