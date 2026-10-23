// Data-only console capture lifecycle, independent of IME and global input.
#pragma once
#include "../conpty/session.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

namespace tf_shell_test {
inline void raw_receiver_check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
enum class RawReadDisposition { data, stopped, failed };
enum class RawReceiverStep { none, read, zero_count, capacity, unicode, write, flush, incomplete_scalar, cancel, wait };
inline const char* raw_receiver_step_name(RawReceiverStep step) {
    switch(step) {
    case RawReceiverStep::none:return "";
    case RawReceiverStep::read:return "ReadConsoleW";
    case RawReceiverStep::zero_count:return "ReadConsoleW-zero-count";
    case RawReceiverStep::capacity:return "UTF16-capacity";
    case RawReceiverStep::unicode:return "UTF8-conversion";
    case RawReceiverStep::write:return "capture-write";
    case RawReceiverStep::flush:return "capture-flush";
    case RawReceiverStep::incomplete_scalar:return "incomplete-surrogate";
    case RawReceiverStep::cancel:return "CancelSynchronousIo";
    case RawReceiverStep::wait:return "worker-wait";
    }
    return "unknown";
}
inline RawReadDisposition raw_read_disposition(bool stop_requested,bool success,DWORD count,DWORD error) {
    if(!success) return stop_requested && error==ERROR_OPERATION_ABORTED?RawReadDisposition::stopped:RawReadDisposition::failed;
    // CancelSynchronousIo can complete an outstanding ReadConsoleW with TRUE
    // and zero units. Only our explicit stop request makes that terminal.
    if(!count) return stop_requested?RawReadDisposition::stopped:RawReadDisposition::failed;
    return RawReadDisposition::data;
}
inline bool raw_cancel_request_valid(bool success,DWORD error) { return success || error==ERROR_NOT_FOUND; }
struct RawCancelFailure {
    bool observed=false;
    DWORD error=0;
    void observe(bool success,DWORD native_error) {
        if(!observed && !raw_cancel_request_valid(success,native_error)) { observed=true; error=native_error; }
    }
    DWORD after_join(DWORD worker_error) const {
        if(worker_error) return worker_error;
        // Preserve a failed native call reporting zero in `error`; use an
        // explicit failure fallback rather than treating zero as no failure.
        return observed?(error?error:ERROR_GEN_FAILURE):ERROR_SUCCESS;
    }
};
struct RawReceiverStatus {
    bool stop_requested=false,worker_joined=false,last_read_success=false,zero_read_accepted=false;
    DWORD last_read_count=0,last_read_error=0,worker_error=0,last_cancel_error=0;
    unsigned reads_entered=0,reads_completed=0,cancel_calls=0,cancel_successes=0;
    size_t received_units=0,saved_bytes=0;
    const char* failure_step="";
    DWORD unexpected_cancel_error=0,last_wait_result=WAIT_TIMEOUT,last_wait_error=0;
    bool unexpected_cancel_observed=false;
};
class RawConsoleReceiver {
    static constexpr size_t maximum_units=4096;
    tf_conpty::Handle file_;
    std::filesystem::path diagnostic_path_;
    std::atomic<DWORD> error_{0},last_count_{0},last_read_error_{0};
    std::atomic<unsigned> entered_{0},completed_{0};
    std::atomic<bool> stop_{false},last_success_{false},zero_accepted_{false};
    std::atomic<size_t> received_units_{0},saved_bytes_{0};
    std::atomic<RawReceiverStep> failure_step_{RawReceiverStep::none};
    unsigned cancel_calls_=0,cancel_successes_=0;
    DWORD last_cancel_error_=0;
    RawCancelFailure cancel_failure_;
    DWORD last_wait_result_=WAIT_TIMEOUT,last_wait_error_=0;
    bool joined_=false,finalized_=false;
    std::thread thread_;
    static std::string encode(const std::wstring& units) {
        if(units.empty()) return {};
        const int count=static_cast<int>(units.size());
        const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,units.data(),count,nullptr,0,nullptr,nullptr);
        raw_receiver_check(size>0,"raw receiver invalid UTF-16"); std::string bytes(static_cast<size_t>(size),0);
        raw_receiver_check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,units.data(),count,bytes.data(),size,nullptr,nullptr)==size,"raw receiver conversion failed");
        return bytes;
    }
    void read() noexcept {
        RawReceiverStep operation=RawReceiverStep::read;
        try {
            wchar_t pending=0;
            while(!stop_) {
                wchar_t units[256]{}; DWORD count=0; ++entered_;
                const BOOL success=ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE),units,256,&count,nullptr);
                const DWORD error=success?ERROR_SUCCESS:GetLastError();
                last_success_=success!=FALSE; last_count_=count; last_read_error_=error; ++completed_;
                const auto decision=raw_read_disposition(stop_,success!=FALSE,count,error);
                if(decision==RawReadDisposition::stopped) { zero_accepted_=success && !count; break; }
                if(decision==RawReadDisposition::failed) { failure_step_=success?RawReceiverStep::zero_count:RawReceiverStep::read; error_=success?ERROR_INVALID_DATA:(error?error:ERROR_READ_FAULT); break; }
                operation=RawReceiverStep::capacity;
                raw_receiver_check(count<=maximum_units-received_units_.load(),"raw receiver exceeds UTF-16 bound"); received_units_+=count;
                std::wstring complete; if(pending) complete+=pending; complete.append(units,count); pending=0;
                if(!complete.empty() && complete.back()>=0xd800 && complete.back()<=0xdbff) { pending=complete.back(); complete.pop_back(); }
                if(!complete.empty()) {
                    operation=RawReceiverStep::unicode; const auto bytes=encode(complete); DWORD written=0;
                    const BOOL saved=WriteFile(file_.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
                    const DWORD write_error=saved?ERROR_SUCCESS:GetLastError();
                    if(!saved || written!=bytes.size()) {
                        failure_step_=RawReceiverStep::write; error_=write_error?write_error:ERROR_WRITE_FAULT; break;
                    }
                    saved_bytes_+=written;
                    if(!FlushFileBuffers(file_.get())) { const DWORD native=GetLastError(); failure_step_=RawReceiverStep::flush; error_=native?native:ERROR_WRITE_FAULT; break; }
                }
                operation=RawReceiverStep::read;
            }
            if(pending && !error_) { failure_step_=RawReceiverStep::incomplete_scalar; error_=ERROR_INVALID_DATA; }
        } catch(...) { if(!error_) { failure_step_=operation; error_=ERROR_INVALID_DATA; } }
    }
    void diagnostic(const char* state) const {
        std::ofstream out(diagnostic_path_,std::ios::binary); const auto value=status();
        out<<"{\"schema\":\"termforge-raw-console-receiver-v1\",\"state\":\""<<state<<"\",\"stop_requested\":"<<(value.stop_requested?"true":"false")
           <<",\"worker_joined\":"<<(value.worker_joined?"true":"false")<<",\"last_read_success\":"<<(value.last_read_success?"true":"false")
           <<",\"last_read_count\":"<<value.last_read_count<<",\"last_read_error\":"<<value.last_read_error<<",\"worker_error\":"<<value.worker_error
           <<",\"failure_step\":\""<<value.failure_step<<'"'
           <<",\"zero_read_accepted\":"<<(value.zero_read_accepted?"true":"false")<<",\"reads_entered\":"<<value.reads_entered<<",\"reads_completed\":"<<value.reads_completed
           <<",\"cancel_calls\":"<<value.cancel_calls<<",\"cancel_successes\":"<<value.cancel_successes<<",\"last_cancel_error\":"<<value.last_cancel_error
           <<",\"received_units\":"<<value.received_units<<",\"saved_bytes\":"<<value.saved_bytes
           <<",\"unexpected_cancel_observed\":"<<(value.unexpected_cancel_observed?"true":"false")
           <<",\"unexpected_cancel_error\":"<<value.unexpected_cancel_error<<",\"last_wait_result\":"<<value.last_wait_result<<",\"last_wait_error\":"<<value.last_wait_error<<"}\n";
        out.close(); raw_receiver_check(out.good(),"raw receiver diagnostic write failed");
    }
