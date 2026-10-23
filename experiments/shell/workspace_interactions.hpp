// Independent workspace interactions; no WindTerm code or assets.
#pragma once
#include <cwctype>

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

inline void Workspace::begin_rename(int index) {
    if(closing_ || index<0 || index>=static_cast<int>(tabs_.size()) || tabs_[index].shell->closing()) return;
    select(index);
    rename_id_=tabs_[index].id;
    rename_=CreateWindowExW(0,L"EDIT",tabs_[index].title.c_str(),
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,index*tab_width()+4,tab_bar_.top+2,
        tab_width()-32,22,window_,reinterpret_cast<HMENU>(100),GetModuleHandleW(nullptr),nullptr);
    require(rename_!=nullptr,"rename editor creation failed");
    SendMessageW(rename_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
    SendMessageW(rename_,EM_SETLIMITTEXT,64,0);
    SendMessageW(rename_,EM_SETSEL,0,-1);
    SetFocus(rename_);
}

inline void Workspace::end_rename(bool commit) {
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
        if(valid) for(auto& tab:tabs_) if(tab.id==id && !tab.shell->closing()) tab.title=title;
    }
    if(window_) InvalidateRect(window_,nullptr,FALSE);
    if(active() && !closing_) SetFocus(active()->handle());
}

inline bool Workspace::edit_key(const MSG& msg) {
    if(!rename_ || msg.hwnd!=rename_ || msg.message!=WM_KEYDOWN) return false;
    if(msg.wParam==VK_RETURN || msg.wParam==VK_ESCAPE) { end_rename(msg.wParam==VK_RETURN); return true; }
    return false;
}
