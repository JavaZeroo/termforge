// Owned synthetic UI scenarios; never used in a normal launch.
#pragma once
inline void Workspace::scenario_edit() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<20000,"workspace edit scenario timeout");
    if(stage_==0 && active()->ready()) {
        SendMessageW(window_,WM_MBUTTONDOWN,0,MAKELPARAM(30,tab_bar_.top+10));
        require(rename_!=nullptr,"middle click did not open rename");
        for(wchar_t c:std::wstring(L"\x7814\x7a76 Shell")) PostMessageW(rename_,WM_CHAR,c,0);
        PostMessageW(rename_,WM_KEYDOWN,VK_RETURN,0); stage_=1;
    } else if(stage_==1 && !rename_) {
        require(tabs_[0].title==L"\x7814\x7a76 Shell","unicode rename failed");
        require(GetFocus()==active()->handle(),"rename did not return terminal focus");
        active()->command(L"echo TF_RENAME_CLEAN\r"); stage_=2;
    } else if(stage_==2 && active()->contains_line("TF_RENAME_CLEAN")) {
        PostMessageW(window_,WM_KEYDOWN,VK_F2,0); stage_=3;
    } else if(stage_==3 && rename_) {
        SetWindowTextW(rename_,L"Cancelled"); PostMessageW(rename_,WM_KEYDOWN,VK_ESCAPE,0); stage_=4;
    } else if(stage_==4 && !rename_) {
        require(tabs_[0].title==L"\x7814\x7a76 Shell","Escape changed title");
        SendMessageW(window_,WM_COMMAND,6,0);
        SetWindowTextW(rename_,L"   "); PostMessageW(rename_,WM_KEYDOWN,VK_RETURN,0); stage_=5;
    } else if(stage_==5 && !rename_) {
        require(tabs_[0].title==L"\x7814\x7a76 Shell","blank title accepted");
        auto drag=[&](int x,int y,int end_x,int end_y) {
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));
            require(GetCapture()==window_,"divider did not capture mouse");
            SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end_x,end_y));
            SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(end_x,end_y));
            require(dragging_==Divider::none,"divider retained capture state");
        };
        drag(left_,100,-500,100); require(state_.left==.1,"left lower bound failed");
        drag(left_,100,5000,100); require(state_.left==.35,"left upper bound failed");
        drag(left_,100,300,100);
        drag(width_-right_-1,100,5000,100); require(state_.right==.1,"right lower bound failed");
        drag(width_-right_-1,100,-500,100); require(state_.right==.35,"right upper bound failed");
        drag(width_-right_-1,100,width_-281,100);
        drag(600,tab_bar_.top-25,600,-500); require(state_.upper==.25,"upper lower bound failed");
        drag(600,tab_bar_.top-25,600,5000); require(state_.upper==.8,"upper upper bound failed");
        drag(600,tab_bar_.top-25,600,420);
        drag(10,left_middle_,10,-500); require(state_.left_top==.2,"left pane lower bound failed");
        drag(10,left_middle_,10,5000); require(state_.left_top==.8,"left pane upper bound failed");
        drag(10,left_middle_,10,180);
        drag(width_-10,right_middle_,width_-10,-500); require(state_.right_top==.2,"right pane lower bound failed");
        drag(width_-10,right_middle_,width_-10,5000); require(state_.right_top==.8,"right pane upper bound failed");
        drag(width_-10,right_middle_,width_-10,250);
        require(left_==300 && right_==280 && left_middle_==180 && right_middle_==250,"drag geometry mismatch");
        const double upper=state_.upper;
        SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(600,420));
        SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(600,600));
        ReleaseCapture();
        require(state_.upper==upper && dragging_==Divider::none,"capture loss did not cancel drag");
        SendMessageW(window_,WM_COMMAND,3,0); SendMessageW(window_,WM_COMMAND,3,0);
        require(state_.upper==upper,"focus mode overwrote split ratio");
        require(!settings_failed_,"layout save failed");
        SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(600,420));
        SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(600,600));
        PostMessageW(window_,WM_KEYDOWN,VK_ESCAPE,0); stage_=60;
    } else if(stage_==60 && dragging_==Divider::none) {
        require(std::abs(state_.upper-387.0/769)<1e-12,"Escape did not cancel drag"); stage_=6;
    } else if(stage_==6 && active()->settled()) { active()->check_size(); stage_=7;
    } else if(stage_==7 && active()->contains_line("TF_SIZE_113x12")) {
        Bitmap frame(width_,height_); paint(frame.dc); active()->render_to(frame.dc,terminal_.left,terminal_.top); frame.save(image_);
        SendMessageW(window_,WM_COMMAND,4,0); require(!state_.sides,"side hide failed");
        const auto stored=tf_workspace::load_layout(settings_path_);
        require(!stored.sides && !stored.focus && stored.left==state_.left && stored.upper==state_.upper,"saved layout differs");
        stage_=100; SendMessageW(window_,WM_CLOSE,0,0);
    }
}

