// Disposable-hosted clipboard owners and evidence for the product read path.
#pragma once
#include "clipboard.hpp"
#include "paste_scenario_state.hpp"
#include "sent_message_wait.hpp"
#include <atomic>
#include <cstdlib>

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
    std::atomic<HWND> window_{nullptr};
    bool delayed_;
    std::thread thread_;
    void step(const char* name) noexcept { step_=name; SetLastError(ERROR_SUCCESS); }
    void record_failure(const char* name,DWORD native_error,DWORD fallback=ERROR_GEN_FAILURE) noexcept {
        // Only the fixture worker writes failures. Publish error_ last so the
        // observer sees the original call and error before teardown APIs run.
        if(error_) return;
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
            " worker_error="+std::to_string(error_.load());
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
                        if(!SetClipboardData(CF_UNICODETEXT,memory)) {
                            owner->record_failure("SetClipboardData-render",GetLastError());
                            GlobalFree(memory);
                        }
                    }
                }
            }
            owner->step("SetEvent-render-released");
            if(!SetEvent(owner->released_.get())) owner->record_failure("SetEvent-render-released",GetLastError());
            return 0;
        }
        if(message==WM_CLOSE) { DestroyWindow(window); return 0; }
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
        try { thread_.join(); } catch(...) { std::_Exit(92); }
    }
public:
    explicit ClipboardScenarioOwner(bool delayed):delayed_(delayed) {
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
                if(opened) CloseClipboard();
                if(window_ && IsWindow(window_)) DestroyWindow(window_);
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
    bool released() const {
        if(error_) {
            const auto detail=worker_failure();
            std::cerr<<"[TF-CLIPBOARD-OWNER] "<<detail<<std::endl;
            throw std::runtime_error(detail);
        }
        return clipboard_event(released_.get());
    }
    bool occupied() const { return !delayed_ && GetOpenClipboardWindow()==window_; }
    void release() { paste_check(SetEvent(release_.get())!=FALSE,"clipboard owner release failed"); }
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
    size_t receiver_bytes=0;
};
struct ClipboardScenarioState {
    unsigned index=0;
    std::unique_ptr<ClipboardScenarioOwner> owner;
    std::unique_ptr<PasteReceiverGate> receiver;
    tf_conpty::Handle helper;
    HWND target=nullptr;
    ClipboardWindowCase pending;
    std::vector<ClipboardWindowCase> cases;
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
            flag("retry_verified",row.retry_verified); out<<'}';
        }
        out<<"]}\n"; out.close(); paste_check(out.good(),"clipboard workflow evidence write failed");
    }
};
} // namespace tf_shell_test
