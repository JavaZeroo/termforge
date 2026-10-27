// Original, bounded ConPTY lifecycle experiment. MIT; see repository LICENSE.
#pragma once
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <string_view>
#include <exception>
#include "backend.hpp"

namespace tf_conpty {
inline void fail(const char* operation, DWORD error = GetLastError()) {
    throw std::runtime_error(std::string(operation) + ": " + std::to_string(error));
}
class Handle {
    HANDLE value_ = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    HANDLE* put() { reset(); return &value_; }
    void reset(HANDLE value = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }
};

// Output has its own draining thread: ClosePseudoConsole must not block the
// only reader. Input calls are deliberately small in this lifecycle probe.
// The local shell supplies an external bounded streaming consumer and input
// worker; the lifecycle probe uses bounded capture and its --child entry point.
class Session {
    // Destroy this last: callbacks, reader and HPCON must stop while their
    // selected API module is still loaded.
    Backend backend_;
    Handle input_, output_, process_, job_;
    HPCON console_ = nullptr;
    std::thread reader_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::string captured_;
    bool reader_done_ = false;
    std::exception_ptr reader_failure_;
    static constexpr size_t capture_limit = 16 * 1024 * 1024;
public:
    DWORD pid = 0, exit_code = STILL_ACTIVE, read_error = 0;
    DWORD active_after = 0;
    unsigned long long output_bytes = 0;
    bool capture_overflow = false, forced_reader_cancel = false, forced_process_cancel = false;
    bool child_signaled = false, cleanup_complete = false;
    explicit Session(Backend backend = Backend::configured()) : backend_(std::move(backend)) {}
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { close(); }
    const char* backend_identity() const {
        return backend_.identity();
    }
    DWORD backend_creation_flags() const { return backend_.creation_flags(); }

