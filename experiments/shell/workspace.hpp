// Independent UI-A workspace. Measurements: docs/research/windterm-ui-2026-10-23.md.
#pragma once
#include <cmath>

class Workspace {
    struct Tab { int id; std::unique_ptr<ShellWindow> shell; };
    HWND window_{};
    HFONT font_{};
    std::vector<Tab> tabs_;
    int active_ = -1, next_id_ = 1, stage_ = 0, collected_ = 0;
    bool closing_ = false, focus_ = false, sides_ = true;
    std::string image_, failure_;
    Clock::time_point started_ = Clock::now();
    RECT terminal_{}, tab_bar_{};
    int width_ = 1360, height_ = 860;
    int side_ = 226, middle_ = 289;
    static constexpr COLORREF background = RGB(28,28,28), foreground = RGB(187,187,187), accent = RGB(0,122,204);
    static bool inside(RECT r, int x, int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
    ShellWindow* active() { return active_>=0 ? tabs_[active_].shell.get() : nullptr; }
    int tab_width() const { return std::min(140,width_/static_cast<int>(tabs_.size()+1)); }
    void select(int index) {
        if (index<0 || index>=static_cast<int>(tabs_.size())) return;
        active_=index; layout();
        if (!closing_) SetFocus(active()->handle());
    }
    void add() {
        if (closing_ || tabs_.size()>=8) return;
        auto shell=std::make_unique<ShellWindow>("", ""); shell->create(window_);
        tabs_.push_back({next_id_++, std::move(shell)}); select(static_cast<int>(tabs_.size())-1);
    }
    void close_all() {
        if (closing_) return;
        closing_=true;
        for (auto& tab:tabs_) tab.shell->close();
        if (tabs_.empty()) DestroyWindow(window_);
    }
    void action(int id) {
        if (closing_) return;
        if (id==1) add();
        if (id==2 && active()) active()->close();
        if (id==3) { focus_=!focus_; layout(); }
        if (id==4) { sides_=!sides_; layout(); }
        if (id==5) close_all();
    }
    void layout() {
        RECT area{}; GetClientRect(window_,&area); width_=area.right; height_=area.bottom;
        // Reference geometry at 96 logical units. Physical DPI parity is not claimed.
        int work=std::max(100,height_-90);
        int upper=static_cast<int>(std::lround((work-1)*2.0/3.0));
        side_=sides_?static_cast<int>(std::lround((width_-2)/6.0)):0;
        middle_=33+static_cast<int>(std::lround((upper-1)/2.0));
        int bottom=focus_?33:34+upper;
        tab_bar_={0,bottom+24,width_,bottom+50};
        terminal_={0,tab_bar_.bottom,width_,std::max<LONG>(tab_bar_.bottom+26,height_-57)};
        for(size_t i=0;i<tabs_.size();++i) {
            auto handle=tabs_[i].shell->handle();
            if (!handle) continue;
            MoveWindow(handle,terminal_.left,terminal_.top,terminal_.right,terminal_.bottom-terminal_.top,TRUE);
            ShowWindow(handle,static_cast<int>(i)==active_?SW_SHOWNA:SW_HIDE);
        }
        InvalidateRect(window_,nullptr,FALSE);
    }
    static void fill(HDC dc, RECT r, COLORREF color) {
        HBRUSH brush=CreateSolidBrush(color); FillRect(dc,&r,brush); DeleteObject(brush);
    }
    static void label(HDC dc, RECT r, const std::wstring& text, COLORREF color=foreground) {
        SetTextColor(dc,color); r.left+=8;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void pane(HDC dc, RECT r, const std::wstring& title) {
        fill(dc,r,background); RECT header{r.left,r.top,r.right,r.top+24};
        fill(dc,header,RGB(51,51,51)); label(dc,header,title);
    }
    void paint(HDC dc) {
        auto old=SelectObject(dc,font_); SetBkMode(dc,TRANSPARENT);
        fill(dc,{0,0,width_,height_},background);
        label(dc,{0,0,82,33},L"Session"); label(dc,{82,0,150,33},L"View");
        fill(dc,{0,32,width_,33},RGB(68,68,68));
        if (!focus_) {
            const int side=side_;
            const int center_left=sides_?side+1:0, center_right=sides_?width_-side-1:width_;
            const int middle=middle_;
            fill(dc,{0,33,width_,tab_bar_.top-24},RGB(68,68,68));
            fill(dc,{center_left,33,center_right,tab_bar_.top-25},background);
            fill(dc,{center_left,52,center_right,86},RGB(35,35,35));
            label(dc,{center_left+8,52,center_right-8,86},L"TermForge");
            label(dc,{center_left+24,110,center_right-24,146},L"Local shells are available in the Shell pane below.");
            label(dc,{center_left+24,146,center_right-24,178},L"Ctrl+T  New shell     Ctrl+Tab  Switch shell     F11  Focus shell");
            if(sides_) {
                pane(dc,{0,33,side,middle},L"Explorer");
                pane(dc,{0,middle+1,side,tab_bar_.top-25},L"Filer");
                pane(dc,{width_-side,33,width_,middle},L"Sessions");
                pane(dc,{width_-side,middle+1,width_,tab_bar_.top-25},L"Outline");
                for(size_t i=0;i<tabs_.size();++i) {
                    RECT row{width_-side,57+static_cast<int>(i)*26,width_,83+static_cast<int>(i)*26};
                    if(row.bottom>middle) break;
                    if(static_cast<int>(i)==active_) fill(dc,row,RGB(42,55,65));
                    label(dc,row,L"cmd "+std::to_wstring(tabs_[i].id));
                }
            }
        }
        pane(dc,{0,tab_bar_.top-24,width_,terminal_.bottom},L"Shell");
        for(size_t i=0;i<tabs_.size();++i) {
            RECT tab{static_cast<LONG>(i)*tab_width(),tab_bar_.top,static_cast<LONG>(i+1)*tab_width(),tab_bar_.bottom};
            fill(dc,tab,static_cast<int>(i)==active_?background:RGB(51,51,51));
            label(dc,tab,L"cmd "+std::to_wstring(tabs_[i].id)+(tabs_[i].shell->closing()?L" ...":L""));
            if(static_cast<int>(i)==active_) {
                fill(dc,{tab.left,tab.bottom-2,tab.right,tab.bottom},accent);
                label(dc,{tab.right-25,tab.top,tab.right,tab.bottom},L"x");
            }
        }
        label(dc,{static_cast<LONG>(tabs_.size())*tab_width(),tab_bar_.top,width_,tab_bar_.bottom},L"+");
        fill(dc,{0,height_-56,width_,height_-29},RGB(35,35,35));
        label(dc,{0,height_-56,width_,height_-29},L"+ New shell");
        fill(dc,{0,height_-29,width_,height_},RGB(38,38,38));
        label(dc,{0,height_-29,width_,height_},closing_?L"Closing sessions...":L"LOCAL     "+std::to_wstring(tabs_.size())+L" sessions");
        SelectObject(dc,old);
    }
    void click(int x,int y) {
        if(y<33) {
            HMENU menu=CreatePopupMenu();
            if(x<82) { AppendMenuW(menu,MF_STRING,1,L"New shell\tCtrl+T"); AppendMenuW(menu,MF_STRING,2,L"Close shell\tCtrl+W"); AppendMenuW(menu,MF_STRING,5,L"Exit"); }
            else { AppendMenuW(menu,MF_STRING|(focus_?MF_CHECKED:0),3,L"Focus shell\tF11"); AppendMenuW(menu,MF_STRING|(sides_?MF_CHECKED:0),4,L"Side panels"); }
            POINT at{x,33}; ClientToScreen(window_,&at);
            const int selected=TrackPopupMenu(menu,TPM_RETURNCMD,at.x,at.y,0,window_,nullptr);
            DestroyMenu(menu); action(selected); return;
        }
        if(inside(tab_bar_,x,y)) {
            int index=x/tab_width();
            if(index==static_cast<int>(tabs_.size())) add();
            else if(index==active_ && x%tab_width()>=tab_width()-25) action(2);
            else select(index);
        } else if(y>=height_-56 && y<height_-29 && x<140) add();
        else if(sides_ && !focus_ && x>=width_-side_ && y>=57) {
            if(y<middle_) select((y-57)/26);
        }
    }
    void scenario() {
        if(image_.empty() || closing_) return;
        require(ms(started_)<20000,"workspace scenario timeout");
        if(stage_==0 && active() && active()->ready()) { active()->command(L"echo TF_UI_FIRST\r"); stage_=1; }
        else if(stage_==1 && active()->contains_line("TF_UI_FIRST")) {
            // Queued WM_CHAR is dispatched only after add() hides this child.
            active()->command(L"echo TF_UI_BACKGROUND\r");
            SendMessageW(window_,WM_COMMAND,1,0); stage_=2;
        }
        else if(stage_==2 && active()->ready()) { active()->command(L"echo TF_UI_SECOND\r"); stage_=3; }
        else if(stage_==3 && active()->contains_line("TF_UI_SECOND") && tabs_[0].shell->contains_line("TF_UI_BACKGROUND")) {
            require(!active()->contains_line("TF_UI_FIRST"),"session state leaked");
            require(tabs_[0].shell->contains_line("TF_UI_BACKGROUND"),"hidden session stopped consuming output");
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(width_-20,65));
            require(active_==0,"session list selection failed");
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(160,tab_bar_.top+10));
            require(active_==1,"tab selection failed");
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(20,tab_bar_.top+10));
            require(active()->contains_line("TF_UI_FIRST"),"first session lost state");
            SendMessageW(window_,WM_COMMAND,3,0); stage_=4;
        } else if(stage_==4 && active()->settled()) { active()->check_size(); stage_=40;
        } else if(stage_==40 && active()->contains_line("TF_SIZE_113x27")) {
            SendMessageW(window_,WM_COMMAND,3,0); stage_=5;
        } else if(stage_==5 && active()->settled()) { active()->check_size(); stage_=50;
        } else if(stage_==50 && active()->contains_line("TF_SIZE_113x7")) {
            Bitmap frame(width_,height_); paint(frame.dc); active()->render_to(frame.dc,terminal_.left,terminal_.top);
            frame.save(image_);
            RECT size{0,0,2048,1120}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
            require(SetWindowPos(window_,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"workspace resize failed");
            stage_=51;
        } else if(stage_==51 && active()->settled()) { active()->check_size(); stage_=52;
        } else if(stage_==52 && active()->contains_line("TF_SIZE_170x11")) {
            SendMessageW(window_,WM_COMMAND,4,0); require(!sides_,"panel toggle failed");
            SendMessageW(window_,WM_COMMAND,4,0); require(sides_,"panel restore failed");
            SendMessageW(window_,WM_COMMAND,2,0); stage_=6;
        } else if(stage_==6 && tabs_.size()==1) {
            require(active()->contains_line("TF_UI_SECOND"),"remaining session lost state");
            SendMessageW(window_,WM_COMMAND,2,0); stage_=7;
        } else if(stage_==7 && tabs_.empty()) {
            SendMessageW(window_,WM_COMMAND,1,0); stage_=8;
        } else if(stage_==8 && active()->ready()) { active()->command(L"echo TF_UI_REOPEN\r"); stage_=9;
        } else if(stage_==9 && active()->contains_line("TF_UI_REOPEN")) {
            stage_=10; SendMessageW(window_,WM_CLOSE,0,0);
        }
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<Workspace*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) { self=static_cast<Workspace*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); self->window_=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if(!self) return DefWindowProcW(hwnd,message,wp,lp);
        try {
            if(message==WM_CLOSE) { self->close_all(); return 0; }
            if(message==WM_DESTROY) { self->window_=nullptr; PostQuitMessage(0); return 0; }
            if(message==WM_SIZE && wp!=SIZE_MINIMIZED) { self->layout(); return 0; }
            if(message==WM_GETMINMAXINFO) { auto m=reinterpret_cast<MINMAXINFO*>(lp); m->ptMinTrackSize={1000,650}; m->ptMaxTrackSize={6020,5300}; return 0; }
            if(message==WM_SETFOCUS && self->active()) { SetFocus(self->active()->handle()); return 0; }
            if(message==WM_COMMAND) { self->action(LOWORD(wp)); return 0; }
            if(message==WM_LBUTTONDOWN) { self->click(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))); return 0; }
            if(message==WM_TIMER) { self->scenario(); return 0; }
            if(message==WM_APP+1) {
                for(size_t i=0;i<self->tabs_.size();++i) if(self->tabs_[i].shell.get()==reinterpret_cast<ShellWindow*>(lp)) {
                    try {
                        const Result result=self->tabs_[i].shell->collect();
                        require(result.complete && result.active_children==0,"workspace child cleanup failed");
                    } catch(const std::exception& error) { self->failure_=error.what(); }
                    if(static_cast<int>(i)<self->active_) --self->active_;
                    self->tabs_.erase(self->tabs_.begin()+i); ++self->collected_;
                    self->active_=self->tabs_.empty()?-1:std::max(0,std::min(self->active_,static_cast<int>(self->tabs_.size())-1));
                    if(!self->failure_.empty() && !self->closing_) self->close_all();
                    else if(self->closing_ && self->tabs_.empty()) DestroyWindow(hwnd);
                    else { self->layout(); if(self->active()) SetFocus(self->active()->handle()); }
                    return 0;
                }
            }
            if(message==WM_ERASEBKGND) return 1;
            if(message==WM_PAINT) {
                PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
                try { self->paint(dc); } catch(...) { EndPaint(hwnd,&ps); throw; }
                EndPaint(hwnd,&ps); return 0;
            }
        } catch(const std::exception& error) { self->failure_=error.what(); self->close_all(); }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
