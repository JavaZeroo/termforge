// Independent workspace. Measurements: docs/research/windterm-ui-2026-10-23.md.
#pragma once
#include "../core/text_projection.hpp"
#include "menu_gesture.hpp"
#include <cmath>
#include "workspace_state.hpp"
#include "ui_font.hpp"
#include "paste_scenario_state.hpp"
#include "clipboard_scenario_state.hpp"
#include "workflow_scenario_state.hpp"
#include "workflow_system_state.hpp"

class Workspace {
    struct Tab {
        int id;
        std::wstring title;
        std::optional<tf_shell::LaunchDefinition> launch;
        std::unique_ptr<ShellWindow> shell;
    };
    enum class Group { bottom, main };
    enum class Divider { none, left, right, bottom, left_middle, right_middle };
    HWND window_{};
    HFONT font_{};
    std::unique_ptr<WorkspaceFont> ui_font_;
    std::vector<Tab> tabs_;
    std::vector<Tab> main_tabs_;
    int main_active_ = -1, split_index_ = -1;
    bool split_active_ = false, split_right_focus_ = false;
    Group focused_ = Group::bottom, rename_group_ = Group::bottom;
    int active_ = -1, next_id_ = 1, stage_ = 0, collected_ = 0;
    bool closing_ = false, settings_failed_ = false, settings_read_only_ = false;
    bool settings_closed_ = false, restoring_ = false, settings_discarded_ = false;
    tf_workspace::LayoutState state_, drag_start_;
    tf_workspace::WorkspaceState restored_workspace_;
    tf_workspace::LoadStatus settings_load_status_ = tf_workspace::LoadStatus::missing;
    Divider dragging_ = Divider::none;
    std::filesystem::path settings_path_;
    HWND rename_{};
    int rename_id_ = -1;
    std::string test_mode_;
    std::string image_, failure_, settings_error_;
    Clock::time_point started_ = Clock::now();
    RECT terminal_{}, tab_bar_{};
    RECT main_group_{}, main_toolbar_{}, main_address_{}, main_terminal_{};
    std::vector<std::string> geometry_samples_;
    uint64_t history_anchor_ = 0;
    tf_shell_test::PasteScenarioState paste_scenario_;
    tf_shell_test::ClipboardScenarioState clipboard_scenario_;
    std::unique_ptr<tf_shell_test::ProductImeScenarioState> ime_scenario_;
    std::unique_ptr<tf_shell_test::WorkflowScenarioState> workflow_;
    std::unique_ptr<tf_shell_test::WorkflowSystemState> workflow_system_;
    tf_conpty::Handle shell_size_release_;
    std::wstring shell_size_event_;
    bool shell_size_fence_observed_ = false;
    int width_ = 1360, height_ = 860, dpi_ = 96;
    int physical_width_ = 1360, physical_height_ = 860;
    int left_ = 226, right_ = 226, left_middle_ = 289, right_middle_ = 289;
    static constexpr COLORREF background = RGB(28,28,28), foreground = RGB(187,187,187), accent = RGB(0,122,204);
    static bool inside(RECT r, int x, int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
    int physical(int value) const { return MulDiv(value,dpi_,96); }
    int logical(int value) const { return MulDiv(value,96,dpi_); }
    int physical_edge(int value,int logical_end,int physical_end) const {
        // An arbitrary physical client may not round-trip through whole DIPs.
        // Anchor flush edges to the measured client endpoint, including its
        // last fractional-DIP pixel, while internal edges share one rounding.
        return value==logical_end?physical_end:physical(value);
    }
    int hit_logical(int value,int logical_end,int physical_end) const {
        // Invert the rounded rectangle edges, not just their scale factor.
        // At 144 DPI, logical x=227 begins at physical x=341, not x=340.
        int result=logical(value);
        while(physical_edge(result,logical_end,physical_end)>value) --result;
        while(physical_edge(result+1,logical_end,physical_end)<=value) ++result;
        return result;
    }
    int hit_x(int value) const { return hit_logical(value,width_,physical_width_); }
    int hit_y(int value) const { return hit_logical(value,height_,physical_height_); }
    RECT physical(RECT value) const {
        return {physical_edge(value.left,width_,physical_width_),physical_edge(value.top,height_,physical_height_),
            physical_edge(value.right,width_,physical_width_),physical_edge(value.bottom,height_,physical_height_)};
    }
    void place(HWND child,RECT logical_rect,bool repaint=true) {
        const RECT rect=physical(logical_rect);
        require(MoveWindow(child,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,repaint?TRUE:FALSE)!=FALSE,"workspace child placement failed");
    }
    RECT rename_rect(Group group,int index) const {
        const RECT rect=tab_rect(group,index);
        return {rect.left+4,rect.top+2,std::max<LONG>(rect.left+12,rect.right-28),rect.top+24};
    }
    void set_dpi(int dpi) {
        require(dpi>=48 && dpi<=768,"unsupported workspace DPI");
        if(dpi_==dpi) return;
        end_drag(false);
        auto next=std::make_unique<WorkspaceFont>(dpi);
        // The edit control must release the old HFONT before its owner dies.
        if(rename_) SendMessageW(rename_,WM_SETFONT,reinterpret_cast<WPARAM>(next->handle()),TRUE);
        font_=next->handle(); ui_font_=std::move(next); dpi_=dpi;
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group)) tab.shell->set_dpi(dpi_,true);
    }
    std::vector<Tab>& group_tabs(Group group) { return group==Group::main?main_tabs_:tabs_; }
    int& selection(Group group) { return group==Group::main?main_active_:active_; }
    ShellWindow* selected(Group group) {
        const int index=group==Group::main && split_active_ && split_right_focus_?split_index_:selection(group);
        return index>=0?group_tabs(group)[index].shell.get():nullptr;
    }
    ShellWindow* active() { return selected(focused_); }
    size_t session_count() const { return tabs_.size()+main_tabs_.size(); }
    int tab_width() const { return std::min(140,width_/static_cast<int>(tabs_.size()+1)); }
    int main_tab_width() const { return std::max(1,std::min(140,static_cast<int>(main_group_.right-main_group_.left-62)/std::max(1,static_cast<int>(main_tabs_.size())))); }
    RECT tab_rect(Group group,int index) const {
        const RECT bar=group==Group::main?main_group_:tab_bar_;
        const int width=group==Group::main?main_tab_width():tab_width();
        return {bar.left+index*width,bar.top,bar.left+(index+1)*width,bar.bottom};
    }
    void note_focus(ShellWindow* shell) {
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group))
            if(tab.shell.get()==shell) {
                const bool right=group==Group::main && split_active_ &&
                    tab.shell.get()==main_tabs_[split_index_].shell.get();
                const bool changed=focused_!=group || (group==Group::main && split_active_ && right!=split_right_focus_);
                focused_=group;
                if(group==Group::main && split_active_) split_right_focus_=right;
                if(changed) save_workspace();
                InvalidateRect(window_,nullptr,FALSE); return;
            }
    }
    Divider divider_at(int x, int y) const;
    void drag_to(int x, int y);
    void end_drag(bool commit);
    void begin_rename(int index,Group group=Group::bottom);
    void end_rename(bool commit,bool restore_focus=true);
    bool edit_key(const MSG& msg);
    tf_workspace::WorkspaceState capture_state() {
        tf_workspace::WorkspaceState result;
        result.layout=state_;
        result.bottom.clear(); result.main.clear();
        result.bottom_active=active_; result.main_active=main_active_;
        result.split_index=split_index_; result.main_focused=focused_==Group::main;
        result.split_active=split_active_; result.split_right_focus=split_right_focus_;
        WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
        require(GetWindowPlacement(window_,&placement)!=FALSE,"workspace normal bounds query failed");
        const RECT normal=placement.rcNormalPosition;
        RECT frame{};
        require(AdjustWindowRectExForDpi(&frame,WS_OVERLAPPEDWINDOW,FALSE,0,dpi_)!=FALSE,
                "workspace frame measurement failed");
        result.window.x=normal.left; result.window.y=normal.top;
        result.window.width=logical(normal.right-normal.left-(frame.right-frame.left));
        result.window.height=logical(normal.bottom-normal.top-(frame.bottom-frame.top));
        for(const auto group:{Group::bottom,Group::main}) {
            auto& sessions=group==Group::bottom?result.bottom:result.main;
            for(const auto& tab:group_tabs(group)) {
                if(!tab.launch) throw std::runtime_error("custom shell launch cannot be persisted");
                sessions.push_back({tab.launch->kind,tab.launch->distribution,
                    tab.launch->working_directory,tab.title,tab.shell->zoom_percent()});
            }
        }
        return result;
    }
    void restore_sessions() {
        restoring_=true;
        try {
            if(settings_load_status_==tf_workspace::LoadStatus::valid_v2 ||
               settings_load_status_==tf_workspace::LoadStatus::valid_empty_v2) {
                for(const auto& session:restored_workspace_.bottom)
                    add(Group::bottom,tf_shell::make_launch({session.kind,session.distribution,session.working_directory}),&session);
                for(const auto& session:restored_workspace_.main)
                    add(Group::main,tf_shell::make_launch({session.kind,session.distribution,session.working_directory}),&session);
                active_=restored_workspace_.bottom_active; main_active_=restored_workspace_.main_active;
                split_index_=restored_workspace_.split_index;
                focused_=restored_workspace_.main_focused?Group::main:Group::bottom;
                split_active_=restored_workspace_.split_active;
                split_right_focus_=restored_workspace_.split_right_focus;
            } else add();
            restoring_=false;
            layout();
        } catch(...) { restoring_=false; throw; }
    }
    bool save_workspace() {
        if(restoring_ || settings_closed_) return false;
        try {
            const auto state=capture_state();
            if(settings_path_.empty()) settings_error_="empty settings path";
            else if(settings_read_only_) settings_error_="read-only settings";
            else if(const char* error=tf_workspace::workspace_validation_error(state); error!=nullptr)
                settings_error_=error;
            else if(!tf_workspace::save_workspace(settings_path_,state)) settings_error_="atomic write";
            else settings_error_.clear();
            settings_failed_=!settings_error_.empty();
        } catch(const std::exception& error) { settings_failed_=true; settings_error_=error.what(); }
        if(window_) InvalidateRect(window_,nullptr,FALSE);
        return !settings_failed_;
    }
    void select(int index,Group group=Group::bottom) {
        if (index<0 || index>=static_cast<int>(group_tabs(group).size())) return;
        end_rename(true);
        if(group==Group::main && state_.focus) { state_.focus=false; save_workspace(); }
        focused_=group;
        if(group==Group::main && split_active_ && split_right_focus_) split_index_=index;
        else selection(group)=index;
        layout();
        if (!closing_) SetFocus(active()->handle());
        save_workspace();
    }
    void add(Group group=Group::bottom, tf_shell::ShellLaunch launch={},
             const tf_workspace::SessionState* restored=nullptr) {
        if(workflow_) ++workflow_->attempted_creations;
        if (closing_ || session_count()>=8) return;
        const auto definition=tf_shell::known_launch(launch);
        const auto launch_id=launch.id;
        auto shell=std::make_unique<ShellWindow>("", "");
        shell->set_dpi(dpi_);
        if(restored) shell->change_zoom(restored->zoom_percent-shell->zoom_percent());
        shell->create(window_,std::move(launch));
        const int id=next_id_++;
        auto& tabs=group_tabs(group);
        const auto title=restored?restored->title:
            (launch_id.empty()?L"cmd "+std::to_wstring(id):launch_id);
        tabs.push_back({id,title,definition,std::move(shell)});
        select(static_cast<int>(tabs.size())-1,group);
    }
    void split(tf_shell::ShellLaunch launch={}) {
        if(closing_ || focused_!=Group::main || main_active_<0 || split_active_ || session_count()>=8) return;
        split_active_=true; split_right_focus_=true;
        add(Group::main,std::move(launch));
    }
    void zoom_split() {
        if(split_index_<0) return;
        split_active_=!split_active_;
        if(split_active_ && main_active_==split_index_ && main_tabs_.size()>1)
            main_active_=split_index_==0?1:0;
        split_right_focus_=split_active_;
        layout();
        if(!closing_ && active()) SetFocus(active()->handle());
        save_workspace();
    }
    void focus_split(bool right) {
        if(!split_active_) return;
        split_right_focus_=right; focused_=Group::main;
        layout(); SetFocus(active()->handle());
        save_workspace();
    }
    void close_all(bool persist=true) {
        if (closing_) return;
        end_rename(false); end_drag(false);
        if(persist && !save_workspace()) {
            settings_failed_=true;
            if(settings_error_.empty()) settings_error_="not saved";
            if(window_) InvalidateRect(window_,nullptr,FALSE);
            return;
        }
        closing_=true;
        settings_closed_=true;
        for (auto& tab:tabs_) tab.shell->close();
        for (auto& tab:main_tabs_) tab.shell->close();
        if (session_count()==0) DestroyWindow(window_);
    }
    void discard_and_close() {
        settings_discarded_=true;
        close_all(false);
    }
    tf_shell::TextActionState text_action_state(tf_shell::TextAction action) {
        const auto* shell=active();
        return tf_shell::text_action_state(action,{shell!=nullptr,closing_ || (shell && shell->closing()),
            rename_!=nullptr,shell && shell->composing(),shell && shell->paste_busy(),shell && shell->has_selection()});
    }
    bool perform_text_action(tf_shell::TextAction action) {
        // Explicit workspace commands retain the focused session target. A
        // native rename editor and a closing workspace never borrow a PTY.
        // Find EDIT keyboard input is consumed by route_find_message first.
        if(closing_ || rename_ || !active()) return false;
        return active()->perform_text_action(action);
    }
    void action(int id) {
        if (closing_) return;
        if (id==1) add();
        if (id==2 && active()) active()->close();
        if (id==3) { end_drag(false); end_rename(true); state_.focus=!state_.focus; if(state_.focus) focused_=Group::bottom; layout(); save_workspace(); SetFocus(active()?active()->handle():window_); }
        if (id==4) { end_drag(false); state_.sides=!state_.sides; layout(); save_workspace(); }
        if (id==5) close_all();
        if (id==17) discard_and_close();
        if (id==6) begin_rename(selection(focused_),focused_);
        if (id==7) { end_rename(false); end_drag(false); state_={}; layout(); save_workspace(); }
        if (id==8) add(Group::main);
        if (id==12) perform_text_action(tf_shell::TextAction::paste);
        if (id==13) perform_text_action(tf_shell::TextAction::cancel_paste);
        if (id==14) perform_text_action(tf_shell::TextAction::copy);
        if (id==15) perform_text_action(tf_shell::TextAction::select_all);
        if (id==16) perform_text_action(tf_shell::TextAction::find);
        if (!rename_ && active() && id>=9 && id<=11) {
            if(id==11) active()->reset_zoom(); else active()->change_zoom(id==9?25:-25);
            save_workspace();
            InvalidateRect(window_,nullptr,FALSE);
        }
    }
    bool shortcut(WPARAM key,bool ctrl,bool shift=false,bool alt=false) {
        if(closing_ || rename_) return false;
        if(key==VK_ESCAPE && dragging_!=Divider::none) { end_drag(false); return true; }
        if(key==VK_F2) { action(6); return true; }
        if(alt && !ctrl && key==VK_OEM_MINUS) { split(); return true; }
        if(alt && !ctrl && key=='Z') { zoom_split(); return true; }
        if(alt && !ctrl && key=='X') { action(2); return true; }
        if(alt && !ctrl && key==VK_LEFT) { focus_split(false); return true; }
        if(alt && !ctrl && key==VK_RIGHT) { focus_split(true); return true; }
        if(ctrl && key=='T') { action(shift?8:1); return true; }
        if(ctrl && shift && key=='W') { action(17); return true; }
        if(ctrl && key=='W') { action(2); return true; }
        if(ctrl && (key==VK_OEM_PLUS || key==VK_ADD)) { action(9); return true; }
        if(ctrl && (key==VK_OEM_MINUS || key==VK_SUBTRACT)) { action(10); return true; }
        if(ctrl && key=='0') { action(11); return true; }
        if(ctrl && key==VK_TAB && !group_tabs(focused_).empty()) {
            const int count=static_cast<int>(group_tabs(focused_).size());
            select((selection(focused_)+(shift?count-1:1))%count,focused_); return true;
        }
        if(key==VK_F11) { action(3); return true; }
        return false;
    }
    void layout() {
        RECT area{}; GetClientRect(window_,&area); physical_width_=area.right; physical_height_=area.bottom;
        width_=logical(physical_width_); height_=logical(physical_height_);
        // Layout and hit testing share 96-DIP coordinates; round rectangle edges
        // once when crossing the HWND/GDI boundary, avoiding cumulative gaps.
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
        const int center_left=state_.sides?left_+1:0, center_right=state_.sides?width_-right_-1:width_;
        main_group_={center_left,33,center_right,main_tabs_.empty()?52:59};
        main_toolbar_={center_left,main_group_.bottom,center_right,main_group_.bottom+(main_tabs_.empty()?36:34)};
        main_address_={center_left+103,main_toolbar_.top+4,center_right-30,main_toolbar_.bottom-4};
        main_terminal_={center_left,main_toolbar_.bottom,center_right,34+upper-1};
        for(size_t i=0;i<tabs_.size();++i) {
            auto handle=tabs_[i].shell->handle();
            if (!handle) continue;
            place(handle,terminal_,false);
            tabs_[i].shell->sync_client_size();
            ShowWindow(handle,static_cast<int>(i)==active_?SW_SHOWNA:SW_HIDE);
        }
        for(size_t i=0;i<main_tabs_.size();++i) {
            auto handle=main_tabs_[i].shell->handle();
            if(!handle) continue;
            RECT target=main_terminal_;
            bool visible=!state_.focus && static_cast<int>(i)==main_active_;
            if(split_active_ && split_index_>=0) {
                const LONG middle=(main_terminal_.left+main_terminal_.right)/2;
                target.right=middle;
                if(static_cast<int>(i)==split_index_) target={middle,main_terminal_.top,main_terminal_.right,main_terminal_.bottom};
                visible=!state_.focus && (static_cast<int>(i)==main_active_ || static_cast<int>(i)==split_index_);
            }
            if(visible) {
                place(handle,target,false);
                main_tabs_[i].shell->sync_client_size();
            }
            ShowWindow(handle,visible?SW_SHOWNA:SW_HIDE);
        }
        if(rename_) {
            auto& tabs=group_tabs(rename_group_);
            for(size_t i=0;i<tabs.size();++i) if(tabs[i].id==rename_id_) {
                place(rename_,rename_rect(rename_group_,static_cast<int>(i)));
            }
        }
        InvalidateRect(window_,nullptr,FALSE);
    }
    void fill(HDC dc, RECT r, COLORREF color) const {
        r=physical(r);
        HBRUSH brush=CreateSolidBrush(color); FillRect(dc,&r,brush); DeleteObject(brush);
    }
    void label(HDC dc, RECT r, const std::wstring& text, COLORREF color=foreground) const {
        SetTextColor(dc,color); r.left+=8; r=physical(r);
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void pane(HDC dc, RECT r, const std::wstring& title) {
        fill(dc,r,background); RECT header{r.left,r.top,r.right,r.top+24};
        fill(dc,header,RGB(51,51,51)); label(dc,header,title);
    }
    std::wstring settings_status_text() const {
        if(settings_read_only_) return L"Workspace settings not overwritten";
        std::wstring result=L"Workspace could not be saved";
        if(!settings_error_.empty()) result+=L": "+utf16(settings_error_.c_str());
        return result;
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
            fill(dc,main_group_,RGB(51,51,51));
            for(size_t i=0;i<main_tabs_.size();++i) {
                const RECT tab=tab_rect(Group::main,static_cast<int>(i));
                fill(dc,tab,static_cast<int>(i)==main_active_?background:RGB(51,51,51));
                RECT title=tab; title.right-=25;
                label(dc,title,main_tabs_[i].title+(main_tabs_[i].shell->closing()?L" ...":L""));
                if(static_cast<int>(i)==main_active_) {
                    fill(dc,{tab.left,tab.bottom-2,tab.right,tab.bottom},accent);
                    label(dc,{tab.right-25,tab.top,tab.right,tab.bottom},L"x");
                }
            }
            label(dc,{main_group_.right-30,main_group_.top,main_group_.right,main_group_.bottom},L"+");
            fill(dc,main_toolbar_,RGB(35,35,35));
            label(dc,{center_left,main_toolbar_.top,center_left+30,main_toolbar_.bottom},L"+");
            label(dc,{center_left+30,main_toolbar_.top,center_left+60,main_toolbar_.bottom},L"x",main_tabs_.empty()?RGB(100,100,100):foreground);
            label(dc,{center_left+60,main_toolbar_.top,center_left+103,main_toolbar_.bottom},L"F2",main_tabs_.empty()?RGB(100,100,100):foreground);
            fill(dc,main_address_,background);
            label(dc,main_address_,main_tabs_.empty()?L"Local session":L"Local / cmd.exe");
            if(main_tabs_.empty()) {
                label(dc,{center_left+24,110,center_right-24,146},L"Open a local session here with Ctrl+Shift+T or +.");
                label(dc,{center_left+24,146,center_right-24,178},L"Ctrl+T  New bottom shell     Ctrl+Tab  Switch in focused area");
            }
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
                for(size_t i=0;i<main_tabs_.size();++i) {
                    RECT row{width_-right_,57+static_cast<int>(tabs_.size()+i)*26,width_,83+static_cast<int>(tabs_.size()+i)*26};
                    if(row.bottom>right_middle_) break;
                    if(static_cast<int>(i)==main_active_) fill(dc,row,RGB(42,55,65));
                    label(dc,row,L"Main: "+main_tabs_[i].title);
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
        const auto settings_error=settings_status_text();
        label(dc,{0,height_-29,width_,height_},closing_?L"Closing sessions...":settings_failed_?settings_error:L"LOCAL     "+std::to_wstring(session_count())+L" sessions     Focus: "+(focused_==Group::main?L"Main":L"Shell")+L"     Zoom: "+std::to_wstring(active()?active()->zoom_percent():100)+L"%     "+(active()?active()->status_text():L""));
        SelectObject(dc,old);
    }
    void click(int x,int y) {
        if(y<33) {
            HMENU menu=CreatePopupMenu();
            if(x<82) { AppendMenuW(menu,MF_STRING,8,L"New main session\tCtrl+Shift+T"); AppendMenuW(menu,MF_STRING,1,L"New shell\tCtrl+T"); AppendMenuW(menu,MF_STRING,2,L"Close focused session\tCtrl+W"); AppendMenuW(menu,MF_STRING,6,L"Rename focused session\tF2"); AppendMenuW(menu,MF_STRING,5,L"Exit"); AppendMenuW(menu,MF_STRING,17,L"Exit without saving\tCtrl+Shift+W"); }
            else { AppendMenuW(menu,MF_STRING|(state_.focus?MF_CHECKED:0),3,L"Focus shell\tF11"); AppendMenuW(menu,MF_STRING|(state_.sides?MF_CHECKED:0),4,L"Side panels"); AppendMenuW(menu,MF_STRING,7,L"Reset layout"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr); const UINT enabled=active()&&!rename_?MF_STRING:MF_STRING|MF_GRAYED; AppendMenuW(menu,enabled,9,L"Zoom in\tCtrl++"); AppendMenuW(menu,enabled,10,L"Zoom out\tCtrl+-"); AppendMenuW(menu,enabled,11,L"Reset zoom\tCtrl+0"); }
            if(x<82) {
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                const auto flags=[&](tf_shell::TextAction action) { return MF_STRING|(text_action_state(action).enabled?0:MF_GRAYED); };
                AppendMenuW(menu,flags(tf_shell::TextAction::paste),12,L"Paste\tCtrl+Shift+V");
                AppendMenuW(menu,flags(tf_shell::TextAction::cancel_paste),13,L"Cancel paste\tEscape");
                AppendMenuW(menu,flags(tf_shell::TextAction::copy),14,L"Copy\tCtrl+Shift+C");
                AppendMenuW(menu,flags(tf_shell::TextAction::select_all),15,L"Select All\tCtrl+Shift+A");
                AppendMenuW(menu,flags(tf_shell::TextAction::find),16,L"Find\tCtrl+Shift+F");
            }
            POINT at{physical(x),physical(33)}; ClientToScreen(window_,&at);
            tf_shell_test::observe_popup_open(window_,menu);
            const int selected=TrackPopupMenu(menu,TPM_RETURNCMD,at.x,at.y,0,window_,nullptr);
            tf_shell_test::observe_popup_result(window_,menu,selected);
            DestroyMenu(menu); action(selected); return;
        }
        if(!state_.focus && inside(main_group_,x,y)) {
            if(x>=main_group_.right-30) add(Group::main);
            else if(!main_tabs_.empty()) {
                const int index=(x-main_group_.left)/main_tab_width();
                if(index<static_cast<int>(main_tabs_.size())) {
                    const auto tab=tab_rect(Group::main,index);
                    if(index==main_active_ && x>=tab.right-25) { select(index,Group::main); action(2); }
                    else select(index,Group::main);
                }
            }
        } else if(!state_.focus && inside(main_toolbar_,x,y)) {
            const int relative=x-main_toolbar_.left;
            if(relative<30) add(Group::main);
            else if(relative<103 && main_active_>=0) { select(main_active_,Group::main); action(relative<60?2:6); }
        } else if(inside(tab_bar_,x,y)) {
            int index=x/tab_width();
            if(index==static_cast<int>(tabs_.size())) add();
            else if(index==active_ && x%tab_width()>=tab_width()-25) { select(index); action(2); }
            else select(index);
        } else if(y>=height_-56 && y<height_-29 && x<140) add();
        else if(state_.sides && !state_.focus && x>=width_-right_ && y>=57) {
            const int index=(y-57)/26;
            if(83+index*26<=right_middle_) {
                if(index<static_cast<int>(tabs_.size())) select(index);
                else select(index-static_cast<int>(tabs_.size()),Group::main);
            }
        }
    }
    void scenario();
    void scenario_edit();
    void scenario_restore();
    bool workspace_data_mode() const {
        return test_mode_.rfind("ui-workspace-",0)==0;
    }
    std::string workspace_geometry();
    void save_workspace_geometry();
    void scenario_workspace();
    void scenario_main();
    void scenario_dpi();
    void scenario_monitors();
    void scenario_shells();
    void scenario_launch_failure();
    void scenario_split();
    void scenario_history();
    void scenario_keyboard();
    void scenario_paste();
    void scenario_clipboard();
    void scenario_selection();
    void scenario_selection_bytes();
    void scenario_selection_context();
    void scenario_actions();
    void scenario_ime();
    void scenario_workflow();
    ShellWindow* workflow_shell(const std::string& name);
    void workflow_select(const std::string& name);
    void workflow_checkpoint(const char* phase,unsigned cycle=0,const std::vector<std::string>& names={});
    void workflow_resource_mark(const char* phase,unsigned cycle);
    void workflow_attach(const std::string& name,const std::string& role,Group group,bool create);
    void workflow_collected(ShellWindow* child,int id);
    void workflow_write();
    void workflow_system_initialize();
    std::string workflow_system_identity(const std::string& name);
    void workflow_system_event(const std::string& kind,const std::string& fields);
    void workflow_system_shortcut(const std::string& name,bool copy);
    HWND workflow_system_prompt();
    void workflow_system_point(const char* phase,bool clipboard=false,bool prompt=false,bool ime=false);
    bool workflow_system_tick();
    void workflow_system_finish();
    std::string workflow_operation_top();
    void workflow_operation_arm(uint64_t id,UINT command);
    bool workflow_operation_complete(uint64_t id);
    void scenario_find();
    void scenario_find_stream();
    static std::string size_marker(ShellWindow* shell) {
        RECT outer{};require(GetWindowRect(shell->handle(),&outer)!=FALSE,"workspace terminal bounds query failed");
        const auto [cell_width,cell_height]=shell->pixel_cell_size();
        const int bar=GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(shell->handle()));
        require(cell_width>0 && cell_height>0 && outer.right>outer.left && outer.bottom>outer.top,"workspace terminal cell/bounds invalid");
        return "TF_SIZE_"+std::to_string((outer.right-outer.left-bar)/cell_width)+"x"+std::to_string((outer.bottom-outer.top)/cell_height);
    }
    void record_geometry(const char* phase);
    void collect_child(ShellWindow* child);
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<Workspace*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) { self=static_cast<Workspace*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); self->window_=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if(!self) return DefWindowProcW(hwnd,message,wp,lp);
        try {
            if(message==WM_CLOSE) { self->close_all(); return 0; }
            if(message==WM_DESTROY) { self->window_=nullptr; PostQuitMessage(0); return 0; }
            if(message==WM_SIZE && wp!=SIZE_MINIMIZED) { self->layout(); return 0; }
            if(message==WM_EXITSIZEMOVE) { self->save_workspace(); return 0; }
            if(message==WM_DPICHANGED) {
                const HWND focus=GetFocus();
                self->set_dpi(LOWORD(wp));
                const RECT& suggested=*reinterpret_cast<const RECT*>(lp);
                require(SetWindowPos(hwnd,nullptr,suggested.left,suggested.top,suggested.right-suggested.left,
                    suggested.bottom-suggested.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"workspace DPI resize failed");
                self->layout();
                if(focus && IsWindow(focus) && IsChild(hwnd,focus)) SetFocus(focus);
                return 0;
            }
            if(message==WM_GETMINMAXINFO) { auto m=reinterpret_cast<MINMAXINFO*>(lp); m->ptMinTrackSize={std::min(6020,self->physical(1000)),std::min(5300,self->physical(650))}; m->ptMaxTrackSize={6020,5300}; return 0; }
            if(message==WM_SETFOCUS && self->active()) { SetFocus(self->active()->handle()); return 0; }
            if(message==WM_APP+2) { self->note_focus(reinterpret_cast<ShellWindow*>(lp)); return 0; }
            if(message==WM_APP+4) {
                if(!self->closing_) SetFocus(self->rename_?self->rename_:(self->active()?self->active()->handle():hwnd));
                return 0;
            }
            if(message==WM_COMMAND) {
                if(reinterpret_cast<HWND>(lp)==self->rename_ && self->rename_ && HIWORD(wp)==EN_KILLFOCUS) self->end_rename(true,false);
                else self->action(LOWORD(wp));
                return 0;
            }
            if(message==WM_LBUTTONDOWN) {
                const int x=self->hit_x(static_cast<short>(LOWORD(lp))), y=self->hit_y(static_cast<short>(HIWORD(lp)));
                const auto divider=self->divider_at(x,y);
                self->end_rename(true);
                if(divider!=Divider::none && !self->closing_) { self->drag_start_=self->state_; self->dragging_=divider; SetCapture(hwnd); }
                else self->click(x,y);
                return 0;
            }
            if(message==WM_MOUSEMOVE && self->dragging_!=Divider::none) { self->drag_to(self->hit_x(static_cast<short>(LOWORD(lp))),self->hit_y(static_cast<short>(HIWORD(lp)))); return 0; }
            if(message==WM_LBUTTONUP && self->dragging_!=Divider::none) { self->drag_to(self->hit_x(static_cast<short>(LOWORD(lp))),self->hit_y(static_cast<short>(HIWORD(lp)))); self->end_drag(true); return 0; }
            if(message==WM_CAPTURECHANGED) { self->end_drag(false); return 0; }
            if(message==WM_MBUTTONDOWN) {
                const int x=self->hit_x(static_cast<short>(LOWORD(lp))), y=self->hit_y(static_cast<short>(HIWORD(lp)));
                if(inside(self->tab_bar_,x,y)) self->begin_rename(x/self->tab_width());
                else if(!self->state_.focus && inside(self->main_group_,x,y))
                    self->begin_rename((x-self->main_group_.left)/self->main_tab_width(),Group::main);
                return 0;
            }
            if(message==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
                POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd,&point);
                const auto divider=self->divider_at(self->hit_x(point.x),self->hit_y(point.y));
                if(divider!=Divider::none) { SetCursor(LoadCursorW(nullptr,divider==Divider::left || divider==Divider::right?IDC_SIZEWE:IDC_SIZENS)); return TRUE; }
            }
            if(message==WM_TIMER) {
                if(self->test_mode_=="ui-edit") self->scenario_edit();
                else if(self->test_mode_=="ui-restore") self->scenario_restore();
                else if(self->workspace_data_mode()) self->scenario_workspace();
                else if(self->test_mode_=="ui-main") self->scenario_main();
                else if(self->test_mode_=="ui-dpi") self->scenario_dpi();
                else if(self->test_mode_=="ui-monitors") self->scenario_monitors();
                else if(self->test_mode_=="ui-shells") self->scenario_shells();
                else if(self->test_mode_=="ui-launch-failure") self->scenario_launch_failure();
                else if(self->test_mode_=="ui-split") self->scenario_split();
                else if(self->test_mode_=="ui-history") self->scenario_history();
                else if(self->test_mode_=="ui-keyboard") self->scenario_keyboard();
                else if(self->test_mode_=="ui-paste" || self->test_mode_=="ui-paste-system") self->scenario_paste();
                else if(self->test_mode_=="ui-clipboard-system") self->scenario_clipboard();
                else if(self->test_mode_=="ui-selection" || self->test_mode_=="ui-copy-system") self->scenario_selection();
                else if(self->test_mode_=="ui-selection-bytes" || self->test_mode_=="ui-copy-bytes-system") self->scenario_selection_bytes();
                else if(self->test_mode_=="ui-selection-context" || self->test_mode_=="ui-copy-context-system") self->scenario_selection_context();
                else if(self->test_mode_=="ui-actions" || self->test_mode_=="ui-actions-system") self->scenario_actions();
                else if(self->workflow_) self->scenario_workflow();
                else if(self->test_mode_=="ui-find") self->scenario_find();
                else if(self->test_mode_=="ui-find-stream") self->scenario_find_stream();
                else if(tf_shell_test::is_product_ime_mode(utf16(self->test_mode_.c_str()))) self->scenario_ime();
                else self->scenario();
                return 0;
            }
            if(message==WM_APP+1) {
                self->collect_child(reinterpret_cast<ShellWindow*>(lp)); return 0;
            }
            if(message==WM_ERASEBKGND) return 1;
            if(message==WM_PAINT) {
                PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
                try { self->paint(dc); } catch(...) { EndPaint(hwnd,&ps); throw; }
                EndPaint(hwnd,&ps); return 0;
            }
        } catch(const std::exception& error) {
            self->failure_=error.what();
            // Workspace scenarios (selection, actions, shells) fail outside the
            // per-tab dump, so report the scenario identity and the message here.
            std::cerr << "workspace-failure mode=" << self->test_mode_ << " what=" << self->failure_ << '\n';
            // Per-tab counters: which session was waiting matters more than the text.
            for (const auto group : {Group::bottom, Group::main}) {
                auto& tabs = self->group_tabs(group);
                for (size_t index = 0; index < tabs.size(); ++index) {
                    if (!tabs[index].shell) continue;
                    std::cerr << "  tab " << (group == Group::main ? "main" : "bottom") << ':' << index << ' '
                              << tabs[index].shell->diagnostic_line() << '\n';
                }
            }
            self->discard_and_close();
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
public:
    explicit Workspace(std::string image="",std::string mode="ui"):test_mode_(std::move(mode)),image_(std::move(image)) {
        if(test_mode_=="ui-workflow-sessions" || test_mode_=="ui-workflow-system") {
            const bool system=test_mode_=="ui-workflow-system";
            if(system) {tf_shell_test::require_hosted_ime_input();workflow_system_=std::make_unique<tf_shell_test::WorkflowSystemState>();}
            workflow_=std::make_unique<tf_shell_test::WorkflowScenarioState>(system);
        }
        if(!image_.empty()) settings_path_=std::filesystem::path(image_).parent_path()/"workspace.layout";
        else {
            wchar_t base[32768]{};
            DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
            if(n && n<32768) settings_path_=std::filesystem::path(base)/L"TermForge"/L"workspace-v1.layout";
        }
        const auto loaded=tf_workspace::load_workspace(settings_path_);
        settings_load_status_=loaded.status;
        restored_workspace_=loaded.state;
        state_=restored_workspace_.layout;
        settings_read_only_=loaded.status==tf_workspace::LoadStatus::unsupported_version ||
            loaded.status==tf_workspace::LoadStatus::io_error;
    }
    ~Workspace() { end_rename(false); tabs_.clear(); main_tabs_.clear(); if(window_) DestroyWindow(window_); UnregisterClassW(L"TermForgeWorkspace",GetModuleHandleW(nullptr)); }
    int run() {
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"TermForgeWorkspace"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        require(RegisterClassW(&cls)!=0,"workspace class failed");
        dpi_=static_cast<int>(GetDpiForSystem()); if(!dpi_) dpi_=96;
        ui_font_=std::make_unique<WorkspaceFont>(dpi_); font_=ui_font_->handle();
        // The test starts smaller, reproducing creation-time size clamping.
        RECT rect{0,0,physical(image_.empty()?1360:1000),physical(image_.empty()?860:650)};
        require(AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,0,dpi_)!=FALSE,"workspace frame calculation failed");
        require(CreateWindowExW(0,cls.lpszClassName,L"TermForge",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,this)!=nullptr,"workspace creation failed");
        // Creation can query track limits before WM_NCCREATE installs `self`.
        // Apply the desired size once our window procedure owns the HWND.
        const UINT actual_dpi=GetDpiForWindow(window_); require(actual_dpi!=0,"workspace DPI query failed");
        set_dpi(static_cast<int>(actual_dpi));
        const auto& restored=restored_workspace_.window;
        rect={0,0,physical(restored.width),physical(restored.height)};
        require(AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,0,dpi_)!=FALSE,"workspace restored frame calculation failed");
        RECT desired{restored.x,restored.y,restored.x+rect.right-rect.left,restored.y+rect.bottom-rect.top};
        MONITORINFO monitor{sizeof(MONITORINFO)};
        require(GetMonitorInfoW(MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,
                "workspace monitor query failed");
        const RECT work=monitor.rcWork;
        const LONG outer_width=std::min(desired.right-desired.left,work.right-work.left);
        const LONG outer_height=std::min(desired.bottom-desired.top,work.bottom-work.top);
        const LONG x=std::clamp<LONG>(restored.x,work.left,std::max(work.left,work.right-outer_width));
        const LONG y=std::clamp<LONG>(restored.y,work.top,std::max(work.top,work.bottom-outer_height));
        const LONG frame_width=rect.right-rect.left-physical(restored.width);
        const LONG frame_height=rect.bottom-rect.top-physical(restored.height);
        const int expected_width=logical(outer_width-frame_width);
        const int expected_height=logical(outer_height-frame_height);
        require(SetWindowPos(window_,nullptr,x,y,outer_width,outer_height,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
                "workspace restored bounds failed");
        layout();
        require(width_==expected_width && height_==expected_height,
                ("workspace restored client size mismatch: expected="+
                 std::to_string(expected_width)+"x"+std::to_string(expected_height)+" actual="+
                 std::to_string(width_)+"x"+std::to_string(height_)+" saved="+
                 std::to_string(restored.width)+"x"+std::to_string(restored.height)+
                 " dpi="+std::to_string(dpi_)+" outer="+std::to_string(outer_width)+"x"+
                 std::to_string(outer_height)+" frame="+std::to_string(frame_width)+"x"+
                 std::to_string(frame_height)).c_str());
        restore_sessions();
        if(!image_.empty()) require(SetTimer(window_,1,30,nullptr)!=0,"workspace timer failed");
        ShowWindow(window_,image_.empty()?SW_SHOW:SW_SHOWNOACTIVATE);
        MSG msg{}; BOOL status;
        while((status=GetMessageW(&msg,nullptr,0,0))>0) {
            bool find_message=false;
            for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group))
                if(tab.shell->route_find_message(msg)) find_message=true;
            if(find_message) continue;
            bool paste_message=false;
            for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group))
                if(tab.shell->route_paste_message(msg)) paste_message=true;
            if(paste_message) continue;
            if(edit_key(msg)) continue;
            const auto mods=ShellWindow::key_modifiers();
            if(msg.message==WM_KEYDOWN && !(active() && active()->composing()) && shortcut(msg.wParam,mods.control,mods.shift,mods.alt)) continue;
            ShellWindow::dispatch_message(msg);
        }
        require(status==0,"workspace message loop failed"); require(failure_.empty(),failure_.c_str());
        if(!image_.empty() && workspace_data_mode()) {
            const int expected_closed=test_mode_=="ui-workspace-empty"?0:
                test_mode_=="ui-workspace-save" || test_mode_=="ui-workspace-restore"?3:1;
            const bool protected_settings=test_mode_=="ui-workspace-future" || test_mode_=="ui-workspace-close-failure";
            require(stage_==100 && collected_==expected_closed,"workspace persistence scenario incomplete");
            require(settings_failed_==protected_settings && settings_discarded_==protected_settings,
                    "workspace persistence save result mismatch");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":"<<expected_closed
                <<",\"active_children_after\":0,\"settings_save_failed\":"<<(settings_failed_?"true":"false");
            if(protected_settings) std::cout<<",\"configuration_preserved\":true,\"settings_discarded\":true";
            if(test_mode_=="ui-workspace-write-failure") std::cout<<",\"retry_saved\":true";
            std::cout<<",\"presented_done\":null}\n";
            return 0;
        }
        if(!image_.empty() && workflow_) {
            require(stage_==100 && collected_==18,"workflow sessions scenario incomplete");
            if(workflow_system_) require(workflow_system_->points.size()==13 && workflow_system_->journal_rows==28,"workflow system observations incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":18,\"active_children_after\":0,\"presented_done\":null}\n";
            return 0;
        }
        if(!image_.empty() && test_mode_=="ui") {
            require(stage_==10 && collected_==3,"workspace scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui\",\"sessions_closed\":3,\"isolation_verified\":true,\"resize_verified\":true,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-main") {
            require(stage_==100 && collected_==5,"workspace main scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-main\",\"main_sessions_verified\":true,\"focus_verified\":true,\"geometry_verified\":true,\"ui_font_family\":\""<<utf8(ui_font_->face().c_str())<<"\",\"sessions_closed\":5,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-dpi") {
            require(stage_==100 && collected_==3,"workspace DPI scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-dpi\",\"dpi_and_zoom_verified\":true,\"sessions_closed\":3,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-monitors") {
            require(stage_==100 && collected_==3,"workspace monitor scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-monitors\",\"real_monitor_transition\":true,\"sessions_closed\":3,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-shells") {
            require(stage_==100 && collected_==4,"workspace local shells scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-shells\",\"shells_verified\":4,\"sessions_closed\":4,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-launch-failure") {
            require(stage_==100 && collected_==3,"workspace launch failure scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-launch-failure\",\"launch_failure_visible\":true,\"recovered\":true,\"sessions_closed\":3,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-split") {
            require(stage_==100 && collected_==3,"workspace split scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-split\",\"split_verified\":true,\"sessions_closed\":3,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-history") {
            require(stage_==100 && collected_==3,"workspace history scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-history\",\"sessions_closed\":3,\"history_verified\":true,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && test_mode_=="ui-keyboard") {
            require(stage_==100 && collected_==1,"workspace keyboard scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\"ui-keyboard\",\"sessions_closed\":1,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && (test_mode_=="ui-paste" || test_mode_=="ui-paste-system" || test_mode_=="ui-clipboard-system")) {
            require(stage_==100 && collected_==2,"workspace paste scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":2,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && (test_mode_=="ui-selection" || test_mode_=="ui-copy-system" || test_mode_=="ui-find" || test_mode_=="ui-find-stream" || test_mode_=="ui-selection-bytes" || test_mode_=="ui-copy-bytes-system" || test_mode_=="ui-actions" || test_mode_=="ui-actions-system")) {
            require(stage_==100 && collected_==2,"workspace selection scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":2,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && tf_shell_test::is_product_ime_mode(utf16(test_mode_.c_str()))) {
            const int expected=test_mode_=="ui-ime-focus"?3:2;
            require(stage_==100 && collected_==expected,"workspace IME scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":"<<expected<<",\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && (test_mode_=="ui-selection-context" || test_mode_=="ui-copy-context-system")) {
            require(stage_==100 && collected_==3,"workspace selection context incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"sessions_closed\":3,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        if(!image_.empty() && !tf_shell_test::is_product_ime_mode(utf16(test_mode_.c_str())) && test_mode_!="ui" && test_mode_!="ui-main" && test_mode_!="ui-dpi" && test_mode_!="ui-monitors" && test_mode_!="ui-shells" && test_mode_!="ui-launch-failure" && test_mode_!="ui-split" && test_mode_!="ui-history" && test_mode_!="ui-keyboard" && test_mode_!="ui-paste" && test_mode_!="ui-paste-system" && test_mode_!="ui-clipboard-system" && test_mode_!="ui-selection" && test_mode_!="ui-copy-system" && test_mode_!="ui-find" && test_mode_!="ui-find-stream" && test_mode_!="ui-selection-bytes" && test_mode_!="ui-copy-bytes-system" && test_mode_!="ui-selection-context" && test_mode_!="ui-copy-context-system" && test_mode_!="ui-actions" && test_mode_!="ui-actions-system") {
            require(stage_==100 && collected_==1,"workspace interaction scenario incomplete");
            std::cout<<"{\"valid\":true,\"mode\":\""<<test_mode_<<"\",\"interactions_verified\":true,\"sessions_closed\":1,\"active_children_after\":0,\"presented_done\":null}\n";
        }
        return 0;
    }
};
#include "workspace_interactions.hpp"
#include "workspace_scenarios.hpp"
#include "workspace_persistence_scenario.hpp"
#include "workspace_main_scenario.hpp"
#include "workspace_dpi_scenario.hpp"
#include "workspace_monitor_scenario.hpp"
#include "workspace_shells_scenario.hpp"
#include "workspace_launch_failure_scenario.hpp"
#include "workspace_split_scenario.hpp"
#include "workspace_history_scenario.hpp"
#include "workspace_keyboard_scenario.hpp"
#include "workspace_paste_scenario.hpp"
#include "workspace_clipboard_scenario.hpp"
#include "workspace_selection_scenario.hpp"
#include "workspace_selection_bytes_scenario.hpp"
#include "workspace_selection_context_scenario.hpp"
#include "workspace_actions_scenario.hpp"
#include "workspace_find_scenario.hpp"
#include "workspace_ime_scenario.hpp"
#include "workspace_workflow_scenario.hpp"
#include "workspace_workflow_system.hpp"