public:
    explicit RawConsoleReceiver(const std::filesystem::path& path):diagnostic_path_(path.wstring()+L".receiver.json") {
        DWORD mode=0; raw_receiver_check(GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&mode)!=FALSE,"raw receiver requires its own console input");
        raw_receiver_check(!std::filesystem::exists(diagnostic_path_),"raw receiver diagnostic must be new");
        file_.reset(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        raw_receiver_check(file_.get()!=INVALID_HANDLE_VALUE,"raw receiver capture must be new");
        thread_=std::thread([this] { read(); });
    }
    RawReceiverStatus status() const {
        return {stop_,joined_,last_success_,zero_accepted_,last_count_,last_read_error_,error_,last_cancel_error_,
            entered_,completed_,cancel_calls_,cancel_successes_,received_units_,saved_bytes_,raw_receiver_step_name(failure_step_),
            cancel_failure_.error,last_wait_result_,last_wait_error_,cancel_failure_.observed};
    }
    void finish() {
        if(!finalized_) {
            stop_=true;
            if(thread_.joinable()) {
                const auto deadline=GetTickCount64()+3000;
                do {
                    ++cancel_calls_; const BOOL cancelled=CancelSynchronousIo(thread_.native_handle());
                    last_cancel_error_=cancelled?ERROR_SUCCESS:GetLastError(); if(cancelled) ++cancel_successes_;
                    cancel_failure_.observe(cancelled!=FALSE,last_cancel_error_);
                    const auto state=WaitForSingleObject(thread_.native_handle(),10);
                    last_wait_error_=state==WAIT_FAILED?GetLastError():ERROR_SUCCESS; last_wait_result_=state;
                    if(state==WAIT_OBJECT_0) { thread_.join(); joined_=true; break; }
                    if(state==WAIT_FAILED || GetTickCount64()>=deadline) {
                        failure_step_=RawReceiverStep::wait;
                        try { diagnostic(state==WAIT_FAILED?"wait-failed":"timeout"); } catch(...) {}
                        std::_Exit(19);
                    }
                } while(true);
            }
            const DWORD joined_error=cancel_failure_.after_join(error_);
            if(!error_ && joined_error) { failure_step_=RawReceiverStep::cancel; error_=joined_error; }
            if(!error_ && !FlushFileBuffers(file_.get())) { const DWORD native=GetLastError(); failure_step_=RawReceiverStep::flush; error_=native?native:ERROR_WRITE_FAULT; }
            finalized_=true; diagnostic(error_?"failed":"stopped"); if(!error_) file_.reset();
        }
        raw_receiver_check(error_==0,"raw receiver console worker failed");
    }
    ~RawConsoleReceiver() { if(thread_.joinable()) { try { finish(); } catch(...) { std::_Exit(19); } } }
    RawConsoleReceiver(const RawConsoleReceiver&)=delete;
    RawConsoleReceiver& operator=(const RawConsoleReceiver&)=delete;
};
} // namespace tf_shell_test
