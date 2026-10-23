// Owned synthetic main-session acceptance, independent of WindTerm artifacts.
#pragma once
inline void Workspace::record_geometry(const char* phase) {
    auto rect=[](RECT r) { return "["+std::to_string(r.left)+","+std::to_string(r.top)+","+
        std::to_string(r.right-r.left)+","+std::to_string(r.bottom-r.top)+"]"; };
    std::string child="null";
    if(selected(Group::main)) {
        RECT actual{}; require(GetWindowRect(selected(Group::main)->handle(),&actual)!=FALSE,"main window query failed");
        // The outer child includes its native history scrollbar. Its bounds
        // remain the measured workspace rectangle, independently of the grid.
        POINT origin{actual.left,actual.top};
        require(ScreenToClient(window_,&origin)!=FALSE,"main origin conversion failed");
        OffsetRect(&actual,origin.x-actual.left,origin.y-actual.top); child=rect(actual);
        require(EqualRect(&actual,&main_terminal_)!=FALSE,"main child geometry differs from assigned view");
    }
    geometry_samples_.push_back("{\"phase\":\""+std::string(phase)+"\",\"window\":["+std::to_string(width_)+","+
        std::to_string(height_)+"],\"main_group\":"+rect(main_group_)+",\"main_toolbar\":"+rect(main_toolbar_)+
        ",\"main_address\":"+rect(main_address_)+",\"main_terminal\":"+rect(main_terminal_)+",\"main_child\":"+child+
        ",\"shell_terminal\":"+rect(terminal_)+"}");
    std::ofstream out(std::filesystem::path(image_).parent_path()/"geometry.json");
    out<<"{\"ui_font_family\":\""<<utf8(ui_font_->face().c_str())<<"\",\"snapshots\":[";
    for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
    out<<"]}\n"; require(bool(out),"main geometry write failed");
}

