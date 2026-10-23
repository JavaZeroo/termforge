// Bounded clipboard helper: the UI never calls a third-party delayed renderer.
#pragma once
#include "paste.hpp"
#include "../conpty/session.hpp"
#include <array>
#include <filesystem>
#include <vector>

namespace tf_shell {
inline constexpr uint32_t clipboard_wire_magic = 0x50434654; // "TFCP", little endian
inline constexpr uint32_t clipboard_wire_version = 1;
inline constexpr size_t clipboard_wire_header = 16;
enum class ClipboardReadState { running, cancelling, completed, cancelled, timed_out, failed };
struct ClipboardReadProgress {
    ClipboardReadState state = ClipboardReadState::running;
    size_t bytes_received = 0, input_units = 0;
    DWORD win32_error = 0, active_processes = 1, process_id = 0;
    bool cleanup_complete = false;
    std::string error;
};
inline bool clipboard_read_terminal(ClipboardReadState state) {
    return state == ClipboardReadState::completed || state == ClipboardReadState::cancelled ||
           state == ClipboardReadState::timed_out || state == ClipboardReadState::failed;
}
class ClipboardReadJob {
    tf_conpty::Handle pipe_, process_, job_;
    std::vector<unsigned char> frame_;
    ClipboardReadProgress progress_;
    ClipboardReadState ending_ = ClipboardReadState::running;
    ULONGLONG deadline_ = 0;
    size_t expected_ = 0;
    bool eof_ = false, taken_ = false;
    static std::wstring object_name(HANDLE object) {
        wchar_t name[256]{}; DWORD needed = 0;
        if (!GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &needed)) tf_conpty::fail("clipboard desktop identity");
        return name;
    }
    static uint32_t field(const unsigned char* bytes) {
        return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
               (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    }
    void request_end(ClipboardReadState state, DWORD code, const char* detail) {
        if (ending_ != ClipboardReadState::running || clipboard_read_terminal(progress_.state)) return;
        ending_ = state; progress_.state = ClipboardReadState::cancelling;
        progress_.win32_error = code; progress_.error = detail;
        if (job_.get() && !TerminateJobObject(job_.get(), 97)) {
            progress_.win32_error = GetLastError(); progress_.error = "clipboard helper termination failed";
        }
        // No WaitForSingleObject with a positive timeout, thread join, or UI
        // object reference. poll() separately proves eventual reclamation.
    }
public:
    explicit ClipboardReadJob(unsigned timeout_ms = 5000) {
        if (timeout_ms < 50 || timeout_ms > 10000) throw std::invalid_argument("clipboard helper timeout outside 50..10000 ms");
        deadline_ = GetTickCount64() + timeout_ms;
        std::wstring executable(32768, 0);
        const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!count || count == executable.size()) tf_conpty::fail("clipboard host executable");
        executable.resize(count);
        executable = (std::filesystem::path(executable).parent_path() / L"shell_clipboard_reader.exe").wstring();
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        tf_conpty::Handle output, null;
        if (!CreatePipe(pipe_.put(), output.put(), &security, 4096) ||
            !SetHandleInformation(pipe_.get(), HANDLE_FLAG_INHERIT, 0)) tf_conpty::fail("clipboard helper pipe");
        null.reset(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (null.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("clipboard helper null handles");
        job_.reset(CreateJobObjectW(nullptr, nullptr));
        if (!job_.get()) tf_conpty::fail("clipboard helper Job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) tf_conpty::fail("clipboard helper Job limits");
        SIZE_T attribute_size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
        std::vector<unsigned char> storage(attribute_size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) tf_conpty::fail("clipboard helper attributes");
        struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
        const HANDLE inherited[] = {output.get(), null.get()};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<HANDLE*>(inherited),
                                       sizeof(inherited), nullptr, nullptr)) tf_conpty::fail("clipboard helper handle allowlist");
        auto desktop = object_name(GetProcessWindowStation()) + L"\\" + object_name(GetThreadDesktop(GetCurrentThreadId()));
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        startup.StartupInfo.lpDesktop = desktop.data(); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = startup.StartupInfo.hStdError = null.get(); startup.StartupInfo.hStdOutput = output.get();
        std::wstring command = L"\"" + executable + L"\" --read";
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup.StartupInfo, &info)) tf_conpty::fail("clipboard helper CreateProcess");
        process_.reset(info.hProcess); tf_conpty::Handle thread(info.hThread);
        progress_.process_id = info.dwProcessId;
        if (!AssignProcessToJobObject(job_.get(), process_.get())) {
            const DWORD error = GetLastError(); TerminateProcess(process_.get(), 98);
            tf_conpty::fail("clipboard helper Job assignment", error);
        }
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) tf_conpty::fail("clipboard helper ResumeThread");
    }
    ~ClipboardReadJob() { if (job_.get()) TerminateJobObject(job_.get(), 97); }
    ClipboardReadJob(const ClipboardReadJob&) = delete;
    ClipboardReadJob& operator=(const ClipboardReadJob&) = delete;
    ClipboardReadProgress progress() const { return progress_; }
    void cancel() { request_end(ClipboardReadState::cancelled, ERROR_CANCELLED, "clipboard read cancelled"); }
    ClipboardReadProgress poll() {
        if (clipboard_read_terminal(progress_.state)) return progress_;
        if (GetTickCount64() >= deadline_) request_end(ClipboardReadState::timed_out, WAIT_TIMEOUT, "clipboard read timed out");
        if (ending_ == ClipboardReadState::running && !eof_) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) eof_ = true;
                else request_end(ClipboardReadState::failed, GetLastError(), "clipboard pipe inspection failed");
            } else if (available) {
                constexpr size_t frame_limit = clipboard_wire_header + PastePolicy::hard_utf16_units * 2;
                const size_t count = (std::min)(static_cast<size_t>(available), static_cast<size_t>(65536));
                if (count > frame_limit - frame_.size()) request_end(ClipboardReadState::failed, ERROR_INVALID_DATA, "clipboard frame exceeds limit");
                else {
                    const size_t previous = frame_.size(); frame_.resize(previous + count); DWORD read = 0;
                    if (!ReadFile(pipe_.get(), frame_.data() + previous, static_cast<DWORD>(count), &read, nullptr) || read != count)
                        request_end(ClipboardReadState::failed, GetLastError(), "clipboard pipe read failed");
                    progress_.bytes_received = frame_.size();
                }
            }
            if (ending_ == ClipboardReadState::running && frame_.size() >= clipboard_wire_header) {
                const auto units = field(frame_.data() + 12), error = field(frame_.data() + 8);
                if (field(frame_.data()) != clipboard_wire_magic || field(frame_.data() + 4) != clipboard_wire_version ||
                    units > PastePolicy::hard_utf16_units || (error && units))
                    request_end(ClipboardReadState::failed, ERROR_INVALID_DATA, "invalid clipboard protocol header");
                else {
                    expected_ = clipboard_wire_header + static_cast<size_t>(units) * 2;
                    progress_.input_units = units;
                    if (frame_.size() > expected_) request_end(ClipboardReadState::failed, ERROR_INVALID_DATA, "trailing clipboard data");
                    else if (frame_.capacity() < expected_) frame_.reserve(expected_);
                }
            }
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
            request_end(ClipboardReadState::failed, GetLastError(), "clipboard Job inspection failed");
        else progress_.active_processes = accounting.ActiveProcesses;
        if (WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0 && !progress_.active_processes) {
            if (ending_ != ClipboardReadState::running) { progress_.state = ending_; progress_.cleanup_complete = true; }
            else if (eof_) {
                DWORD code = 0;
                if (!GetExitCodeProcess(process_.get(), &code) || code || !expected_ || frame_.size() != expected_) {
                    progress_.state = ClipboardReadState::failed; progress_.win32_error = ERROR_INVALID_DATA;
                    progress_.error = "clipboard helper exited with an incomplete frame";
                } else if (field(frame_.data() + 8)) {
                    progress_.state = ClipboardReadState::failed; progress_.win32_error = field(frame_.data() + 8);
                    progress_.error = "system clipboard read failed";
                } else progress_.state = ClipboardReadState::completed;
                progress_.cleanup_complete = true;
            }
        }
        if (clipboard_read_terminal(progress_.state) && progress_.state != ClipboardReadState::completed) std::vector<unsigned char>{}.swap(frame_);
        return progress_;
    }
    std::u16string take_text() {
        if (progress_.state != ClipboardReadState::completed || !progress_.cleanup_complete || taken_)
            throw std::logic_error("clipboard result is not available");
        std::u16string result(progress_.input_units, 0);
        for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<char16_t>(frame_[16 + i * 2] | (frame_[17 + i * 2] << 8));
        taken_ = true; std::vector<unsigned char>{}.swap(frame_); return result;
    }
};
} // namespace tf_shell
