// Disposable-hosted clipboard owners and evidence for the product read path.
#pragma once
#include "clipboard.hpp"
#include "paste_scenario_state.hpp"
#include "sent_message_wait.hpp"
#include "clipboard_owner_policy.hpp"
#include <atomic>
#include <cstdlib>
#include <optional>

namespace tf_shell_test {
inline void require_hosted_clipboard() {
    const auto matches=[](const wchar_t* name,const wchar_t* expected) {
        wchar_t value[128]{}; const auto count=GetEnvironmentVariableW(name,value,128);
        return count && count<128 && std::wstring(value)==expected;
    };
    paste_check(matches(L"GITHUB_ACTIONS",L"true") && matches(L"RUNNER_ENVIRONMENT",L"github-hosted"),
                "system clipboard scenario requires a disposable GitHub-hosted VM");
}
inline bool clipboard_event(HANDLE event) {
    const auto status=WaitForSingleObject(event,0);
    paste_check(status==WAIT_OBJECT_0 || status==WAIT_TIMEOUT,"clipboard event query failed");
    return status==WAIT_OBJECT_0;
}

// The fixture worker alone owns its HWND and clipboard. Readiness and release
// are events, not sleeps. The UI remains free while WM_RENDERFORMAT is held.
class ClipboardScenarioOwner {
    tf_conpty::Handle ready_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    tf_conpty::Handle entered_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    tf_conpty::Handle release_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    tf_conpty::Handle released_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::atomic<DWORD> error_{0};
    std::atomic<DWORD> native_error_{0};
    std::atomic<DWORD> registration_error_{0};
    std::atomic<const char*> step_{"thread-not-started"};
    std::atomic<const char*> failure_step_{nullptr};
    std::atomic<DWORD> secondary_error_{0}, secondary_native_error_{0};
    std::atomic<const char*> secondary_failure_step_{nullptr};
    std::atomic<bool> publication_attempted_{false}, publication_succeeded_{false};
    std::atomic<HWND> window_{nullptr};
    bool delayed_;
    bool thread_exited_ = false;
    ClipboardOwnerPolicy policy_;
    std::thread thread_;
    void step(const char* name) noexcept { step_=name; SetLastError(ERROR_SUCCESS); }
    void record_failure(const char* name,DWORD native_error,DWORD fallback=ERROR_GEN_FAILURE) noexcept {
        // Only the fixture worker writes failures. Publish error_ last so the
        // observer sees the original call and error before teardown APIs run.
        if(error_) {
            if(!secondary_error_) {
                secondary_failure_step_=name; secondary_native_error_=native_error;
                secondary_error_=native_error?native_error:fallback;
            }
            return;
        }
        failure_step_=name; native_error_=native_error;
        error_=native_error?native_error:fallback;
    }
    void check(bool passed) {
        if(passed) return;
        const DWORD native_error=GetLastError();
        record_failure(step_.load(),native_error);
        throw std::runtime_error("clipboard owner native step failed");
    }
    std::string ready_failure(DWORD wait_status) const {
        return std::string("clipboard owner failed its bounded ready handshake: mode=")+
            (delayed_?"delayed":"occupied")+" step="+step_.load()+
            " win32_error="+std::to_string(native_error_.load())+
            " worker_error="+std::to_string(error_.load())+
            " registration_last_error="+std::to_string(registration_error_.load())+
            " wait_status="+std::to_string(wait_status);
    }
    std::string worker_failure() const {
        const auto failed=failure_step_.load();
        return std::string("clipboard owner worker failed: mode=")+(delayed_?"delayed":"occupied")+
            " step="+(failed?failed:step_.load())+
            " win32_error="+std::to_string(native_error_.load())+
            " worker_error="+std::to_string(error_.load())+
            " secondary_step="+(secondary_failure_step_.load()?secondary_failure_step_.load():"none")+
            " secondary_native_error="+std::to_string(secondary_native_error_.load())+
            " secondary_worker_error="+std::to_string(secondary_error_.load());
    }
    ClipboardOwnerObservation observation() const {
        ClipboardOwnerObservation result;
        result.release_event=clipboard_event(released_.get()); result.thread_exited=thread_exited_;
        result.publication_attempted=publication_attempted_; result.publication_succeeded=publication_succeeded_;
        // error_ and secondary_error_ publish the remaining error fields last;
        // do not inspect partially written details before that publication.
        result.worker_error=error_.load();
        if(result.worker_error) {
            result.native_error=native_error_.load();
            if(const auto name=failure_step_.load()) result.failure_step=name;
        }
        result.secondary_worker_error=secondary_error_.load();
        if(result.secondary_worker_error) {
            result.secondary_native_error=secondary_native_error_.load();
            if(const auto name=secondary_failure_step_.load()) result.secondary_failure_step=name;
        }
        return result;
    }
    void reject_worker() const {
        const auto detail=worker_failure();
        std::cerr<<"[TF-CLIPBOARD-OWNER] "<<detail<<std::endl;
        throw std::runtime_error(detail);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* owner=reinterpret_cast<ClipboardScenarioOwner*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            owner=static_cast<ClipboardScenarioOwner*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));
        }
        if(message==WM_RENDERFORMAT && owner) {
            // No exceptions may escape this WNDPROC. Capture native errors
            // immediately, before GlobalFree/SetEvent can replace LastError.
            owner->step("SetEvent-render-entered");
            if(!SetEvent(owner->entered_.get())) owner->record_failure("SetEvent-render-entered",GetLastError());
            owner->step("WaitForSingleObject-render-release");
            const DWORD wait_status=WaitForSingleObject(owner->release_.get(),12000);
            if(wait_status!=WAIT_OBJECT_0) {
                const DWORD native_error=wait_status==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
                owner->record_failure(wait_status==WAIT_TIMEOUT?"WaitForSingleObject-render-release-timeout":
                    "WaitForSingleObject-render-release-failed",native_error,
                    wait_status==WAIT_TIMEOUT?WAIT_TIMEOUT:ERROR_GEN_FAILURE);
            }
            else {
                constexpr wchar_t late[]=L"LATE_MUST_NOT_SEND";
                owner->step("GlobalAlloc-render");
                HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,sizeof(late));
                if(!memory) owner->record_failure("GlobalAlloc-render",GetLastError(),ERROR_NOT_ENOUGH_MEMORY);
                else {
                    owner->step("GlobalLock-render");
                    void* bytes=GlobalLock(memory);
                    if(!bytes) {
                        owner->record_failure("GlobalLock-render",GetLastError(),ERROR_NOT_ENOUGH_MEMORY);
                        GlobalFree(memory);
                    } else {
                        std::memcpy(bytes,late,sizeof(late));
                        owner->step("GlobalUnlock-render");
                        GlobalUnlock(memory);
                        // WM_RENDERFORMAT must supply data without OpenClipboard.
                        owner->step("SetClipboardData-render");
                        owner->publication_attempted_=true;
                        if(!SetClipboardData(CF_UNICODETEXT,memory)) {
                            owner->record_failure("SetClipboardData-render",GetLastError());
                            GlobalFree(memory);
                        } else owner->publication_succeeded_=true;
                    }
                }
            }
            owner->step("SetEvent-render-released");
            if(!SetEvent(owner->released_.get())) owner->record_failure("SetEvent-render-released",GetLastError());
            return 0;
        }
        if(message==WM_CLOSE) {
            if(owner) owner->step("DestroyWindow-close");
            if(!DestroyWindow(window) && owner) owner->record_failure("DestroyWindow-close",GetLastError());
            return 0;
        }
        if(message==WM_DESTROY) { if(owner) owner->window_=nullptr; PostQuitMessage(0); return 0; }
        return DefWindowProcW(window,message,wp,lp);
    }
    void stop() noexcept {
        if(!thread_.joinable()) return;
        SetEvent(release_.get());
        if(window_) PostMessageW(window_,WM_CLOSE,0,0);
        // Never detach a fixture thread that still refers to this owner. On a
        // native teardown failure the outer test Job reclaims this process.
        if(WaitForSingleObject(thread_.native_handle(),3000)!=WAIT_OBJECT_0) {
            std::cerr<<"[TF-CLIPBOARD-OWNER] teardown deadline: step="<<step_.load()
                     <<" win32_error="<<native_error_.load()<<" worker_error="<<error_.load()<<std::endl;
            std::_Exit(92);
        }
        try { thread_.join(); thread_exited_=true; } catch(...) { std::_Exit(92); }
    }