inline void Workspace::scenario_main() {
    if(image_.empty() || closing_) return;
    if(ms(started_)>=30000) throw std::runtime_error("workspace main scenario timeout: stage="+std::to_string(stage_));
    auto click_at=[&](int x,int y) { SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(x,y)); };
    auto click_child=[&](ShellWindow* child) { SendMessageW(child->handle(),WM_LBUTTONDOWN,0,MAKELPARAM(10,10)); };
    if(stage_==0 && selected(Group::bottom)->ready()) {
        record_geometry("empty-1360");
        selected(Group::bottom)->command(L"echo TF_MAIN_BOTTOM\r");
        click_at(main_group_.right-15,main_group_.top+8); stage_=1;
    } else if(stage_==1 && selected(Group::main)->ready()) {
        selected(Group::main)->command(L"echo TF_MAIN_FIRST\r");
        click_at(main_toolbar_.left+12,main_toolbar_.top+12); stage_=2;
    } else if(stage_==2 && selected(Group::main)->ready()) {
        selected(Group::main)->command(L"echo TF_MAIN_SECOND\r");
        main_tabs_[0].shell->command(L"echo TF_MAIN_HIDDEN\r");
        selected(Group::bottom)->command(L"echo TF_MAIN_BOTTOM_LIVE\r"); stage_=3;
    } else if(stage_==3 && selected(Group::main)->contains_line("TF_MAIN_SECOND") &&
              main_tabs_[0].shell->contains_line("TF_MAIN_HIDDEN") && selected(Group::bottom)->contains_line("TF_MAIN_BOTTOM_LIVE")) {
        require(tabs_.size()==1 && main_tabs_.size()==2 && session_count()==3,"main and shell group count mismatch");
        require(!selected(Group::main)->contains_line("TF_MAIN_FIRST") && !selected(Group::bottom)->contains_line("TF_MAIN_SECOND"),"group state leaked");
        require(IsWindowVisible(selected(Group::main)->handle()) && IsWindowVisible(selected(Group::bottom)->handle()) &&
                !IsWindowVisible(main_tabs_[0].shell->handle()),"group visibility mismatch");
        click_at(main_group_.left+16,main_group_.top+10);
        require(main_active_==0 && active_==0 && active()->contains_line("TF_MAIN_FIRST"),"main click selection failed");
        shortcut(VK_TAB,true); require(main_active_==1 && active_==0,"main Ctrl+Tab affected bottom selection");
        click_child(selected(Group::bottom));
        require(focused_==Group::bottom && GetFocus()==selected(Group::bottom)->handle(),"bottom child click focus failed");
        shortcut(VK_TAB,true); require(main_active_==1 && active_==0,"bottom Ctrl+Tab affected main selection");
        click_child(selected(Group::main)); require(focused_==Group::main,"main child click focus failed");
        shortcut(VK_F11,false);
        require(state_.focus && focused_==Group::bottom && GetFocus()==selected(Group::bottom)->handle() &&
                !IsWindowVisible(selected(Group::main)->handle()),"focus shell did not transfer existing main focus to bottom");
        shortcut(VK_F11,false); click_child(selected(Group::main));
        require(!state_.focus && focused_==Group::main && GetFocus()==selected(Group::main)->handle() &&
                IsWindowVisible(selected(Group::main)->handle()) && IsWindowVisible(selected(Group::bottom)->handle()) &&
                main_active_==1 && active_==0,"focus shell restore changed selected sessions or visibility");
        shortcut(VK_F2,false); require(rename_ && rename_group_==Group::main,"F2 did not target main");
        SetWindowTextW(rename_,L"\x4e3b\x4f1a\x8bdd cmd");
        click_child(selected(Group::bottom));
        require(!rename_ && focused_==Group::bottom && GetFocus()==selected(Group::bottom)->handle(),"rename focus loss reclaimed old terminal");
        click_child(selected(Group::main)); stage_=4;
    } else if(stage_==4 && !rename_ && selected(Group::main)->settled() && selected(Group::bottom)->settled()) {
        require(main_tabs_[1].title==L"\x4e3b\x4f1a\x8bdd cmd" && tabs_[0].title==L"cmd 1","main rename affected another group");
        require(GetFocus()==selected(Group::main)->handle(),"main rename did not restore focus");
        selected(Group::main)->check_size(); selected(Group::bottom)->check_size(); stage_=5;
    } else if(stage_==5 && selected(Group::main)->contains_line(size_marker(906,17,selected(Group::main))) && selected(Group::bottom)->contains_line(size_marker(1360,7,selected(Group::bottom)))) {
        record_geometry("session-1360");
        Bitmap frame(width_,height_); paint(frame.dc);
        selected(Group::main)->render_to(frame.dc,main_terminal_.left,main_terminal_.top);
        selected(Group::bottom)->render_to(frame.dc,terminal_.left,terminal_.top); frame.save(image_);
        RECT size{0,0,2048,1120}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
        require(SetWindowPos(window_,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"main resize failed"); stage_=6;
    } else if(stage_==6 && selected(Group::main)->settled() && selected(Group::bottom)->settled()) {
        selected(Group::main)->check_size(); selected(Group::bottom)->check_size(); stage_=7;
    } else if(stage_==7 && selected(Group::main)->contains_line(size_marker(1364,24,selected(Group::main))) && selected(Group::bottom)->contains_line(size_marker(2048,11,selected(Group::bottom)))) {
        record_geometry("session-2048");
        click_child(selected(Group::bottom));
        main_tabs_[0].shell->command(L"exit 0\r"); stage_=8;
    } else if(stage_==8 && main_tabs_.size()==1) {
        require(focused_==Group::bottom && GetFocus()==selected(Group::bottom)->handle(),"background main exit stole bottom focus");
        require(main_tabs_[0].title==L"\x4e3b\x4f1a\x8bdd cmd" && main_active_==0,"background exit changed main identity");
        click_child(selected(Group::main)); shortcut('W',true);
        require(selected(Group::main)->closing() && !selected(Group::bottom)->closing(),"main Ctrl+W targeted bottom");
        // Begin editing in the other area before the asynchronous close is
        // collected, so its completion must preserve this editor and focus.
        click_child(selected(Group::bottom));
        shortcut(VK_F2,false); require(rename_ && rename_group_==Group::bottom,"F2 did not target bottom");
        SetWindowTextW(rename_,L"Pending"); SendMessageW(rename_,EM_SETSEL,7,7);
        stage_=9;
    } else if(stage_==9 && main_tabs_.empty()) {
        require(main_active_==-1 && main_group_.bottom-main_group_.top==19,"last main exit did not restore empty area");
        require(rename_ && GetFocus()==rename_ && focused_==Group::bottom,"last main exit stole rename focus");
        PostMessageW(rename_,WM_CHAR,L'X',0); stage_=10;
    } else if(stage_==10) {
        wchar_t text[65]{}; GetWindowTextW(rename_,text,65);
        require(std::wstring(text)==L"PendingX","background exit interrupted rename typing");
        PostMessageW(rename_,WM_KEYDOWN,VK_ESCAPE,0); stage_=11;
    } else if(stage_==11 && !rename_) {
        shortcut(VK_F11,false); require(state_.focus,"shell focus setup failed");
        shortcut('T',true,true);
        require(!state_.focus && focused_==Group::main && IsWindowVisible(active()->handle()),"new main remained hidden in focus-shell mode");
        require(!tf_workspace::load_layout(settings_path_).focus,"main reopen did not save restored layout"); stage_=12;
    } else if(stage_==12 && selected(Group::main)->ready()) {
        selected(Group::main)->command(L"echo TF_MAIN_REOPEN\r"); stage_=13;
    } else if(stage_==13 && selected(Group::main)->contains_line("TF_MAIN_REOPEN")) {
        require(GetFocus()==selected(Group::main)->handle(),"reopened main did not receive focus");
        // The bottom tab close hit must target that tab even while the main
        // child has keyboard focus.
        click_at(tab_width()-12,tab_bar_.top+10); stage_=14;
    } else if(stage_==14 && tabs_.empty()) {
        require(main_tabs_.size()==1 && !main_tabs_[0].shell->closing(),"bottom close hit closed main session");
        require(focused_==Group::main && GetFocus()==selected(Group::main)->handle(),"last bottom close did not return main focus");
        shortcut(VK_F11,false);
        require(state_.focus && GetFocus()==window_ && !IsWindowVisible(selected(Group::main)->handle()),"empty shell focus retained hidden main input target");
        shortcut(VK_F11,false); click_child(selected(Group::main));
        require(!state_.focus && GetFocus()==selected(Group::main)->handle(),"restored main did not regain visible focus");
        shortcut('T',true); stage_=15;
    } else if(stage_==15 && selected(Group::bottom)->ready()) {
        selected(Group::bottom)->command(L"echo TF_BOTTOM_REOPEN\r"); stage_=16;
    } else if(stage_==16 && selected(Group::bottom)->contains_line("TF_BOTTOM_REOPEN")) {
        require(main_tabs_.size()==1 && tabs_.size()==1 && IsWindowVisible(selected(Group::main)->handle()),"two-group global close setup failed");
        stage_=100; SendMessageW(window_,WM_CLOSE,0,0);
    }
}
