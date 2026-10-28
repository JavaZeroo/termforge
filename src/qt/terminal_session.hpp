// GUI-owned terminal state with bounded, worker-owned ConPTY transport.
#pragma once

#include "../../experiments/core/terminal.hpp"
#include "../../experiments/shell/queues.hpp"
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace termforge::qt {

// One model owns one session. Only its Qt thread reads or modifies
// Parser/Screen; the reader publishes bounded byte chunks, never screen state.
class SessionModel final : public QObject {
public:
    static constexpr int initial_rows = 24;
    static constexpr int initial_columns = 80;
    struct ResizeState {
        int requested_columns, requested_rows;
        int transport_columns, transport_rows;
        unsigned long long requested_revision, transport_revision;
        bool accepting, pending;
        DWORD child_pid;
    };
    std::function<void()> on_changed;

private:
    Parser parser_{initial_rows, initial_columns, 100, ParseMode::recovery};
    OutputQueue output_;
    std::unique_ptr<tf_conpty::Session> session_;
    std::unique_ptr<InputQueue> input_;
    QTimer timer_;
    std::thread lifecycle_;
    std::atomic<bool> close_requested_{false};
    std::atomic<bool> cleanup_done_{false};
    struct ResizeRequest { short columns, rows; unsigned long long revision; };
    mutable std::mutex control_mutex_;
    std::optional<ResizeRequest> pending_resize_;
    unsigned long long requested_revision_ = 0, transport_revision_ = 0;
    COORD transport_size_{};
    bool accepting_resize_ = false;
    struct Completion {
        DWORD exit_code = STILL_ACTIVE;
        DWORD active_processes = MAXDWORD;
        unsigned long long output_bytes = 0;
        bool cleanup_verified = false;
        bool natural_exit = false;
        bool output_eof = false;
        std::string error;
    } completion_;
    bool started_ = false;
    bool finished_ = false;
    bool parser_failed_ = false;
    bool natural_exit_ = false;
    bool cleanup_verified_ = false;
    DWORD exit_code_ = STILL_ACTIVE;
    QString error_;

    void notify() { if (on_changed) on_changed(); }
    void remember_error(const QString& error) {
        if (error_.isEmpty()) error_ = error;
    }
    // Only this worker calls resize/wait/cancel/close after startup. The latest
    // bounded request is taken without holding a lock over a native API call.
    // During natural close the Qt timer keeps draining the queue, allowing the
    // ConPTY reader to make progress while ClosePseudoConsole waits for EOF.
    void supervise(bool startup_succeeded) noexcept {
        bool child_exited = false;
        try {
            if (startup_succeeded) {
                while (!close_requested_.load(std::memory_order_acquire)) {
                    if (session_->wait_exit(20)) { child_exited = true; break; }
                    std::optional<ResizeRequest> request;
                    {
                        std::lock_guard<std::mutex> lock(control_mutex_);
                        if (accepting_resize_ && !close_requested_.load(std::memory_order_acquire))
                            request = std::exchange(pending_resize_, std::nullopt);
                    }
                    if (request) {
                        // A successful call is transport progress, never proof
                        // that the child has observed its new console geometry.
                        session_->resize(request->columns, request->rows);
                        std::lock_guard<std::mutex> lock(control_mutex_);
                        transport_size_ = {request->columns, request->rows};
                        transport_revision_ = request->revision;
                    }
                }
            }
        } catch (const std::exception& error) {
            completion_.error = error.what();
        } catch (...) {
            completion_.error = "Unknown session supervision failure";
        }

        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            accepting_resize_ = false;
            pending_resize_.reset();
        }

        const bool cancelled = close_requested_.load(std::memory_order_acquire) || !child_exited;
        if (cancelled) {
            output_.stop(); // Release a reader waiting for room before closing.
            if (session_ && session_->pid) {
                try { session_->cancel(); }
                catch (const std::exception& error) {
                    if (completion_.error.empty()) completion_.error = error.what();
                } catch (...) {
                    if (completion_.error.empty()) completion_.error = "Session cancellation failed";
                }
            }
        }
        if (input_) input_->stop();
        if (session_) {
            session_->close();
            completion_.exit_code = session_->exit_code;
            completion_.active_processes = session_->active_after;
            completion_.output_bytes = session_->output_bytes;
            completion_.cleanup_verified = session_->cleanup_complete;
            completion_.output_eof = session_->read_error == ERROR_BROKEN_PIPE || session_->read_error == ERROR_SUCCESS;
            completion_.natural_exit = child_exited && !cancelled && !session_->forced_process_cancel &&
                !session_->forced_reader_cancel && completion_.output_eof;
            try { session_->check_reader(); }
            catch (const std::exception& error) {
                if (completion_.error.empty()) completion_.error = error.what();
            } catch (...) {
                if (completion_.error.empty()) completion_.error = "Session output reader failed";
            }
        } else {
            completion_.active_processes = 0;
            completion_.cleanup_verified = true;
        }
        cleanup_done_.store(true, std::memory_order_release);
    }
    void poll() {
        if (finished_) return;
        bool changed = false;
        const auto bytes = output_.take(32 * 1024);
        if (!bytes.empty()) {
            changed = true;
            if (!parser_failed_) {
                try { parser_.feed(bytes); }
                catch (const std::exception& error) {
                    parser_failed_ = true;
                    remember_error(QString::fromUtf8(error.what()));
                    close();
                }
            }
        }
        if (cleanup_done_.load(std::memory_order_acquire) && !output_.snapshot().queued_bytes) {
            if (!parser_failed_) {
                try { parser_.finish(); }
                catch (const std::exception& error) {
                    parser_failed_ = true;
                    remember_error(QString::fromUtf8(error.what()));
                }
            }
            if (!completion_.error.empty()) remember_error(QString::fromUtf8(completion_.error.c_str()));
            exit_code_ = completion_.exit_code;
            cleanup_verified_ = completion_.cleanup_verified;
            natural_exit_ = completion_.natural_exit && !close_requested_.load(std::memory_order_acquire) &&
                !parser_failed_ && completion_.error.empty();
            if (!cleanup_verified_) remember_error(QStringLiteral("Session cleanup could not be verified"));
            finished_ = true;
            timer_.stop();
            if (lifecycle_.joinable()) lifecycle_.join();
            changed = true;
        }
        if (changed) notify();
    }

public:
    explicit SessionModel(QObject* parent = nullptr) : QObject(parent) {
        timer_.setInterval(10);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
    }
    ~SessionModel() override {
        on_changed = {};
        timer_.stop();
        close_requested_.store(true, std::memory_order_release);
        // QApplication may already have stopped dispatching timers. Abandoning
        // this view must still release queue backpressure before worker join.
        output_.stop();
        if (lifecycle_.joinable()) lifecycle_.join();
    }
    bool start(const std::wstring& executable, const std::wstring& arguments,
               const std::wstring& working_directory = {}) {
        if (started_) {
            remember_error(QStringLiteral("This terminal session has already been started"));
            notify(); return false;
        }
        started_ = true;
        bool succeeded = false;
        try {
            session_ = std::make_unique<tf_conpty::Session>(tf_conpty::Backend::configured());
            if (std::string_view(session_->backend_identity()) == "system")
                throw std::runtime_error("The Qt product entry requires the fixed bundled ConPTY backend");
            input_ = std::make_unique<InputQueue>(*session_);
            session_->start(executable, arguments, [this](std::string_view bytes) { output_.push(bytes); },
                            working_directory, {static_cast<short>(parser_.screen.width()),
                                                static_cast<short>(parser_.screen.height())});
            input_->start();
            {
                std::lock_guard<std::mutex> lock(control_mutex_);
                transport_size_ = {static_cast<short>(parser_.screen.width()),
                                   static_cast<short>(parser_.screen.height())};
                transport_revision_ = requested_revision_;
                accepting_resize_ = true;
            }
            succeeded = true;
        } catch (const std::exception& error) {
            remember_error(QString::fromUtf8(error.what()));
            close_requested_.store(true, std::memory_order_release);
            output_.stop();
        }
        lifecycle_ = std::thread([this, succeeded] { supervise(succeeded); });
        timer_.start();
        notify();
        return succeeded;
    }
    bool send(const std::string& bytes) {
        if (!running() || !input_) return false;
        try {
            if (input_->send(bytes)) return true;
            remember_error(QStringLiteral("Terminal input queue is full or unavailable"));
        } catch (const std::exception& error) {
            remember_error(QString::fromUtf8(error.what()));
            close();
        }
        notify(); return false;
    }
    bool send(const QByteArray& bytes) {
        return send(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
    }
    bool resize(int columns, int rows) {
        if (finished_ || close_requested_.load(std::memory_order_acquire)) return false;
        if (columns < 1 || columns > 500 || rows < 1 || rows > 200) {
            remember_error(QStringLiteral("Terminal grid must be within 1..500 columns and 1..200 rows"));
            notify(); return false;
        }
        try {
            std::lock_guard<std::mutex> lock(control_mutex_);
            if (started_ && !accepting_resize_) return false;
            if (columns == parser_.screen.width() && rows == parser_.screen.height()) return true;
            parser_.screen.resize(rows, columns);
            ++requested_revision_;
            if (started_)
                pending_resize_ = ResizeRequest{static_cast<short>(columns), static_cast<short>(rows), requested_revision_};
        }
        catch (const std::exception& error) {
            remember_error(QString::fromUtf8(error.what()));
            close(); notify(); return false;
        }
        notify(); return true;
    }
    void close() noexcept {
        close_requested_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(control_mutex_);
        accepting_resize_ = false;
        pending_resize_.reset();
    }
    // This diagnostic separates the GUI request/core from native call completion.
    // Real child acknowledgement must come from the child, outside this state.
    ResizeState resize_state() const {
        std::lock_guard<std::mutex> lock(control_mutex_);
        const bool accepting = !finished_ && !close_requested_.load(std::memory_order_acquire) &&
            (!started_ || accepting_resize_);
        return {parser_.screen.width(), parser_.screen.height(), transport_size_.X, transport_size_.Y,
                requested_revision_, transport_revision_, accepting,
                started_ && accepting && requested_revision_ != transport_revision_, session_ ? session_->pid : 0};
    }
    const Screen& screen() const { return parser_.screen; }
    bool running() const { return started_ && !finished_ && !close_requested_.load(std::memory_order_acquire); }
    bool finished() const { return finished_; }
    bool natural_exit() const { return finished_ && natural_exit_; }
    bool cleanup_verified() const { return finished_ && cleanup_verified_; }
    DWORD exit_code() const { return exit_code_; }
    DWORD active_processes_after() const { return finished_ ? completion_.active_processes : MAXDWORD; }
    unsigned long long received_bytes() const { return finished_ ? completion_.output_bytes : 0; }
    bool output_eof() const { return finished_ && completion_.output_eof; }
    const QString& error() const { return error_; }
    QString configuration_identity() const {
        return QStringLiteral("cpp-core / qt-widgets / ") +
            QString::fromLatin1(session_ ? session_->backend_identity() : "bundled-conpty-not-loaded") +
            QStringLiteral(" / %1x%2").arg(parser_.screen.width()).arg(parser_.screen.height());
    }
};

} // namespace termforge::qt
