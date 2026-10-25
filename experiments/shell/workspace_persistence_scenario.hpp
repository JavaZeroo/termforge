// Owned data-only persistence scenarios; no bitmap or user-desktop input.
#pragma once

inline std::string Workspace::workspace_geometry() {
    auto rectangle=[](RECT value) {
        return "["+std::to_string(value.left)+","+std::to_string(value.top)+","+
            std::to_string(value.right-value.left)+","+std::to_string(value.bottom-value.top)+"]";
    };
    auto child_rectangle=[&](HWND child) {
        RECT value{}; require(GetWindowRect(child,&value)!=FALSE,"workspace evidence child bounds failed");
        POINT first{value.left,value.top}, second{value.right,value.bottom};
        require(ScreenToClient(window_,&first)!=FALSE && ScreenToClient(window_,&second)!=FALSE,
                "workspace evidence coordinate conversion failed");
        return RECT{first.x,first.y,second.x,second.y};
    };
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    require(GetWindowPlacement(window_,&placement)!=FALSE,"workspace evidence normal bounds failed");
    std::ostringstream out; out.imbue(std::locale::classic());
    out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"{\"schema\":\"termforge-workspace-restore-v1\",\"dpi\":"<<dpi_
       <<",\"window\":"<<rectangle(placement.rcNormalPosition)
       <<",\"client\":["<<physical_width_<<','<<physical_height_<<']'
       <<",\"ratios\":["<<state_.left<<','<<state_.right<<','<<state_.upper<<','
       <<state_.left_top<<','<<state_.right_top<<']'
       <<",\"sides\":"<<state_.sides<<",\"focus_mode\":"<<state_.focus
       <<",\"focused\":\""<<(focused_==Group::main?"main":"bottom")
       <<"\",\"bottom_active\":"<<active_<<",\"main_active\":"<<main_active_
       <<",\"split_index\":"<<split_index_<<",\"split_active\":"<<split_active_
       <<",\"split_right_focus\":"<<split_right_focus_<<",\"tabs\":[";
    bool first=true;
    for(const auto group:{Group::bottom,Group::main}) for(const auto& tab:group_tabs(group)) {
        if(!tab.launch) throw std::runtime_error("workspace evidence requires a known launch");
        const auto [columns,rows]=tab.shell->grid_dimensions();
        const auto [cell_width,cell_height]=tab.shell->pixel_cell_size();
        if(!first) out<<','; first=false;
        out<<"{\"group\":\""<<(group==Group::main?"main":"bottom")<<"\",\"kind\":";
        termforge::Screen::quoted(out,utf8(tab.launch->kind.c_str()));
        out<<",\"title\":"; termforge::Screen::quoted(out,utf8(tab.title.c_str()));
        out<<",\"zoom\":"<<tab.shell->zoom_percent()
           <<",\"rect\":"<<rectangle(child_rectangle(tab.shell->handle()))
           <<",\"visible\":"<<(IsWindowVisible(tab.shell->handle())!=FALSE)
           <<",\"grid\":["<<columns<<','<<rows<<']'
           <<",\"cell\":["<<cell_width<<','<<cell_height<<"]}";
    }
    out<<"]}";
    return out.str();
}

inline void Workspace::save_workspace_geometry() {
    std::ofstream out(std::filesystem::path(image_).parent_path()/"restore-geometry.json",std::ios::binary);
    out<<workspace_geometry()<<'\n'; out.close();
    require(bool(out),"workspace geometry evidence write failed");
}

