// Session-owned modeless find UI and bounded incremental search integration.
#pragma once

inline void ShellWindow::request_find() {
    if(closing_ || ime_.composing()) return;
    selection_end(); SetFocus(hwnd_);
    if(!find_) {
        find_=std::make_unique<tf_shell::FindBar>(hwnd_,renderer_->dpi());
        if(!find_query_.empty()) SetWindowTextW(find_->edit_handle(),utf16(find_query_).c_str());
        find_terminal_visible_=IsWindowVisible(hwnd_)!=FALSE;
    }
    find_->focus_query();
}

inline void ShellWindow::search_changed() {
    search_.notify_change(termforge::TextDocument(parser_.screen));
    if(find_current_) {
        const auto live=search_.match(termforge::TextDocument(parser_.screen),find_current_->index);
        if(!live || live->range.begin!=find_current_->match.range.begin ||
           live->range.end!=find_current_->match.range.end) find_current_.reset();
    }
}

inline void ShellWindow::navigate_find(termforge::SearchDirection direction) {
    if(closing_ || find_query_.empty()) return;
    const bool partial=search_.status()==termforge::SearchStatus::running;
    termforge::TextDocument document(parser_.screen);
    std::optional<termforge::TextPosition> anchor;
    if(find_current_) {
        const auto live=search_.match(document,find_current_->index);
        if(live && live->range.begin==find_current_->match.range.begin) anchor=live->range.begin;
    }
    const auto found=partial?search_.navigate_no_wrap(document,direction,anchor):search_.navigate(document,direction,anchor);
    if(!found) {
        find_pending_directions_.clear();
        if(partial) { find_pending_directions_.push_back(direction); find_initial_=false; }
        if(find_) find_->set_status(partial?L"Searching… no known match in this direction (no wrap)":
            search_.status()==termforge::SearchStatus::capped?L"No matches — result limit reached":L"No matches");
        return;
    }
    find_current_=found; find_initial_=false; find_pending_directions_.clear();
    viewport_.reveal(parser_.screen,found->match.range.begin.line_id);
    if(find_) find_->set_status(partial?L"Searching… partial match (no wrap)":
        std::to_wstring(found->index+1)+L" / "+std::to_wstring(search_.matches().size())+
        (search_.status()==termforge::SearchStatus::capped?L" — result limit reached":L"")+
        (found->wrapped?(search_.status()==termforge::SearchStatus::capped?L" — wrapped within limited results":L" — wrapped"):L""));
    publish();
}

inline void ShellWindow::poll_find() {
    if(closing_) return;
    termforge::TextDocument document(parser_.screen);
    if(find_) {
        const bool visible=IsWindowVisible(hwnd_)!=FALSE;
        if(visible!=find_terminal_visible_) { find_->show(visible); find_terminal_visible_=visible; }
        try {
            if(find_->query_changed()) {
                find_query_=utf8(find_->query().c_str());
                search_.start(document,find_query_,{find_->case_sensitive(),1024});
                find_current_.reset(); find_pending_directions_.clear(); find_initial_=!find_query_.empty();
                find_->set_status(find_query_.empty()?L"":L"Searching…"); publish();
            }
        } catch(const std::exception& error) {
            search_.cancel(); find_current_.reset(); find_query_.clear(); find_initial_=false; find_pending_directions_.clear();
            find_->set_status(L"Find: "+utf16(error.what())); publish();
        }
        // Bound key repeat work separately from the search scan budget.
        for(int count=0;find_ && count<4;++count) {
            const auto action=find_->take_action();
            if(action==tf_shell::FindAction::none) break;
            if(action==tf_shell::FindAction::close) {
                find_.reset(); search_.cancel(); find_current_.reset(); find_initial_=false;
                find_pending_directions_.clear(); restore_input_focus(); publish(); return;
            }
            navigate_find(action==tf_shell::FindAction::previous?termforge::SearchDirection::previous:
                          termforge::SearchDirection::next);
        }
    }
    if(search_.status()!=termforge::SearchStatus::running) {
        if(!find_pending_directions_.empty()) {
            const auto direction=find_pending_directions_.back(); find_pending_directions_.clear();
            navigate_find(direction);
        }
        return;
    }
    const auto progress=search_.step(document,search_.generation(),search_work_budget_);
    if(find_current_) {
        const auto live=search_.match(document,find_current_->index);
        if(!live || live->range.begin!=find_current_->match.range.begin ||
           live->range.end!=find_current_->match.range.end) find_current_.reset();
    }
    if(!find_pending_directions_.empty()) {
        const auto direction=find_pending_directions_.back(); find_pending_directions_.clear();
        navigate_find(direction);
    }
    else if(progress.status!=termforge::SearchStatus::running && find_initial_ && !search_.matches().empty())
        navigate_find(termforge::SearchDirection::previous);
    else if(find_ && !find_current_) {
        find_->set_status(progress.status==termforge::SearchStatus::running?
            (search_.matches().empty()?L"Searching…":L"Searching… partial results (no current match)"):
            search_.matches().empty()?L"No matches":
            std::to_wstring(search_.matches().size())+L" matches"+
            (progress.status==termforge::SearchStatus::capped?L" — result limit reached":L""));
    }
    if(find_ && find_current_ && find_pending_directions_.empty()) {
        const auto& found=*find_current_;
        find_->set_status(progress.status==termforge::SearchStatus::running?L"Searching… partial match (no wrap)":
            std::to_wstring(found.index+1)+L" / "+std::to_wstring(search_.matches().size())+
            (progress.status==termforge::SearchStatus::capped?L" — result limit reached":L"")+
            (found.wrapped?(progress.status==termforge::SearchStatus::capped?L" — wrapped within limited results":L" — wrapped"):L""));
    }
    if(progress.work_used) publish();
}

inline void ShellWindow::search_overlay(termforge::ViewSnapshot& view) {
    if(view.lines.empty()) return;
    termforge::TextDocument document(parser_.screen);
    for(size_t index=0;index<search_.matches().size();++index) {
        const auto& candidate=search_.matches()[index].range;
        if(candidate.end.line_id<view.lines.front().id || candidate.begin.line_id>view.lines.back().id) continue;
        const auto match=search_.match(document,index); if(!match) continue;
        const auto& range=match->range;
        for(auto& row:view.lines) {
            if(row.id<range.begin.line_id || row.id>range.end.line_id) continue;
            const size_t first=row.id==range.begin.line_id?range.begin.column:0;
            const size_t last=row.id==range.end.line_id?range.end.column:row.size();
            for(size_t column=first;column<last && column<row.size();++column) {
                row[column].style.bg=find_current_ && find_current_->index==index?11:3;
                row[column].style.fg=0;
            }
        }
    }
}
