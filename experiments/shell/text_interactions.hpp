// Per-session mouse selection, view-only highlights and actual Unicode copy.
#pragma once

inline std::optional<termforge::TextPosition> ShellWindow::selection_position(int x,int y,bool nearest) const {
    if(!snapshot_ || snapshot_->lines.empty()) return std::nullopt;
    const int row=std::clamp(y/renderer_->cell_height(),0,snapshot_->rows()-1);
    const int column=std::clamp((x+(nearest?renderer_->cell_width()/2:0))/renderer_->cell_width(),0,snapshot_->columns());
    termforge::TextDocument document(parser_.screen);
    const auto index=document.index_of(snapshot_->lines[static_cast<size_t>(row)].id);
    return index?document.position(*index,static_cast<size_t>(column)):std::nullopt;
}

inline void ShellWindow::selection_end() {
    const bool owned_drag=selecting_;
    selecting_=false; selection_anchor_.reset();
    // The same HWND can be captured by DefWindowProc's scrollbar tracker.
    // Only release capture established by our own text drag.
    if(owned_drag && hwnd_ && GetCapture()==hwnd_) ReleaseCapture();
}

inline void ShellWindow::selection_down(int x,int y,unsigned forced_clicks) {
    if(closing_ || ime_.composing()) return;
    SetFocus(hwnd_);
    const auto position=selection_position(x,y,false);
    if(!position) return;
    const DWORD now=GetTickCount();
    const bool nearby=std::abs(x-last_click_point_.x)<=GetSystemMetrics(SM_CXDOUBLECLK)/2 &&
                    std::abs(y-last_click_point_.y)<=GetSystemMetrics(SM_CYDOUBLECLK)/2;
    click_count_=forced_clicks?forced_clicks:
        (nearby && now-last_click_time_<=GetDoubleClickTime()?(click_count_%3)+1:1);
    last_click_time_=now; last_click_point_={x,y}; selection_pointer_={x,y};
    termforge::TextDocument document(parser_.screen);
    const auto index=document.index_of(position->line_id);
    selection_end();
    if(click_count_==2) selection_.select_word(document,*index,position->column);
    else if(click_count_==3) selection_.select_line(document,*index);
    else {
        selection_.clear(); selection_anchor_=position; selecting_=true; SetCapture(hwnd_);
    }
    publish();
}

inline void ShellWindow::selection_move(int x,int y) {
    if(!selecting_ || !selection_anchor_ || closing_) return;
    selection_pointer_={x,y};
    const auto position=selection_position(x,y,true);
    termforge::TextDocument document(parser_.screen);
    if(!position || !document.valid(*selection_anchor_)) { selection_end(); return; }
    if(!selection_.set(document,{*selection_anchor_,*position})) input_status(L"Selection exceeds its bounded capacity");
    publish();
}

inline void ShellWindow::selection_autoscroll() {
    if(!selecting_ || closing_ || GetCapture()!=hwnd_) return;
    const int extent=snapshot_->rows()*renderer_->cell_height();
    const int direction=selection_pointer_.y<0?-1:selection_pointer_.y>=extent?1:0;
    if(!direction) return;
    const auto before=viewport_.reconcile(parser_.screen).top;
    viewport_.scroll_by(parser_.screen,direction);
    if(viewport_.reconcile(parser_.screen).top!=before) {
        publish(); selection_move(selection_pointer_.x,selection_pointer_.y);
    }
}

inline void ShellWindow::selection_overlay(termforge::ViewSnapshot& view) {
    termforge::TextDocument document(parser_.screen);
    for(size_t row=0;row<view.lines.size();++row) {
        const auto columns=selection_.columns(document,view.viewport.top+row);
        if(!columns) continue;
        for(size_t column=columns->begin;column<columns->end && column<view.lines[row].size();++column) {
            // Alter this owned presentation copy, never parser/history cells.
            view.lines[row][column].style.bg=4; view.lines[row][column].style.fg=15;
        }
    }
}

inline void ShellWindow::request_copy() {
    if(closing_) return;
    const auto value=selected_text();
    if(value.status==termforge::SelectionCopyStatus::empty) { input_status(L"No selected text"); return; }
    if(value.status!=termforge::SelectionCopyStatus::ok) {
        input_status(value.status==termforge::SelectionCopyStatus::limit_exceeded?
            L"Selected text exceeds the copy limit":L"Selected text changed; select it again"); publish(); return;
    }
    try {
        const auto wide=utf16(value.text);
        tf_shell::write_unicode_clipboard(hwnd_,std::u16string_view(reinterpret_cast<const char16_t*>(wide.data()),wide.size()));
        input_status(L"Selected text copied");
    } catch(const std::exception& error) { input_status(L"Copy failed: "+utf16(error.what())); }
}

inline void ShellWindow::select_all_text() {
    if(closing_) return;
    selection_end();
    termforge::TextDocument document(parser_.screen);
    for(size_t end=document.size();end>0;--end) {
        const auto row=document.line(end-1);
        if(row.projection.spans.empty() && row.ending==termforge::LineEnding::none) continue;
        auto last=document.position(end-1,row.projection.spans.empty()?0:row.projection.spans.back().column_end);
        if(row.ending==termforge::LineEnding::hard_break && end<document.size()) last=document.position(end,0);
        if(!selection_.set(document,{*document.position(0,0),*last})) input_status(L"Selection exceeds its bounded capacity");
        publish(); return;
    }
    selection_.clear(); publish();
}

inline void ShellWindow::selection_menu(int x,int y) {
    if(closing_ || ime_.composing()) return;
    selection_end(); SetFocus(hwnd_);
    HMENU menu=CreatePopupMenu(); require(menu!=nullptr,"text menu creation failed");
    AppendMenuW(menu,MF_STRING|(has_selection()?0:MF_GRAYED),1,L"Copy\tCtrl+Shift+C");
    AppendMenuW(menu,MF_STRING|(paste_busy()?MF_GRAYED:0),2,L"Paste\tCtrl+Shift+V");
    AppendMenuW(menu,MF_STRING,3,L"Select All\tCtrl+Shift+A");
    AppendMenuW(menu,MF_STRING,4,L"Find\tCtrl+Shift+F");
    POINT point{x,y};
    if(x==-1 && y==-1) { point={0,0}; ClientToScreen(hwnd_,&point); }
    const int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd_,nullptr);
    DestroyMenu(menu);
    if(selected==1) request_copy();
    if(selected==2) request_paste();
    if(selected==3) select_all_text();
    if(selected==4) request_find();
}