inline void Workspace::scenario_restore() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<20000,"workspace restore scenario timeout");
    if(stage_==0 && active()->ready()) {
        require(!state_.sides && !state_.focus,"visibility was not restored");
        require(std::abs(state_.left-300.0/1358)<1e-12 && std::abs(state_.right-280.0/1358)<1e-12 &&
                std::abs(state_.upper-387.0/769)<1e-12 && std::abs(state_.left_top-147.0/386)<1e-12 &&
                std::abs(state_.right_top-217.0/386)<1e-12,"split ratios were not restored");
        require(tabs_.size()==1 && tabs_[0].title==L"cmd 1","session names unexpectedly restored");
        SendMessageW(window_,WM_COMMAND,4,0); stage_=1;
    } else if(stage_==1 && active()->settled()) { active()->check_size(); stage_=2;
    } else if(stage_==2 && active()->contains_line("TF_SIZE_113x12")) {
        require(left_==300 && right_==280 && left_middle_==180 && right_middle_==250,"restored geometry mismatch");
        Bitmap frame(width_,height_); paint(frame.dc); active()->render_to(frame.dc,terminal_.left,terminal_.top); frame.save(image_);
        stage_=100; SendMessageW(window_,WM_CLOSE,0,0);
    }
}

inline void Workspace::scenario() {
        if(image_.empty() || closing_) return;
        if(ms(started_)>=20000) throw std::runtime_error("workspace scenario timeout: stage="+std::to_string(stage_)+
            " client="+std::to_string(width_)+"x"+std::to_string(height_));
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
            // Shrink the Sessions pane until even its first row is not fully
            // visible. Its blank tail must not select that hidden row.
            const auto previous=state_;
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(600,tab_bar_.top-25));
            SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(600,0));
            SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(600,0));
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(width_-20,right_middle_));
            SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(width_-20,0));
            SendMessageW(window_,WM_LBUTTONUP,0,MAKELPARAM(width_-20,0));
            require(right_middle_<83,"partial session row setup failed");
            SendMessageW(window_,WM_LBUTTONDOWN,0,MAKELPARAM(width_-20,65));
            require(active_==1,"invisible session row was clickable");
            state_=previous; layout(); save_layout();
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
            SendMessageW(window_,WM_COMMAND,4,0); require(!state_.sides,"panel toggle failed");
            SendMessageW(window_,WM_COMMAND,4,0); require(state_.sides,"panel restore failed");
            SendMessageW(window_,WM_MBUTTONDOWN,0,MAKELPARAM(160,tab_bar_.top+10));
            require(rename_!=nullptr && rename_id_==2,"second tab rename setup failed");
            SetWindowTextW(rename_,L"Pending");
            SendMessageW(rename_,EM_SETSEL,7,7);
            tabs_[0].shell->command(L"exit 0\r"); stage_=6;
        } else if(stage_==6 && tabs_.size()==1) {
            require(active()->contains_line("TF_UI_SECOND"),"remaining session lost state");
            require(tabs_[active_].id==2 && tabs_[active_].title==L"cmd 2","background close changed active identity or committed rename");
            require(rename_ && GetFocus()==rename_,"background close stole rename focus");
            PostMessageW(GetFocus(),WM_CHAR,L'X',0); stage_=61;
        } else if(stage_==61) {
            wchar_t title[65]{}; GetWindowTextW(rename_,title,65);
            require(std::wstring(title)==L"PendingX","typing after background close left the editor");
            PostMessageW(rename_,WM_KEYDOWN,VK_ESCAPE,0); stage_=62;
        } else if(stage_==62 && !rename_) {
            SendMessageW(window_,WM_COMMAND,2,0); stage_=7;
        } else if(stage_==7 && tabs_.empty()) {
            SendMessageW(window_,WM_COMMAND,1,0); stage_=8;
        } else if(stage_==8 && active()->ready()) { active()->command(L"echo TF_UI_REOPEN\r"); stage_=9;
        } else if(stage_==9 && active()->contains_line("TF_UI_REOPEN")) {
            stage_=10; SendMessageW(window_,WM_CLOSE,0,0);
        }
    }