    void start(const std::wstring& executable, const std::wstring& arguments = L"--child",
               std::function<void(std::string_view)> consume = {}, const std::wstring& working_directory = {},
               COORD initial_size = {80, 24}) {
        Handle child_input, child_output;
        if (!CreatePipe(child_input.put(), input_.put(), nullptr, 0)) fail("CreatePipe(input)");
        if (!CreatePipe(output_.put(), child_output.put(), nullptr, 0)) fail("CreatePipe(output)");
        const HRESULT hr = backend_.create(initial_size, child_input.get(), child_output.get(), 0, &console_);
        if (FAILED(hr)) fail("CreatePseudoConsole", static_cast<DWORD>(hr));
        child_input.reset(); child_output.reset();
        reader_ = std::thread([this, consume = std::move(consume)] {
            char bytes[65536]; DWORD count = 0;
            while (ReadFile(output_.get(), bytes, sizeof(bytes), &count, nullptr) && count) {
                if (consume) {
                    try { if (!reader_failure_) consume(std::string_view(bytes, count)); }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        reader_failure_ = std::current_exception();
                    }
                    std::lock_guard<std::mutex> lock(mutex_);
                    output_bytes += count;
                    continue;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                output_bytes += count;
                if (captured_.size() + count <= capture_limit) captured_.append(bytes, count);
                else capture_overflow = true; // Keep draining even if diagnostics are full.
                changed_.notify_all();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            read_error = GetLastError(); reader_done_ = true; changed_.notify_all();
        });

        job_.reset(CreateJobObjectW(nullptr, nullptr));
        if (!job_.get()) fail("CreateJobObject");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            fail("SetInformationJobObject");
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<unsigned char> storage(size);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) fail("InitializeProcThreadAttributeList");
        struct AttributeCleanup {
            LPPROC_THREAD_ATTRIBUTE_LIST ptr;
            ~AttributeCleanup() { DeleteProcThreadAttributeList(ptr); }
        } cleanup{attributes};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       console_, sizeof(console_), nullptr, nullptr)) fail("UpdateProcThreadAttribute");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        // Do not duplicate the host's redirected stdio into the PTY client.
        // Null standard handles let the console create its own endpoints.
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        PROCESS_INFORMATION info{};
        std::wstring command = L"\"" + executable + L"\" " + arguments;
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
                            working_directory.empty() ? nullptr : working_directory.c_str(),
                            &startup.StartupInfo, &info)) fail("CreateProcess");
        process_.reset(info.hProcess); Handle thread(info.hThread); pid = info.dwProcessId;
        if (!AssignProcessToJobObject(job_.get(), process_.get())) {
            const DWORD error = GetLastError(); TerminateProcess(process_.get(), 90);
            WaitForSingleObject(process_.get(), 5000); fail("AssignProcessToJobObject", error);
        }
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) fail("ResumeThread");
    }

    bool wait_text(const std::string& text, unsigned timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return captured_.find(text) != std::string::npos || reader_done_;
        });
        return captured_.find(text) != std::string::npos;
    }
    std::string output() const {
        std::lock_guard<std::mutex> lock(mutex_); return captured_;
    }
    void check_reader() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reader_failure_) std::rethrow_exception(reader_failure_);
    }
    void send(const std::string& bytes, const std::function<void(size_t)>& written_progress = {}) {
        // Keep this probe's blocking writes bounded to control-sized messages.
        if (bytes.empty() || bytes.size() > 1024) throw std::runtime_error("input command size invalid");
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const BOOL success = WriteFile(input_.get(), bytes.data() + offset,
                                           static_cast<DWORD>(bytes.size() - offset), &written, nullptr);
            const DWORD error = success ? ERROR_SUCCESS : GetLastError();
            if (written > bytes.size() - offset) throw std::runtime_error("invalid WriteFile(input) byte count");
            if (written && written_progress) written_progress(written);
            if (!success || !written) fail("WriteFile(input)", error ? error : ERROR_WRITE_FAULT);
            offset += written;
        }
    }
    void resize(short columns, short rows) {
        const HRESULT hr = backend_.resize(console_, {columns, rows});
        if (FAILED(hr)) fail("ResizePseudoConsole", static_cast<DWORD>(hr));
    }
    bool wait_exit(unsigned timeout_ms, DWORD* observed_wait_result = nullptr, DWORD* observed_wait_error = nullptr) {
        const auto result = WaitForSingleObject(process_.get(), timeout_ms);
        const DWORD error = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        if (observed_wait_result) *observed_wait_result = result;
        if (observed_wait_error) *observed_wait_error = error;
        child_signaled = result == WAIT_OBJECT_0;
        if (child_signaled && !GetExitCodeProcess(process_.get(), &exit_code)) fail("GetExitCodeProcess");
        return child_signaled;
    }
    void cancel() {
        if (!TerminateJobObject(job_.get(), 91)) fail("TerminateJobObject");
    }
    void close() noexcept {
        if (cleanup_complete) return;
        input_.reset();
        if (console_) { backend_.close(console_); console_ = nullptr; }
        if (process_.get()) {
            child_signaled = WaitForSingleObject(process_.get(), 5000) == WAIT_OBJECT_0;
            if (!child_signaled && job_.get()) {
                forced_process_cancel = true;
                TerminateJobObject(job_.get(), 92);
                child_signaled = WaitForSingleObject(process_.get(), 5000) == WAIT_OBJECT_0;
            }
            GetExitCodeProcess(process_.get(), &exit_code);
        }
        if (reader_.joinable()) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!changed_.wait_for(lock, std::chrono::seconds(5), [&] { return reader_done_; })) {
                forced_reader_cancel = true;
                CancelSynchronousIo(reader_.native_handle());
            }
            lock.unlock(); reader_.join();
        }
        if (job_.get()) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (!QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation,
                                               &accounting, sizeof(accounting), nullptr)) {
                    active_after = MAXDWORD; break;
                }
                active_after = accounting.ActiveProcesses;
                if (!active_after) break;
                Sleep(10);
            }
        }
        const bool no_process = !process_.get();
        output_.reset(); process_.reset(); job_.reset();
        cleanup_complete = (no_process || child_signaled) && !active_after && !forced_reader_cancel && !forced_process_cancel;
    }
};
} // namespace tf_conpty
