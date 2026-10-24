// One in-flight resize plus one coalesced request. UI keeps draining output
// while the console host processes resize. Join before closing its HPCON.
#pragma once
#include "../conpty/session.hpp"
#include <functional>
class ResizeWorker {
    tf_conpty::Session& session_;
    // Optional test synchronization before the real API. The worker still
    // performs Session::resize; a working generation means worker ownership,
    // not proof that the kernel call is blocked or a child applied the size.
    std::function<void()> before_resize_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    int rows_ = 24, columns_ = 80;
    unsigned requested_ = 0, completed_ = 0;
    unsigned working_generation_ = 0;
    bool working_ = false, stopping_ = false, started_ = false, joined_ = false;
    std::exception_ptr error_;
public:
    struct Snapshot {
        // completed means ResizePseudoConsole returned successfully, not that
        // a child observed the size, parser state changed or a frame appeared.
        unsigned requested_generation = 0, completed_generation = 0, working_generation = 0, pending_generation = 0;
        bool working = false, pending = false, stopping = false, started = false, joined = false, failed = false;
    };
    explicit ResizeWorker(tf_conpty::Session& session,std::function<void()> before_resize={})
        : session_(session),before_resize_(std::move(before_resize)) {}
    ~ResizeWorker() { stop(); }
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        // A generation difference is not a queue depth. At most one resize is
        // working and one latest request remains after coalescing. Stopped or
        // failed workers can leave uncompleted generations, never pending work.
        const bool pending = !stopping_ && !error_ && requested_ != (working_ ? working_generation_ : completed_);
        return {requested_, completed_, working_ ? working_generation_ : 0, pending ? requested_ : 0,
                working_, pending, stopping_, started_, joined_, error_ != nullptr};
    }
    void start() {
        std::lock_guard<std::mutex> starting(mutex_);
        thread_ = std::thread([this] {
            try {
                for (;;) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || requested_ != completed_; });
                    if (stopping_) return;
                    const auto generation = requested_;
                    const int rows = rows_, columns = columns_;
                    working_ = true; working_generation_ = generation;
                    lock.unlock();
                    if(before_resize_) before_resize_();
                    session_.resize(static_cast<short>(columns), static_cast<short>(rows));
                    lock.lock(); completed_ = generation; working_ = false; working_generation_ = 0;
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                working_ = false; working_generation_ = 0; error_ = std::current_exception();
            }
        });
        started_ = true;
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
        if (thread_.joinable()) {
            thread_.join();
            std::lock_guard<std::mutex> lock(mutex_); joined_ = true;
        }
    }
};