public:
    explicit ClipboardScenarioOwner(bool delayed):delayed_(delayed),policy_(delayed) {
        static_assert(ERROR_CLIPBOARD_NOT_OPEN==1418,"fixture policy uses the Windows clipboard error identity");
        require_hosted_clipboard();
        paste_check(ready_.get() && entered_.get() && release_.get() && released_.get(),"clipboard fixture events failed");
        const HDESK desktop=GetThreadDesktop(GetCurrentThreadId());
        thread_=std::thread([this,desktop] {
            bool opened=false;
            try {
                step("SetThreadDesktop"); check(SetThreadDesktop(desktop)!=FALSE);
                WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr);
                cls.lpszClassName=L"TermForgeProductClipboardOwner";
                step("RegisterClassW"); check(RegisterClassW(&cls)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
                step("CreateWindowExW");
                window_=CreateWindowExW(0,cls.lpszClassName,L"Synthetic product clipboard owner",0,
                    0,0,8,8,nullptr,nullptr,cls.hInstance,this);
                check(window_!=nullptr);
                step("OpenClipboard"); check(OpenClipboard(window_)!=FALSE); opened=true;
                if(delayed_) {
                    step("EmptyClipboard"); check(EmptyClipboard()!=FALSE);
                    step("SetClipboardData-delayed");
                    SetClipboardData(CF_UNICODETEXT,nullptr);
                    registration_error_=GetLastError();
                    step("IsClipboardFormatAvailable"); check(IsClipboardFormatAvailable(CF_UNICODETEXT)!=FALSE);
                    step("CloseClipboard-ready"); check(CloseClipboard()!=FALSE); opened=false;
                } else { step("GetOpenClipboardWindow"); check(GetOpenClipboardWindow()==window_); }
                step("SetEvent-ready"); check(SetEvent(ready_.get())!=FALSE);
                if(!delayed_) {
                    step("Wait-release");
                    paste_check(WaitForSingleObject(release_.get(),12000)==WAIT_OBJECT_0,"clipboard contention release timeout");
                    step("CloseClipboard-release"); check(CloseClipboard()!=FALSE); opened=false;
                    SetEvent(released_.get());
                }
                MSG message{};
                while(true) {
                    step("GetMessageW");
                    const BOOL status=GetMessageW(&message,nullptr,0,0);
                    if(status<=0) { check(status==0); break; }
                    DispatchMessageW(&message);
                }
            } catch(...) {
                if(!error_) record_failure(step_.load(),GetLastError());
                if(opened) {
                    step("CloseClipboard-cleanup");
                    if(!CloseClipboard()) record_failure("CloseClipboard-cleanup",GetLastError());
                }
                if(window_ && IsWindow(window_)) {
                    step("DestroyWindow-cleanup");
                    if(!DestroyWindow(window_)) record_failure("DestroyWindow-cleanup",GetLastError());
                }
                SetEvent(ready_.get()); SetEvent(released_.get());
            }
        });
        const DWORD ready_status=wait_event_with_sent_messages(ready_.get(),3000);
        if(ready_status!=WAIT_OBJECT_0 || error_) {
            const auto detail=ready_failure(ready_status);
            // Preserve the original blocked/failed call even if bounded stop
            // must terminate the fixture process before an exception escapes.
            std::cerr<<"[TF-CLIPBOARD-OWNER] "<<detail<<std::endl;
            stop(); throw std::runtime_error(detail);
        }
    }
    ~ClipboardScenarioOwner() { stop(); }
    bool entered() const { return clipboard_event(entered_.get()); }
    // [DEBUG-r99] Observation only: manual-reset event, fixed step names and
    // independent atomics. This is not a coherent acceptance snapshot.
    void debug_record_r99(std::ostream& out) const {
        out<<",\"owner_step\":\""<<step_.load()<<"\",\"owner_released_wait\":"
           <<WaitForSingleObject(released_.get(),0)<<",\"owner_worker_error\":"<<error_.load()
           <<",\"owner_native_error\":"<<native_error_.load();
    }
    bool released() const {
        // The individual atomics do not form a coherent publication tuple
        // while WM_RENDERFORMAT is still writing it. Its release event is the
        // handshake; finish() additionally joins before the final strict check.
        if(!clipboard_event(released_.get())) return false;
        const auto value=observation();
        if(policy_.decide(value)==ClipboardOwnerDecision::rejected) reject_worker();
        return value.release_event;
    }
    bool occupied() const { return !delayed_ && GetOpenClipboardWindow()==window_; }
    void release() {
        policy_.request_ordinary();
        paste_check(SetEvent(release_.get())!=FALSE,"clipboard owner release failed");
    }
    void release_after_requester_exit(HANDLE requester,ClipboardReleaseReason reason,ClipboardReleaseProof proof) {
        proof.owner_entered=entered();
        proof.requester_exited=requester && clipboard_event(requester);
        policy_.request_after_exit(reason,proof);
        paste_check(SetEvent(release_.get())!=FALSE,"clipboard owner requester-exit release failed");
    }
    ClipboardOwnerReleaseResult finish() {
        stop();
        const auto value=observation();
        const auto decision=policy_.decide(value);
        if(decision!=ClipboardOwnerDecision::accepted && decision!=ClipboardOwnerDecision::expected_requester_gone)
            reject_worker();
        return {policy_.reason(),policy_.proof(),value,decision==ClipboardOwnerDecision::expected_requester_gone};
    }
};

