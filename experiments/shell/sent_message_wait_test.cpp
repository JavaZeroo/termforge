// Native cross-thread SendMessage regression. No clipboard or input injection.
#include "sent_message_wait.hpp"
#include "ime_desktop_process.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

static void require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
constexpr UINT sent_message=WM_APP+1, posted_message=WM_APP+2;
struct State { unsigned sent=0,posted=0; };
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        state=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(state && message==sent_message) { ++state->sent; return 73; }
    if(state && message==posted_message) { ++state->posted; return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
static int run() {
    WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"TermForgeSentMessageWaitTest";
    require(RegisterClassW(&cls)!=0,"synthetic test class creation failed");
    State state;
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"owned synthetic message target",0,
        0,0,8,8,HWND_MESSAGE,nullptr,cls.hInstance,&state);
    require(window!=nullptr,"synthetic test window creation failed");
    HANDLE started=CreateEventW(nullptr,TRUE,FALSE,nullptr), completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    require(started && completed,"synthetic handshake event creation failed");
    std::atomic<bool> send_verified{false};
    require(PostMessageW(window,posted_message,0,0)!=FALSE,"posted sentinel creation failed");
    std::thread sender([&] {
        SetEvent(started);
        DWORD_PTR result=0;
        const auto success=SendMessageTimeoutW(window,sent_message,0,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,1500,&result);
        send_verified=success!=0 && result==73;
        SetEvent(completed);
    });
    const auto sender_started=WaitForSingleObject(started,1000);
    // The previous UI wait cannot service the queued synchronous call. Keep
    // this control observation so a green test still demonstrates the cause.
    const auto original_wait=WaitForSingleObject(completed,75);
    const auto sent_before=state.sent;
    const auto actual_wait=tf_shell_test::wait_event_with_sent_messages(completed,500);
    const auto sent_after=state.sent,posted_after=state.posted;
    // Always release/join the owned sender before any assertion throws. This
    // also makes the deliberately red old-wait implementation fail boundedly.
    MSG pending{}; PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE);
    const auto sender_joined=WaitForSingleObject(sender.native_handle(),2000);
    if(sender_joined!=WAIT_OBJECT_0) std::_Exit(91);
    sender.join();
    require(sender_started==WAIT_OBJECT_0,"sender start handshake failed");
    require(original_wait==WAIT_TIMEOUT && sent_before==0,"plain wait unexpectedly serviced the cross-thread message");
    require(actual_wait==WAIT_OBJECT_0 && sent_after==1 && send_verified,
        "bounded UI wait did not service the real synchronous message and completion event");
    require(posted_after==0 && state.posted==0,"wait dispatched posted application work");
    require(PeekMessageW(&pending,window,posted_message,posted_message,PM_NOREMOVE)!=FALSE &&
            pending.message==posted_message,"wait removed the posted sentinel");
    require(PeekMessageW(&pending,window,posted_message,posted_message,PM_REMOVE)!=FALSE,
            "posted sentinel could not be consumed by its owner after the wait");
    // An unrelated posted message must neither wake a sent-only wait nor be
    // removed, including when the observed event never becomes signalled.
    ResetEvent(completed); require(PostMessageW(window,posted_message,0,0)!=FALSE,"timeout sentinel creation failed");
    const auto begin=GetTickCount64();
    const auto timeout=tf_shell_test::wait_event_with_sent_messages(completed,120);
    const auto elapsed=GetTickCount64()-begin;
    require(timeout==WAIT_TIMEOUT && elapsed>=90 && elapsed<1500,"empty event did not respect the bounded timeout");
    require(state.posted==0 && PeekMessageW(&pending,window,posted_message,posted_message,PM_REMOVE)!=FALSE,
            "timeout wait dispatched or removed posted application work");
    SetEvent(completed);
    require(tf_shell_test::wait_event_with_sent_messages(completed,0)==WAIT_OBJECT_0,
            "already signalled event was not immediately observed");
    ResetEvent(completed);
    require(tf_shell_test::wait_event_with_sent_messages(completed,0)==WAIT_TIMEOUT,
            "zero timeout on an empty event was not bounded");
    require(tf_shell_test::wait_event_with_sent_messages(nullptr,50)==WAIT_FAILED && GetLastError()==ERROR_INVALID_HANDLE,
            "invalid event failure was not preserved");
    require(CloseHandle(started)!=FALSE && CloseHandle(completed)!=FALSE,"test event cleanup failed");
    require(DestroyWindow(window)!=FALSE && UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,
            "synthetic window and class cleanup failed");
    std::cout<<"real synchronous message/event handshake passed; posted work retained; timeout_ms="<<elapsed<<'\n';
    return 0;
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--owned-desktop-child") {
            require(tf_shell::is_ime_desktop_child(),"synthetic message test requires its own non-input desktop");
            return run();
        }
        require(argc==1,"unexpected sent-message test arguments");
        const auto result=tf_shell::run_ime_desktop_child(10000);
        require(result==0,"owned desktop sent-message assertions failed");
        std::cout<<"owned process Job drained and non-input desktop deletion verified\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