inline void Workspace::scenario_workspace() {
    if(image_.empty() || closing_) return;
    const auto directory=std::filesystem::path(image_).parent_path();
    if(ms(started_)>=30000) {
        std::ofstream out(directory/"persistence-failure.json",std::ios::binary);
        out<<"{\"mode\":\""<<test_mode_<<"\",\"stage\":"<<stage_<<",\"tabs\":[";
        bool first=true;
        for(const auto group:{Group::bottom,Group::main}) for(const auto& tab:group_tabs(group)) {
            if(!first) out<<','; first=false;
            out<<"{\"group\":\""<<(group==Group::main?"main":"bottom")<<"\",\"kind\":";
            termforge::Screen::quoted(out,tab.launch?utf8(tab.launch->kind.c_str()):"custom");
            out<<",\"ready\":"<<tab.shell->ready()<<",\"observation_enabled\":"
               <<(test_mode_=="ui-workspace-save" && stage_>=2)<<",\"observed\":";
            termforge::Screen::quoted(out,tab.shell->observed_output());
            out<<",\"visible\":[";
            bool first_row=true;
            if(tab.shell->ready()) for(const auto& row:tab.shell->visible_snapshot().lines) {
                std::string text; for(const auto& cell:row) text+=cell.text;
                while(!text.empty() && text.back()==' ') text.pop_back();
                if(!first_row) out<<','; first_row=false;
                termforge::Screen::quoted(out,text);
            }
            out<<"]}";
        }
        out<<"]}"; out.close(); require(bool(out),"workspace failure evidence write failed");
        require(false,("workspace persistence timeout at stage "+std::to_string(stage_)).c_str());
    }
    const auto all_ready=[&] {
        for(const auto group:{Group::bottom,Group::main}) for(const auto& tab:group_tabs(group))
            if(!tab.shell->ready()) return false;
        return true;
    };
    const auto query_sizes=[&] {
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group)) tab.shell->check_size();
    };
    const auto sizes_ready=[&] {
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group)) {
            const auto [columns,rows]=tab.shell->grid_dimensions();
            if(!tab.shell->settled() || !tab.shell->contains_line("TF_SIZE_"+std::to_string(columns)+"x"+std::to_string(rows)))
                return false;
        }
        return true;
    };
    const auto title=[&](Group group,int index,const std::wstring& value) {
        begin_rename(index,group); require(rename_!=nullptr,"workspace rename fixture did not open");
        require(SetWindowTextW(rename_,value.c_str())!=FALSE,"workspace rename fixture write failed");
        end_rename(true); require(group_tabs(group)[index].title==value,"workspace rename fixture was not committed");
    };
    const auto cmd_directory=directory/L"restore-cmd \x76ee\x5f55";
    const auto ps_directory=directory/L"restore-powershell \x7a7a\x683c";
    const auto settings_text=[&] {
        std::ifstream input(settings_path_,std::ios::binary);
        std::ostringstream content; content<<input.rdbuf(); return content.str();
    };

    if(test_mode_=="ui-workspace-empty-create") {
        if(stage_==0 && active() && active()->ready()) { active()->close(); stage_=1; }
        else if(stage_==1 && collected_==1 && session_count()==0) {
            require(!settings_failed_ && active_==-1 && main_active_==-1 && focused_==Group::bottom &&
                    tf_workspace::load_workspace(settings_path_).status==tf_workspace::LoadStatus::valid_empty_v2,
                    "empty workspace was not saved after the last session exited");
            stage_=100; close_all();
        }
        return;
    }
    if(test_mode_=="ui-workspace-empty") {
        if(stage_==0) {
            require(settings_load_status_==tf_workspace::LoadStatus::valid_empty_v2 &&
                    session_count()==0 && active_==-1 && main_active_==-1 && !active() && !settings_failed_,
                    "valid empty workspace incorrectly created a default session");
            stage_=100; close_all();
        }
        return;
    }
    if(test_mode_=="ui-workspace-v1") {
        const std::string legacy="TermForge-layout 1\nleft 0.24\nright 0.21\nupper 0.57\n"
            "left_top 0.38\nright_top 0.69\nsides 1\nfocus 0\n";
        if(stage_==0 && active() && active()->ready()) {
            require(settings_load_status_==tf_workspace::LoadStatus::migrated_v1 && settings_text()==legacy &&
                    tabs_.size()==1 && tabs_[0].title==L"cmd 1" && main_tabs_.empty() &&
                    state_.left==.24 && state_.right==.21 && state_.upper==.57 &&
                    state_.left_top==.38 && state_.right_top==.69 && state_.sides && !state_.focus &&
                    left_==326 && right_==285 && left_middle_==199 && right_middle_==335,
                    "v1 migration, deferred write or geometry mismatch");
            action(9);
            const auto recovered=tf_workspace::load_workspace(settings_path_);
            require(!settings_failed_ && recovered.status==tf_workspace::LoadStatus::valid_v2 &&
                    tf_workspace::same_layout(recovered.state.layout,state_) &&
                    recovered.state.bottom.size()==1 && recovered.state.bottom[0].title==L"cmd 1" &&
                    recovered.state.bottom[0].zoom_percent==125 && tabs_[0].shell->zoom_percent()==125,
                    "v1 committed save did not produce a complete v2 document");
            stage_=100; close_all();
        }
        return;
    }
    if(test_mode_=="ui-workspace-corrupt" || test_mode_=="ui-workspace-future") {
        const bool future=test_mode_=="ui-workspace-future";
        if(stage_==0 && active() && active()->ready()) {
            const auto expected=future?tf_workspace::LoadStatus::unsupported_version:tf_workspace::LoadStatus::corrupt;
            const auto original=settings_text();
            require(settings_load_status_==expected && tabs_.size()==1 && tabs_[0].title==L"cmd 1" &&
                    main_tabs_.empty() && tf_workspace::same_layout(state_,tf_workspace::LayoutState{}),
                    "corrupt or future configuration did not select the complete default fallback");
            if(future) {
                select(0);
                require(settings_read_only_ && settings_failed_ && settings_error_=="read-only settings" &&
                        settings_text()==original,"future configuration was overwritten");
            } else require(!settings_failed_ && settings_text()==original,
                           "corrupt configuration was overwritten during startup");
            stage_=100; close_all();
        }
        return;
    }

    if(test_mode_=="ui-workspace-save") {
        if(stage_==0 && active() && active()->ready()) {
            std::filesystem::create_directories(cmd_directory);
            std::filesystem::create_directories(ps_directory);
            add(Group::main,tf_shell::make_launch({L"cmd",{},cmd_directory.wstring()}));
            split(tf_shell::make_launch({L"powershell",{},ps_directory.wstring()}));
            title(Group::bottom,0,L"\x5e95\x90e8 cmd");
            focus_split(false); title(Group::main,0,L"\x4e3b cmd");
            focus_split(true); title(Group::main,1,L"\x7814\x7a76 PowerShell");
            focus_split(false); action(9); action(9);
            focus_split(true); action(10);
            select(0);
            state_={.24,.21,.57,.38,.69,false,false};
            RECT frame{0,0,physical(1200),physical(760)};
            require(AdjustWindowRectExForDpi(&frame,WS_OVERLAPPEDWINDOW,FALSE,0,dpi_)!=FALSE,
                    "workspace save fixture frame failed");
            require(SetWindowPos(window_,nullptr,90,60,frame.right-frame.left,frame.bottom-frame.top,
                SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"workspace save fixture bounds failed");
            layout(); require(width_==1200 && height_==760,"workspace save fixture client mismatch");
            SendMessageW(window_,WM_EXITSIZEMOVE,0,0); stage_=1;
        } else if(stage_==1 && all_ready()) {
            for(auto& tab:main_tabs_) tab.shell->begin_output_observation();
            focus_split(false); active()->command(L"echo TF_SAVE_CMD\r");
            focus_split(true); active()->command(L"Write-Output 'TF_SAVE_PS'\r");
            select(0); stage_=2;
        } else if(stage_==2 && main_tabs_[0].shell->contains_line("TF_SAVE_CMD") &&
                  main_tabs_[1].shell->contains_line("TF_SAVE_PS")) {
            query_sizes(); stage_=3;
        } else if(stage_==3 && sizes_ready()) {
            focus_split(true); select(0);
            require(focused_==Group::bottom && GetFocus()==tabs_[0].shell->handle() && split_right_focus_,
                    "workspace final bottom focus was not committed");
            const auto state=capture_state();
            save_workspace_geometry();
            require(!settings_failed_ && state.bottom.size()==1 && state.main.size()==2 &&
                    state.window.x==90 && state.window.y==60 && state.window.width==1200 && state.window.height==760 &&
                    !state.main_focused && state.split_active && state.split_index==1 && state.split_right_focus,
                    ("workspace save snapshot mismatch: save_failed="+std::to_string(settings_failed_)+
                     " counts="+std::to_string(state.bottom.size())+":"+std::to_string(state.main.size())+
                     " window="+std::to_string(state.window.x)+":"+std::to_string(state.window.y)+":"+
                     std::to_string(state.window.width)+"x"+std::to_string(state.window.height)+
                     " focus="+std::to_string(state.main_focused)+" split="+std::to_string(state.split_active)+
                     " index="+std::to_string(state.split_index)+" right="+std::to_string(state.split_right_focus)+
                     " live="+std::to_string(focused_==Group::main)+":"+std::to_string(split_right_focus_)+
                     " reason="+settings_error_).c_str());
            stage_=100; close_all();
        }
        return;
    }
    if(test_mode_=="ui-workspace-restore") {
        if(stage_==0 && all_ready()) {
            require(settings_load_status_==tf_workspace::LoadStatus::valid_v2 &&
                    tabs_.size()==1 && main_tabs_.size()==2 && focused_==Group::bottom &&
                    active_==0 && main_active_==0 && split_index_==1 && split_active_ && split_right_focus_,
                    "workspace restored members or focus mismatch");
            require(tabs_[0].title==L"\x5e95\x90e8 cmd" && tabs_[0].shell->zoom_percent()==100 &&
                    main_tabs_[0].title==L"\x4e3b cmd" && main_tabs_[0].shell->zoom_percent()==150 &&
                    main_tabs_[1].title==L"\x7814\x7a76 PowerShell" && main_tabs_[1].shell->zoom_percent()==75,
                    "workspace restored title or zoom mismatch");
            require(tabs_[0].launch && tabs_[0].launch->kind==L"cmd" &&
                    main_tabs_[0].launch && main_tabs_[0].launch->kind==L"cmd" &&
                    main_tabs_[0].launch->working_directory==restored_workspace_.main[0].working_directory &&
                    main_tabs_[1].launch && main_tabs_[1].launch->kind==L"powershell" &&
                    main_tabs_[1].launch->working_directory==restored_workspace_.main[1].working_directory,
                    "workspace restored launch definitions mismatch");
            const auto state=capture_state();
            require(state.window==restored_workspace_.window && tf_workspace::valid_workspace(state),
                    "workspace restored normal bounds mismatch");
            for(const auto group:{Group::bottom,Group::main}) for(const auto& tab:group_tabs(group))
                require(!tab.shell->contains_line("TF_SAVE_CMD") && !tab.shell->contains_line("TF_SAVE_PS"),
                        "old output was restored into a fresh session");
            focus_split(false);
            active()->command(L"for %I in (\"%CD%\") do @echo TF_CMD_DIR=%~nxI\r");
            focus_split(true);
            active()->command(L"Write-Output ('TF_PS_DIR=' + (Split-Path -Leaf $PWD.Path)); "
                L"Write-Output ('TF_PS_MAJOR=' + $PSVersionTable.PSVersion.Major)\r");
            select(0); active()->command(L"echo TF_RESTORE_BOTTOM\r"); stage_=1;
        } else if(stage_==1 && main_tabs_[0].shell->contains_line(utf8(L"TF_CMD_DIR=restore-cmd \x76ee\x5f55")) &&
                  main_tabs_[1].shell->contains_line(utf8(L"TF_PS_DIR=restore-powershell \x7a7a\x683c")) &&
                  main_tabs_[1].shell->contains_line("TF_PS_MAJOR=5") && active()->contains_line("TF_RESTORE_BOTTOM")) {
            query_sizes(); stage_=2;
        } else if(stage_==2 && sizes_ready()) {
            require(!settings_failed_,"workspace restored state save failed");
            save_workspace_geometry(); stage_=100; close_all();
        }
        return;
    }
    require(false,"unknown workspace persistence scenario");
}