inline void write_oversized_clipboard(HWND window) {
    require_hosted_clipboard();
    // Independently authored real allocation exceeds the product hard limit,
    // even allowing allocator rounding. No unbounded input or shared data.
    constexpr size_t units=1024*1024+64;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(units+1)*sizeof(char16_t));
    paste_check(memory!=nullptr,"oversize fixture allocation failed");
    struct Allocation { HGLOBAL memory; ~Allocation() { if(memory) GlobalFree(memory); } } owned{memory};
    auto* text=static_cast<char16_t*>(GlobalLock(memory));
    paste_check(text!=nullptr,"oversize fixture lock failed");
    std::fill_n(text,units,u'X'); text[units]=0; GlobalUnlock(memory);
    tf_shell::ClipboardAccess access(window);
    paste_check(EmptyClipboard()!=FALSE && SetClipboardData(CF_UNICODETEXT,memory)!=nullptr,"oversize fixture publish failed");
    owned.memory=nullptr;
}

struct ClipboardWindowCase {
    std::string name;
    DWORD helper_pid=0;
    bool owner_entered=false, prompt_seen=false, transfer_seen=false;
    bool other_during=false, other_after=false, error_visible=false;
    bool helper_exited=false, busy_cleared=false, target_closed=false, retry_verified=false;
    std::optional<ClipboardOwnerReleaseResult> owner_release;
    size_t receiver_bytes=0;
};
// [DEBUG-r99] Bounded, opt-in fixture trace. No clipboard or terminal content,
// process/window identity, waits, or changes to the scenario's decisions.
class ClipboardStageTraceR99 {
    bool checked_=false, enabled_=false;
    unsigned records_=0, turns_=0, previous_case_=~0u;
    int previous_stage_=-1;
    uint64_t last_ms_=0;
public:
    bool enabled() {
        if(!checked_) {
            wchar_t value[2]{};
            enabled_=GetEnvironmentVariableW(L"TERMFORGE_R99_STAGE_TRACE",value,2)==1 && value[0]==L'1';
            if(enabled_) require_hosted_clipboard();
            checked_=true;
        }
        return enabled_;
    }
    void turn() { ++turns_; }
    void record(const std::filesystem::path& directory,unsigned index,int stage,uint64_t elapsed,
                const char* event,std::optional<bool> busy,HANDLE helper,const ClipboardScenarioOwner* owner) {
        if(records_>=256) return;
        const bool changed=previous_stage_!=stage || previous_case_!=index;
        if(!changed && std::string(event)=="tick" && elapsed-last_ms_<1000) return;
        std::ofstream out(directory/"clipboard-stage-trace.jsonl",std::ios::binary|
            (records_?std::ios::app:std::ios::trunc));
        out<<"{\"schema\":\"termforge-clipboard-stage-r99-v1\",\"tag\":\"[DEBUG-r99]\",\"record\":"<<records_
           <<",\"turn\":"<<turns_<<",\"elapsed_ms\":"<<elapsed<<",\"case\":"<<index
           <<",\"stage\":"<<stage<<",\"event\":\""<<event<<"\",\"paste_busy\":";
        if(busy) out<<(*busy?"true":"false"); else out<<"null";
        out<<",\"helper_wait\":";
        if(helper) out<<WaitForSingleObject(helper,0); else out<<"null";
        if(owner) owner->debug_record_r99(out);
        else out<<",\"owner_step\":null,\"owner_released_wait\":null,\"owner_worker_error\":null,\"owner_native_error\":null";
        out<<",\"truncated\":"<<(records_==255?"true":"false")<<"}\n";
        out.close(); paste_check(out.good(),"[DEBUG-r99] clipboard stage trace write failed");
        ++records_; previous_case_=index; previous_stage_=stage; last_ms_=elapsed;
    }
};
struct ClipboardScenarioState {
    unsigned index=0;
    std::unique_ptr<ClipboardScenarioOwner> owner;
    std::unique_ptr<PasteReceiverGate> receiver;
    tf_conpty::Handle helper;
    HWND target=nullptr;
    ClipboardWindowCase pending;
    std::vector<ClipboardWindowCase> cases;
    ClipboardStageTraceR99 debug_trace_r99;
    const char* name() const { return index==0?"occupied":index==1?"oversize":index==2?"cancel":"close"; }
    std::string retry() const { return std::string("RETRY_")+name(); }
    std::string baseline() const { return std::string(256,'B'); }
    std::string marker(bool after) const { return std::string(after?"TF_CLIP_AFTER_":"TF_CLIP_DURING_")+name(); }
    std::filesystem::path capture(const std::filesystem::path& directory) const { return directory/(std::string(name())+".bin"); }
    bool helper_exited() const { return helper.get() && clipboard_event(helper.get()); }
    void save(const std::filesystem::path& path) const {
        paste_check(cases.size()==4,"clipboard window cases incomplete");
        std::ofstream out(path,std::ios::binary);
        out<<"{\"schema\":\"termforge-window-clipboard-v1\",\"disposable_hosted\":true,\"cases\":[";
        for(size_t i=0;i<cases.size();++i) {
            if(i) out<<',';
            const auto& row=cases[i];
            out<<"{\"name\":\""<<row.name<<"\",\"action\":\"product-paste-command\",\"baseline_bytes\":256"
               <<",\"helper_pid\":"<<row.helper_pid<<",\"receiver_bytes\":"<<row.receiver_bytes;
            const auto flag=[&](const char* name,bool value) { out<<",\""<<name<<"\":"<<(value?"true":"false"); };
            flag("owner_entered",row.owner_entered); flag("prompt_seen",row.prompt_seen); flag("transfer_seen",row.transfer_seen);
            flag("other_during",row.other_during); flag("other_after",row.other_after); flag("error_visible",row.error_visible);
            flag("helper_exited",row.helper_exited); flag("busy_cleared",row.busy_cleared); flag("target_closed",row.target_closed);
            flag("retry_verified",row.retry_verified);
            out<<",\"owner_release\":";
            if(!row.owner_release) out<<"null";
            else {
                const auto& release=*row.owner_release; const auto& value=release.observation;
                const char* reason=release.reason==ClipboardReleaseReason::ordinary?"ordinary":
                    release.reason==ClipboardReleaseReason::requester_cancelled?"requester-cancelled":"target-closed";
                out<<"{\"reason\":\""<<reason<<"\",\"proof\":{\"owner_entered\":"<<(release.proof.owner_entered?"true":"false")
                   <<",\"requester_exited\":"<<(release.proof.requester_exited?"true":"false")
                   <<",\"target_quiescent\":"<<(release.proof.target_quiescent?"true":"false")
                   <<",\"payload_unchanged\":"<<(release.proof.payload_unchanged?"true":"false")<<'}';
                flag("publication_attempted",value.publication_attempted); flag("publication_succeeded",value.publication_succeeded);
                flag("release_event",value.release_event); flag("thread_exited",value.thread_exited);
                out<<",\"failure_step\":\""<<value.failure_step<<"\",\"native_error\":"<<value.native_error
                   <<",\"worker_error\":"<<value.worker_error<<",\"secondary_failure_step\":\""<<value.secondary_failure_step
                   <<"\",\"secondary_native_error\":"<<value.secondary_native_error<<",\"secondary_worker_error\":"<<value.secondary_worker_error;
                flag("expected_requester_gone",release.expected_requester_gone); out<<'}';
            }
            out<<'}';
        }
        out<<"]}\n"; out.close(); paste_check(out.good(),"clipboard workflow evidence write failed");
    }
};
} // namespace tf_shell_test
