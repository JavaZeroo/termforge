// Independent workspace. Measurements: docs/research/windterm-ui-2026-10-23.md.
#pragma once
#include <cmath>
#include "layout_state.hpp"

class Workspace {
    struct Tab { int id; std::wstring title; std::unique_ptr<ShellWindow> shell; };
    enum class Divider { none, left, right, bottom, left_middle, right_middle };
    HWND window_{};
    HFONT font_{};
    std::vector<Tab> tabs_;
    int active_ = -1, next_id_ = 1, stage_ = 0, collected_ = 0;
    bool closing_ = false, settings_failed_ = false;
    tf_workspace::LayoutState state_, drag_start_;
    Divider dragging_ = Divider::none;
    std::filesystem::path settings_path_;
    HWND rename_{};
    int rename_id_ = -1;
    std::string test_mode_;
    std::string image_, failure_;
    Clock::time_point started_ = Clock::now();
    RECT terminal_{}, tab_bar_{};
    int width_ = 1360, height_ = 860;
    int left_ = 226, right_ = 226, left_middle_ = 289, right_middle_ = 289;
    static constexpr COLORREF background = RGB(28,28,28), foreground = RGB(187,187,187), accent = RGB(0,122,204);
    static bool inside(RECT r, int x, int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
    ShellWindow* active() { return active_>=0 ? tabs_[active_].shell.get() : nullptr; }
    int tab_width() const { return std::min(140,width_/static_cast<int>(tabs_.size()+1)); }
    Divider divider_at(int x, int y) const;
    void drag_to(int x, int y);
    void end_drag(bool commit);
    void begin_rename(int index);
    void end_rename(bool commit);
    bool edit_key(const MSG& msg);
    void save_layout() {
        settings_failed_ = settings_path_.empty() || !tf_workspace::save_layout(settings_path_,state_);
        InvalidateRect(window_,nullptr,FALSE);
    }
    void select(int index) {
        if (index<0 || index>=static_cast<int>(tabs_.size())) return;
        end_rename(true);
        active_=index; layout();
        if (!closing_) SetFocus(active()->handle());
    }
    void add() {
        if (closing_ || tabs_.size()>=8) return;
        auto shell=std::make_unique<ShellWindow>("", ""); shell->create(window_);
        const int id=next_id_++;
        tabs_.push_back({id,L"cmd "+std::to_wstring(id),std::move(shell)}); select(static_cast<int>(tabs_.size())-1);
    }
    void close_all() {
        if (closing_) return;
        end_rename(false); end_drag(false);
        closing_=true;
        for (auto& tab:tabs_) tab.shell->close();
        if (tabs_.empty()) DestroyWindow(window_);
    }
    void action(int id) {
        if (closing_) return;
        if (id==1) add();
        if (id==2 && active()) active()->close();
        if (id==3) { end_drag(false); state_.focus=!state_.focus; layout(); save_layout(); }
        if (id==4) { end_drag(false); state_.sides=!state_.sides; layout(); save_layout(); }
        if (id==5) close_all();
        if (id==6) begin_rename(active_);
        if (id==7) { end_rename(false); end_drag(false); state_={}; layout(); save_layout(); }
    }
    void layout() {
        RECT area{}; GetClientRect(window_,&area); width_=area.right; height_=area.bottom;
        // Reference geometry at 96 logical units. Physical DPI parity is not claimed.
        int work=std::max(100,height_-90);
        int upper=static_cast<int>(std::lround((work-1)*state_.upper));
        left_=state_.sides?static_cast<int>(std::lround((width_-2)*state_.left)):0;
        right_=state_.sides?static_cast<int>(std::lround((width_-2)*state_.right)):0;
        const int pane_min=std::min(48,(upper-1)/2);
        left_middle_=33+std::clamp(static_cast<int>(std::lround((upper-1)*state_.left_top)),pane_min,upper-1-pane_min);
        right_middle_=33+std::clamp(static_cast<int>(std::lround((upper-1)*state_.right_top)),pane_min,upper-1-pane_min);
        int bottom=state_.focus?33:34+upper;
        tab_bar_={0,bottom+24,width_,bottom+50};
        terminal_={0,tab_bar_.bottom,width_,std::max<LONG>(tab_bar_.bottom+26,height_-57)};
        for(size_t i=0;i<tabs_.size();++i) {
            auto handle=tabs_[i].shell->handle();
            if (!handle) continue;
            MoveWindow(handle,terminal_.left,terminal_.top,terminal_.right,terminal_.bottom-terminal_.top,TRUE);
            ShowWindow(handle,static_cast<int>(i)==active_?SW_SHOWNA:SW_HIDE);
        }
        if(rename_) {
            for(size_t i=0;i<tabs_.size();++i) if(tabs_[i].id==rename_id_)
                MoveWindow(rename_,static_cast<int>(i)*tab_width()+4,tab_bar_.top+2,tab_width()-32,22,TRUE);
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
        if (!state_.focus) {
            const int center_left=state_.sides?left_+1:0, center_right=state_.sides?width_-right_-1:width_;
            fill(dc,{0,33,width_,tab_bar_.top-24},RGB(68,68,68));
            fill(dc,{center_left,33,center_right,tab_bar_.top-25},background);
            fill(dc,{center_left,52,center_right,86},RGB(35,35,35));
            label(dc,{center_left+8,52,center_right-8,86},L"TermForge");
            label(dc,{center_left+24,110,center_right-24,146},L"Local shells are available in the Shell pane below.");
            label(dc,{center_left+24,146,center_right-24,178},L"Ctrl+T  New shell     Ctrl+Tab  Switch shell     F11  Focus shell");
            if(state_.sides) {
                pane(dc,{0,33,left_,left_middle_},L"Explorer");
                pane(dc,{0,left_middle_+1,left_,tab_bar_.top-25},L"Filer");
                pane(dc,{width_-right_,33,width_,right_middle_},L"Sessions");
                pane(dc,{width_-right_,right_middle_+1,width_,tab_bar_.top-25},L"Outline");
                for(size_t i=0;i<tabs_.size();++i) {
                    RECT row{width_-right_,57+static_cast<int>(i)*26,width_,83+static_cast<int>(i)*26};
                    if(row.bottom>right_middle_) break;
                    if(static_cast<int>(i)==active_) fill(dc,row,RGB(42,55,65));
                    label(dc,row,tabs_[i].title);
                }
            }
        }
        pane(dc,{0,tab_bar_.top-24,width_,terminal_.bottom},L"Shell");
        for(size_t i=0;i<tabs_.size();++i) {
            RECT tab{static_cast<LONG>(i)*tab_width(),tab_bar_.top,static_cast<LONG>(i+1)*tab_width(),tab_bar_.bottom};
            fill(dc,tab,static_cast<int>(i)==active_?background:RGB(51,51,51));
            RECT title=tab; if(static_cast<int>(i)==active_) title.right-=25;
            label(dc,title,tabs_[i].title+(tabs_[i].shell->closing()?L" ...":L""));
            if(static_cast<int>(i)==active_) {
                fill(dc,{tab.left,tab.bottom-2,tab.right,tab.bottom},accent);
                label(dc,{tab.right-25,tab.top,tab.right,tab.bottom},L"x");
            }
        }
        label(dc,{static_cast<LONG>(tabs_.size())*tab_width(),tab_bar_.top,width_,tab_bar_.bottom},L"+");
        fill(dc,{0,height_-56,width_,height_-29},RGB(35,35,35));
        label(dc,{0,height_-56,width_,height_-29},L"+ New shell");
        fill(dc,{0,height_-29,width_,height_},RGB(38,38,38));
        label(dc,{0,height_-29,width_,height_},closing_?L"Closing sessions...":settings_failed_?L"Layout could not be saved":L"LOCAL     "+std::to_wstring(tabs_.size())+L" sessions");
        SelectObject(dc,old);
    }
    void click(int x,int y) {
        if(y<33) {
            HMENU menu=CreatePopupMenu();
            if(x<82) { AppendMenuW(menu,MF_STRING,1,L"New shell\tCtrl+T"); AppendMenuW(menu,MF_STRING,2,L"Close shell\tCtrl+W"); AppendMenuW(menu,MF_STRING,6,L"Rename shell\tF2"); AppendMenuW(menu,MF_STRING,5,L"Exit"); }
            else { AppendMenuW(menu,MF_STRING|(state_.focus?MF_CHECKED:0),3,L"Focus shell\tF11"); AppendMenuW(menu,MF_STRING|(state_.sides?MF_CHECKED:0),4,L"Side panels"); AppendMenuW(menu,MF_STRING,7,L"Reset layout"); }
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
        else if(state_.sides && !state_.focus && x>=width_-right_ && y>=57) {
            const int index=(y-57)/26;
            if(83+index*26<=right_middle_) select(index);
        }
    }
    void scenario();
    void scenario_edit();
    void scenario_restore();
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
            if(message==WM_COMMAND) {
                if(reinterpret_cast<HWND>(lp)==self->rename_ && self->rename_ && HIWORD(wp)==EN_KILLFOCUS) self->end_rename(true);
                else self->action(LOWORD(wp));
                return 0;
            }
            if(message==WM_LBUTTONDOWN) {
                const int x=static_cast<short>(LOWORD(lp)), y=static_cast<short>(HIWORD(lp));
                const auto divider=self->divider_at(x,y);
                self->end_rename(true);
                if(divider!=Divider::none && !self->closing_) { self->drag_start_=self->state_; self->dragging_=divider; SetCapture(hwnd); }
                else self->click(x,y);
                return 0;
            }
            if(message==WM_MOUSEMOVE && self->dragging_!=Divider::none) { self->drag_to(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))); return 0; }
            if(message==WM_LBUTTONUP && self->dragging_!=Divider::none) { self->drag_to(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))); self->end_drag(true); return 0; }
            if(message==WM_CAPTURECHANGED) { self->end_drag(false); return 0; }
            if(message==WM_MBUTTONDOWN && inside(self->tab_bar_,static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp)))) { self->begin_rename(static_cast<short>(LOWORD(lp))/self->tab_width()); return 0; }
            if(message==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
                POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd,&point);
                const auto divider=self->divider_at(point.x,point.y);
                if(divider!=Divider::none) { SetCursor(LoadCursorW(nullptr,divider==Divider::left || divider==Divider::right?IDC_SIZEWE:IDC_SIZENS)); return TRUE; }
            }
            if(message==WM_TIMER) {
                if(self->test_mode_=="ui-edit") self->scenario_edit();
                else if(self->test_mode_=="ui-restore") self->scenario_restore();
                else self->scenario();
                return 0;
            }
            if(message==WM_APP+1) {
                for(size_t i=0;i<self->tabs_.size();++i) if(self->tabs_[i].shell.get()==reinterpret_cast<ShellWindow*>(lp)) {
                    if(self->rename_id_==self->tabs_[i].id) self->end_rename(false);
                    try {
                        const Result result=self->tabs_[i].shell->collect();
                        require(result.complete && result.active_children==0,"workspace child cleanup failed");
                    } catch(const std::exception& error) { self->failure_=error.what(); }
                    if(static_cast<int>(i)<self->active_) --self->active_;
                    self->tabs_.erase(self->tabs_.begin()+i); ++self->collected_;
                    self->active_=self->tabs_.empty()?-1:std::max(0,std::min(self->active_,static_cast<int>(self->tabs_.size())-1));
                    if(!self->failure_.empty() && !self->closing_) self->close_all();
                    else if(self->closing_ && self->tabs_.empty()) DestroyWindow(hwnd);
                    else { self->layout(); if(self->rename_) SetFocus(self->rename_); else if(self->active()) SetFocus(self->active()->handle()); }
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
    explicit Workspace(std::string image="",std::string mode="ui"):test_mode_(std::move(mode)),image_(std::move(image)) {
        if(!image_.empty()) settings_path_=std::filesystem::path(image_).parent_path()/"workspace.layout";
        else {
            wchar_t base[32768]{};
            DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
            if(n && n<32768) settings_path_=std::filesystem::path(base)/L"TermForge"/L"workspace-v1.layout";
        }
        state_=tf_workspace::load_layout(settings_path_);
    }
    ~Workspace() { end_rename(false); tabs_.clear(); if(window_) DestroyWindow(window_); if(font_) DeleteObject(font_); UnregisterClassW(L"TermForgeWorkspace",GetModuleHandleW(nullptr)); }
    int run() {
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"TermForgeWorkspace"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        require(RegisterClassW(&cls)!=0,"workspace class failed");
        font_=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas"); require(font_!=nullptr,"workspace font failed");
        // The test starts smaller, reproducing creation-time size clamping.
        RECT rect{0,0,image_.empty()?1360:1000,image_.empty()?860:650}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        require(CreateWindowExW(0,cls.lpszClassName,L"TermForge",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,this)!=nullptr,"workspace creation failed");
        // Creation can query track limits before WM_NCCREATE installs `self`.
        // Apply the desired size once our window procedure owns the HWND.
        rect={0,0,1360,860}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        require(SetWindowPos(window_,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,
                            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"workspace initial resize failed");
        layout();
        require(width_==1360 && height_==860,"workspace initial client size mismatch");
        add();
        if(!image_.empty()) require(SetTimer(window_,1,30,nullptr)!=0,"workspace timer failed");
        ShowWindow(window_,image_.empty()?SW_SHOW:SW_SHOWNOACTIVATE);
        MSG msg{}; BOOL status;
        while((status=GetMessageW(&msg,nullptr,0,0))>0) {
            if(edit_key(msg)) continue;
            if(msg.message==WM_KEYDOWN && !closing_ && !rename_) {
                if(msg.wParam==VK_ESCAPE && dragging_!=Divider::none) { end_drag(false); continue; }
                if(msg.wParam==VK_F2) { action(6); continue; }
                bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
                if(ctrl && msg.wParam=='T') { action(1); continue; }
                if(ctrl && msg.wParam=='W') { action(2); continue; }
                if(ctrl && msg.wParam==VK_TAB && !tabs_.empty()) { select((active_+1)%static_cast<int>(tabs_.size())); continue; }
                if(msg.wParam==VK_F11) { action(3); continue; }
            }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        require(status==0,"workspace message loop failed"); require(failure_.empty(),failure_.c_str());
        if(!image_.empty() && test_mode_=="ui") {
            require(stage_==10 && collected_==3,"workspace scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui\",\"sessions_closed\":3,\"isolation_verified\":true,\"resize_verified\":true,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_!="ui") {
            require(stage_==100 && collected_==1,"workspace interaction scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"interactions_verified\":true,\"sessions_closed\":1,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        return 0;
    }
};
#include "workspace_interactions.hpp"
#include "workspace_scenarios.hpp"
