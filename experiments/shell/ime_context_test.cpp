// Native HIMC/HWND lifecycle contract. Synthetic messages deliberately do NOT
// prove actual IME composition, candidate rendering or keyboard conversion.
#include "ime.hpp"
#include "ime_desktop_process.hpp"
#include <iostream>
static void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
static void pump() {
    MSG message{};
    for(int count=0;count<1000 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++count) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
struct State { tf_shell::ImeContext ime; tf_shell::KeyboardEncoder keys; std::string error; };
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto state=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        state=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(state) {
        if(message==WM_KILLFOCUS) state->keys.reset();
        const auto result=state->ime.handle(message,wp,lp);
        if(!result.error.empty()) state->error=result.error;
        if(result.handled) return result.result;
    }
    return DefWindowProcW(window,message,wp,lp);
}
static int run() {
    WNDCLASSW cls{}; cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"TermForgeImeContextTest";
    require(RegisterClassW(&cls)!=0,"test class creation failed");
    // Keep an application frame alive while terminal windows come and go.
    // Destroying the last active top-level HWND on a non-input desktop makes
    // the next activation wait for the OS input service (about 2s locally).
    HWND anchor=CreateWindowExW(0,cls.lpszClassName,L"owned test frame",WS_OVERLAPPEDWINDOW,
                               0,0,600,400,nullptr,nullptr,cls.hInstance,nullptr);
    require(anchor!=nullptr,"test frame creation failed");
    ShowWindow(anchor,SW_SHOWNOACTIVATE); SetActiveWindow(anchor);
    for(int iteration=0;iteration<20;++iteration) {
        State first, second;
        HWND a=CreateWindowExW(0,cls.lpszClassName,L"first",WS_OVERLAPPEDWINDOW,0,0,600,400,nullptr,nullptr,cls.hInstance,&first);
        HWND b=CreateWindowExW(0,cls.lpszClassName,L"second",WS_OVERLAPPEDWINDOW,0,0,600,400,nullptr,nullptr,cls.hInstance,&second);
        require(a && b,"test window creation failed");
        first.ime.attach(a); second.ime.attach(b);
        require(first.ime.native_context()!=second.ime.native_context(),"terminal windows must not share HIMC");
        ShowWindow(a,SW_SHOWNOACTIVATE); ShowWindow(b,SW_SHOWNOACTIVATE); SetActiveWindow(a); SetFocus(a); pump();
        first.ime.handle(WM_IME_STARTCOMPOSITION,0,0);
        require(first.ime.composing(),"focused start composition state");
        first.keys.character(0xd800);
        SetActiveWindow(b); SetFocus(b); pump();
        require(!first.ime.composing() && !first.keys.pending_surrogate(),"focus transfer cancels old terminal input state");
        const auto late=first.ime.handle(WM_IME_COMPOSITION,0,GCS_RESULTSTR);
        require(late.handled && late.committed.empty() && late.error.empty(),"late result from unfocused terminal is discarded");
        second.ime.handle(WM_IME_STARTCOMPOSITION,0,0);
        require(second.ime.composing(),"new session has independent composing state");
        second.ime.handle(WM_IME_COMPOSITION,0,0);
        require(!second.ime.composing() && second.ime.preedit().empty(),"zero flags cancel synthetic preedit");
        for(int dpi : {96,144,192}) {
            RECT rect{40*dpi/96,50*dpi/96,52*dpi/96,74*dpi/96};
            require(second.ime.set_caret(rect),"candidate request positioning failed");
            CANDIDATEFORM actual{};
            require(ImmGetCandidateWindow(second.ime.native_context(),0,&actual)!=FALSE && actual.dwStyle==CFS_EXCLUDE &&
                    EqualRect(&rect,&actual.rcArea),"physical candidate position request roundtrip");
        }
        SetFocus(nullptr); SetActiveWindow(anchor); pump();
        const bool first_destroyed=first.ime.detach(), second_destroyed=second.ime.detach();
        require(first_destroyed && second_destroyed,"private HIMC destruction failed");
        require(first.error.empty() && second.error.empty(),"unexpected native IME error");
        require(DestroyWindow(a)!=FALSE && DestroyWindow(b)!=FALSE,"test windows cleanup failed"); pump();
    }
    require(DestroyWindow(anchor)!=FALSE,"test frame cleanup failed");
    require(UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,"test class cleanup failed");
    return 0;
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && (std::string(argv[1])=="--owned-timeout-child" || std::string(argv[1])=="--owned-failure-child")) {
            require(tf_shell::is_ime_desktop_child(),"IME cleanup child requires its own private desktop");
            if(std::string(argv[1])=="--owned-timeout-child") Sleep(10000);
            return 7;
        }
        if(argc==2 && std::string(argv[1])=="--owned-desktop-child") {
            require(tf_shell::is_ime_desktop_child(),"IME child requires its own private desktop"); return run();
        }
        require(argc==1,"unexpected IME context test arguments");
        const auto result=tf_shell::run_ime_desktop_child();
        require(result==0,"IME context child assertions failed");
        require(tf_shell::run_ime_desktop_child(5000,L"--owned-failure-child")==7,"IME child failure was not propagated");
        bool timed_out=false;
        try {tf_shell::run_ime_desktop_child(25,L"--owned-timeout-child");}
        catch(const std::exception& error) {timed_out=std::string(error.what())=="IME child exceeded bounded deadline";}
        require(timed_out,"IME child deadline did not fail explicitly");
        const auto name=L"TermForgeImeChild-"+std::to_wstring(GetCurrentProcessId());
        HDESK remaining=OpenDesktopW(name.c_str(),0,FALSE,DESKTOP_READOBJECTS);
        if(remaining) {CloseDesktop(remaining);throw std::runtime_error("timed-out IME desktop survived cleanup");}
        require(GetLastError()==ERROR_FILE_NOT_FOUND,"timed-out IME desktop deletion was not proven");
        std::cout<<"native per-window IME contexts, synthetic focus/cancel and requested position roundtrip passed; real IME workflow unverified\n";
        return result;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
