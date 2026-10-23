// Bounded queues: output applies backpressure; UI input rejects visibly when full.
#pragma once
#include "../conpty/session.hpp"
#include "paste.hpp"
#include <deque>
#include <memory>

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
    std::shared_ptr<tf_shell::PasteTransfer> paste_;
public:
    static constexpr size_t capacity = 16 * 1024;
    size_t high_water = 0, rejected = 0;
    explicit InputQueue(tf_conpty::Session& session) : session_(session) {}
    ~InputQueue() { stop(); }
    void start() {
        writer_ = std::thread([this] {
            std::shared_ptr<tf_shell::PasteTransfer> writing_paste;
            try {
                for (;;) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || !chunks_.empty() ||
                        (paste_ && !tf_shell::paste_terminal(paste_->progress().state)); });
                    if (stopping_) break;
                    std::string bytes;
                    writing_paste.reset();
                    if (!chunks_.empty()) {
                        bytes = std::move(chunks_.front()); chunks_.pop_front(); bytes_ -= bytes.size();
                    } else {
                        writing_paste = paste_; bytes = writing_paste->take_chunk();
                        high_water = (std::max)(high_water, bytes.size());
                    }
                    lock.unlock();
                    if (bytes.empty()) continue; // Cancelled between the predicate and take.
                    if (writing_paste) {
                        session_.send(bytes, [&](size_t written) { writing_paste->acknowledge_written(written); });
                        writing_paste->finish_chunk();
                    } else session_.send(bytes);
                }
            } catch (const std::exception& error) {
                if (writing_paste) writing_paste->fail(error.what());
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) error_ = std::current_exception();
                if (paste_) paste_->fail(error.what());
            } catch (...) {
                if (writing_paste) writing_paste->fail("unknown input writer failure");
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) error_ = std::current_exception();
                if (paste_) paste_->fail("unknown input writer failure");
            }
        });
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
    bool begin_paste(const std::shared_ptr<tf_shell::PasteTransfer>& paste) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (stopping_ || !paste || (paste_ && !tf_shell::paste_terminal(paste_->progress().state)) || !paste->claim_writer()) return false;
        paste_ = paste; ready_.notify_one(); return true;
    }
    void stop() noexcept {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; chunks_.clear(); bytes_ = 0;
          if (paste_) paste_->cancel(); ready_.notify_all(); }
        if (writer_.joinable()) {
            // Retry covers the race where cancellation runs just before WriteFile.
            while (WaitForSingleObject(writer_.native_handle(), 10) == WAIT_TIMEOUT)
                CancelSynchronousIo(writer_.native_handle());
            writer_.join();
        }
    }
};
