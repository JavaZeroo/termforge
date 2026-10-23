// Independent workspace interactions; no WindTerm code or assets.
#pragma once
#include <cwctype>
#include "edit_actions.hpp"

inline Workspace::Divider Workspace::divider_at(int x,int y) const {
    if(state_.focus || closing_) return Divider::none;
    const int bottom=tab_bar_.top-25;
    if(x>=0 && x<width_ && std::abs(y-bottom)<=2) return Divider::bottom;
    if(!state_.sides || y<33 || y>=bottom) return Divider::none;
    if(std::abs(x-left_)<=2) return Divider::left;
    if(std::abs(x-(width_-right_-1))<=2) return Divider::right;
    if(x>=0 && x<left_ && std::abs(y-left_middle_)<=2) return Divider::left_middle;
    if(x>=width_-right_ && x<width_ && std::abs(y-right_middle_)<=2) return Divider::right_middle;
    return Divider::none;
}

inline void Workspace::drag_to(int x,int y) {
    if(dragging_==Divider::left) state_.left=std::clamp(x/double(width_-2),.1,.35);
    if(dragging_==Divider::right) state_.right=std::clamp((width_-x-1)/double(width_-2),.1,.35);
    if(dragging_==Divider::bottom) state_.upper=std::clamp((y-33)/double(height_-91),.25,.8);
    if(dragging_==Divider::left_middle) state_.left_top=std::clamp((y-33)/double(tab_bar_.top-59),.2,.8);
    if(dragging_==Divider::right_middle) state_.right_top=std::clamp((y-33)/double(tab_bar_.top-59),.2,.8);
    layout();
}

inline void Workspace::end_drag(bool commit) {
    if(dragging_==Divider::none) return;
    dragging_=Divider::none;
    if(GetCapture()==window_) ReleaseCapture();
    if(commit) save_layout();
    else { state_=drag_start_; layout(); }
}

inline void Workspace::begin_rename(int index,Group group) {
    auto& tabs=group_tabs(group);
    if(closing_ || index<0 || index>=static_cast<int>(tabs.size()) || tabs[index].shell->closing()) return;
    select(index,group);
    rename_group_=group; rename_id_=tabs[index].id;
    const RECT rect=physical(rename_rect(group,index));
    rename_=CreateWindowExW(0,L"EDIT",tabs[index].title.c_str(),
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,rect.left,rect.top,
        rect.right-rect.left,rect.bottom-rect.top,window_,reinterpret_cast<HMENU>(100),GetModuleHandleW(nullptr),nullptr);
    require(rename_!=nullptr,"rename editor creation failed");
    SendMessageW(rename_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
    SendMessageW(rename_,EM_SETLIMITTEXT,64,0);
    SendMessageW(rename_,EM_SETSEL,0,-1);
    SetFocus(rename_);
}

inline void Workspace::end_rename(bool commit,bool restore_focus) {
    if(!rename_) return;
    wchar_t buffer[65]{};
    if(commit) GetWindowTextW(rename_,buffer,65);
    HWND editor=rename_; rename_=nullptr;
    const int id=rename_id_; rename_id_=-1;
    DestroyWindow(editor); // EN_KILLFOCUS cannot recursively commit this editor.
    if(commit) {
        std::wstring title(buffer);
        while(!title.empty() && std::iswspace(title.back())) title.pop_back();
        const auto first=title.find_first_not_of(L" \t\r\n");
        if(first!=std::wstring::npos) title.erase(0,first);
        // Reject blank/control characters and incomplete UTF-16, including a
        // surrogate pair cut by the edit limit. Names never enter the PTY.
        bool valid=!title.empty() && title.size()<=64 &&
            std::none_of(title.begin(),title.end(),[](wchar_t c){return c<32 || c==127;});
        if(valid) valid=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,title.data(),static_cast<int>(title.size()),nullptr,0,nullptr,nullptr)>0;
        if(valid) for(auto& tab:group_tabs(rename_group_)) if(tab.id==id && !tab.shell->closing()) tab.title=title;
    }
    if(window_) InvalidateRect(window_,nullptr,FALSE);
    if(restore_focus && active() && !closing_) SetFocus(active()->handle());
}

inline bool Workspace::edit_key(const MSG& msg) {
    if(!rename_ || msg.hwnd!=rename_ || msg.message!=WM_KEYDOWN) return false;
    if(tf_shell::route_edit_select_all(msg,rename_)) return true;
    if((msg.wParam==VK_RETURN || msg.wParam==VK_ESCAPE) && !tf_shell::native_edit_composing(rename_)) {
        end_rename(msg.wParam==VK_RETURN); return true;
    }
    return false;
}

inline void Workspace::collect_child(ShellWindow* child) {
    for(const auto group:{Group::bottom,Group::main}) {
        auto& tabs=group_tabs(group);
        for(size_t i=0;i<tabs.size();++i) if(tabs[i].shell.get()==child) {
            const bool removed_focused=group==focused_ && static_cast<int>(i)==selection(group);
            if(rename_id_==tabs[i].id) end_rename(false);
            try {
                const Result result=child->collect();
                require(result.complete && result.active_children==0,"workspace child cleanup failed");
            } catch(const std::exception& error) { failure_=error.what(); }
            if(workflow_) {
                try { workflow_collected(child,tabs[i].id); }
                catch(const std::exception& error) { failure_=error.what(); }
            }
            int& index=selection(group);
            if(static_cast<int>(i)<index) --index;
            tabs.erase(tabs.begin()+i); ++collected_;
            index=tabs.empty()?-1:std::clamp(index,0,static_cast<int>(tabs.size())-1);
            if(!failure_.empty() && !closing_) close_all();
            else if(closing_ && session_count()==0) DestroyWindow(window_);
            else {
                if(removed_focused && !active()) {
                    focused_=group==Group::main?Group::bottom:Group::main;
                    if(active() && focused_==Group::main && state_.focus) { state_.focus=false; save_layout(); }
                }
                layout();
                // A background exit never changes the other area's keyboard
                // target, nor commits/cancels an unrelated rename editor.
                if(rename_) SetFocus(rename_);
                else if(removed_focused && !closing_) SetFocus(active()?active()->handle():window_);
            }
            return;
        }
    }
}
