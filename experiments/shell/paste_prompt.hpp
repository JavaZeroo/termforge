// Owned modeless confirmation. Its decision is polled by the session owner.
#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>

namespace tf_shell {
class PastePrompt {
    HWND window_ = nullptr;
    int decision_ = 0; // 0 pending, 1 accept, -1 cancel.
    inline static unsigned users_ = 0;
    bool registered_ = false;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self=reinterpret_cast<PastePrompt*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<PastePrompt*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(window,message,wp,lp);
        if(message==WM_CLOSE || (message==WM_COMMAND && (LOWORD(wp)==IDOK || LOWORD(wp)==IDCANCEL))) {
            self->decision_=message==WM_COMMAND && LOWORD(wp)==IDOK?1:-1;
            DestroyWindow(window);
            return 0;
        }
        if(message==WM_NCDESTROY) { self->window_=nullptr; SetWindowLongPtrW(window,GWLP_USERDATA,0); }
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    PastePrompt(HWND terminal, uint64_t target, size_t newlines, size_t bytes) {
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr);
        cls.lpszClassName=L"TermForgePastePrompt"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
        if(!users_ && !RegisterClassW(&cls)) throw std::runtime_error("paste confirmation class failed");
        ++users_; registered_=true;
        try {
            const UINT dpi=GetDpiForWindow(terminal);
            auto px=[&](int value) { return MulDiv(value,dpi?dpi:96,96); };
            RECT parent{}; GetWindowRect(GetAncestor(terminal,GA_ROOT),&parent);
            RECT area{0,0,px(440),px(145)};
            const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
            AdjustWindowRectExForDpi(&area,style,FALSE,WS_EX_TOOLWINDOW,dpi?dpi:96);
            const auto title=L"Paste to session "+std::to_wstring(target);
            if(!CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,cls.lpszClassName,title.c_str(),style,
                parent.left+px(40),parent.top+px(60),area.right-area.left,area.bottom-area.top,
                GetAncestor(terminal,GA_ROOT),nullptr,cls.hInstance,this)) throw std::runtime_error("paste confirmation window failed");
            const auto text=L"This text contains "+std::to_wstring(newlines)+L" newline(s) and "+std::to_wstring(bytes)+
                L" bytes. Pasting can execute commands.\r\nSend it to this session?";
            const auto label=CreateWindowExW(0,L"STATIC",text.c_str(),WS_CHILD|WS_VISIBLE,
                px(16),px(14),px(408),px(70),window_,nullptr,cls.hInstance,nullptr);
            const auto yes=CreateWindowExW(0,L"BUTTON",L"Paste",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                px(242),px(100),px(84),px(28),window_,reinterpret_cast<HMENU>(IDOK),cls.hInstance,nullptr);
            const auto no=CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                px(338),px(100),px(84),px(28),window_,reinterpret_cast<HMENU>(IDCANCEL),cls.hInstance,nullptr);
            if(!label || !yes || !no) throw std::runtime_error("paste confirmation controls failed");
            for(HWND control:{label,yes,no}) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            ShowWindow(window_,SW_SHOWNORMAL); SetFocus(no);
        } catch(...) { if(window_) DestroyWindow(window_); if(--users_==0) UnregisterClassW(cls.lpszClassName,cls.hInstance); registered_=false; throw; }
    }
    ~PastePrompt() {
        if(window_) DestroyWindow(window_);
        if(registered_ && --users_==0) UnregisterClassW(L"TermForgePastePrompt",GetModuleHandleW(nullptr));
    }
    HWND handle() const { return window_; }
    int decision() const { return decision_; }
    bool route(MSG& message) {
        if(!window_ || (message.hwnd!=window_ && !IsChild(window_,message.hwnd))) return false;
        if(message.message==WM_KEYDOWN && (message.wParam==VK_ESCAPE || message.wParam==VK_RETURN)) {
            const auto target=message.wParam==VK_RETURN && GetDlgCtrlID(GetFocus())==IDOK?IDOK:IDCANCEL;
            SendMessageW(window_,WM_COMMAND,target,0); return true;
        }
        if(!IsDialogMessageW(window_,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        return true;
    }
};
} // namespace tf_shell