public:
    explicit Workspace(std::string image=""):image_(std::move(image)) {}
    ~Workspace() { tabs_.clear(); if(window_) DestroyWindow(window_); if(font_) DeleteObject(font_); UnregisterClassW(L"TermForgeWorkspace",GetModuleHandleW(nullptr)); }
    int run() {
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"TermForgeWorkspace"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        require(RegisterClassW(&cls)!=0,"workspace class failed");
        font_=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas"); require(font_!=nullptr,"workspace font failed");
        RECT rect{0,0,1360,860}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        require(CreateWindowExW(0,cls.lpszClassName,L"TermForge",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,this)!=nullptr,"workspace creation failed");
        add();
        if(!image_.empty()) require(SetTimer(window_,1,30,nullptr)!=0,"workspace timer failed");
        ShowWindow(window_,image_.empty()?SW_SHOW:SW_SHOWNOACTIVATE);
        MSG msg{}; BOOL status;
        while((status=GetMessageW(&msg,nullptr,0,0))>0) {
            if(msg.message==WM_KEYDOWN && !closing_) {
                bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
                if(ctrl && msg.wParam=='T') { action(1); continue; }
                if(ctrl && msg.wParam=='W') { action(2); continue; }
                if(ctrl && msg.wParam==VK_TAB && !tabs_.empty()) { select((active_+1)%static_cast<int>(tabs_.size())); continue; }
                if(msg.wParam==VK_F11) { action(3); continue; }
            }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        require(status==0,"workspace message loop failed"); require(failure_.empty(),failure_.c_str());
        if(!image_.empty()) {
            require(stage_==10 && collected_==3,"workspace scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui\",\"sessions_closed\":3,\"isolation_verified\":true,\"resize_verified\":true,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        return 0;
    }
};
