// Controlled DPI-message acceptance on owned HWNDs; not a real monitor move.
#pragma once
inline void Workspace::scenario_dpi() {
    if(image_.empty() || closing_) return;
    if(ms(started_)>=30000) throw std::runtime_error("workspace DPI scenario timeout: stage="+std::to_string(stage_));
    const std::string keep_line="TF_DPI_KEEP_012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";
    auto mouse=[&](UINT message,int x,int y) {
        SendMessageW(window_,message,message==WM_MOUSEMOVE?MK_LBUTTON:0,MAKELPARAM(physical(x),physical(y)));
    };
    auto transition=[&](int dpi) {
        // A synthetic message does not change the OS monitor or non-client DPI.
        // Use the actual OS frame metrics to request a known physical client;
        // the production handler consumes the OS-suggested rect unchanged.
        RECT next{0,0,MulDiv(1360,dpi,96),MulDiv(860,dpi,96)};
        const UINT os_dpi=GetDpiForWindow(window_);
        require(os_dpi!=0 && AdjustWindowRectExForDpi(&next,WS_OVERLAPPEDWINDOW,FALSE,0,os_dpi)!=FALSE,"DPI scenario frame calculation failed");
        SendMessageW(window_,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&next));
        require(dpi_==dpi && width_==1360 && height_==860,"controlled DPI logical size changed");
    };
    auto query=[&](const wchar_t* phase) {
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group)) {
            tab.shell->command(L"cls\r"); tab.shell->check_size();
            tab.shell->command(L"echo TF_DPI_"+std::wstring(phase)+L"\r");
        }
    };
    auto complete=[&](const char* phase) {
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group))
            if(!tab.shell->settled() || !tab.shell->contains_line("TF_DPI_"+std::string(phase))) return false;
        return true;
    };
    auto rect_text=[](RECT rect) {
        return "["+std::to_string(rect.left)+","+std::to_string(rect.top)+","+
            std::to_string(rect.right-rect.left)+","+std::to_string(rect.bottom-rect.top)+"]";
    };
    auto actual_rect=[&](HWND child) {
        RECT rect{}; require(GetWindowRect(child,&rect)!=FALSE,"DPI child window query failed");
        POINT origin{rect.left,rect.top}; require(ScreenToClient(window_,&origin)!=FALSE,"DPI child position query failed");
        OffsetRect(&rect,origin.x-rect.left,origin.y-rect.top); return rect;
    };
    auto editor_check=[&]() {
        require(rename_ && GetFocus()==rename_,"DPI transition lost rename focus");
        // EDIT has a border: compare its outer bounds, not its client rectangle.
        RECT rect{}; require(GetWindowRect(rename_,&rect)!=FALSE,"DPI edit rectangle query failed");
        POINT origin{rect.left,rect.top}; ScreenToClient(window_,&origin);
        OffsetRect(&rect,origin.x-rect.left,origin.y-rect.top);
        const RECT expected=physical(rename_rect(rename_group_,selection(rename_group_)));
        require(EqualRect(&rect,&expected)!=FALSE,"DPI rename editor placement mismatch");
        LOGFONTW description{};
        const auto handle=reinterpret_cast<HFONT>(SendMessageW(rename_,WM_GETFONT,0,0));
        require(handle==font_ && GetObjectW(handle,sizeof(description),&description)==sizeof(description) &&
            description.lfHeight==-MulDiv(9,dpi_,72),"DPI rename editor font mismatch");
        wchar_t text[65]{}; GetWindowTextW(rename_,text,65);
        require(std::wstring(text)==L"Pending DPI","DPI transition changed rename text");
    };
    auto record=[&](const char* phase) {
        RECT parent{}; GetClientRect(window_,&parent);
        require(parent.right==physical(1360) && parent.bottom==physical(860),"DPI physical client size mismatch");
        std::string sample="{\"phase\":\""+std::string(phase)+"\",\"controlled_dpi\":"+std::to_string(dpi_)+
            ",\"os_window_dpi\":"+std::to_string(GetDpiForWindow(window_))+",\"logical_client\":[1360,860],\"physical_client\":["+
            std::to_string(parent.right)+","+std::to_string(parent.bottom)+"],\"sessions\":[";
        bool first=true;
        for(const auto group:{Group::bottom,Group::main}) for(size_t i=0;i<group_tabs(group).size();++i) {
            auto& tab=group_tabs(group)[i]; auto* shell=tab.shell.get();
            const RECT logical_rect=group==Group::main?main_terminal_:terminal_;
            const RECT expected=physical(logical_rect), measured=actual_rect(shell->handle());
            require(EqualRect(&expected,&measured)!=FALSE,"DPI shell child geometry mismatch");
            require(shell->display_dpi()==dpi_,"DPI did not reach a hidden or visible session");
            const auto [columns,rows]=shell->grid_dimensions();
            const auto [cell_width,cell_height]=shell->pixel_cell_size();
            RECT client{}; require(GetClientRect(shell->handle(),&client)!=FALSE,"DPI text client query failed");
            const int native_bar=GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(shell->handle()));
            require(client.right==measured.right-measured.left-native_bar && client.bottom==measured.bottom-measured.top,"DPI native scrollbar allocation differs");
            require(columns==client.right/cell_width && rows==client.bottom/cell_height,"DPI grid does not fit measured text client");
            require(shell->contains_line("TF_SIZE_"+std::to_string(columns)+"x"+std::to_string(rows)),"DPI child-reported ConPTY size differs");
            require(shell->frame_matches_full(),"DPI retained terminal frame differs from a full redraw");
            if(!first) sample+=','; first=false;
            sample+="{\"group\":\""+std::string(group==Group::main?"main":"bottom")+"\",\"index\":"+std::to_string(i)+
                ",\"visible\":"+(IsWindowVisible(shell->handle())?"true":"false")+",\"zoom_percent\":"+std::to_string(shell->zoom_percent())+
                ",\"display_dpi\":"+std::to_string(shell->display_dpi())+",\"logical_rect\":"+rect_text(logical_rect)+
                ",\"physical_rect\":"+rect_text(measured)+",\"grid\":["+std::to_string(columns)+","+std::to_string(rows)+
                "],\"cell_pixels\":["+std::to_string(cell_width)+","+std::to_string(cell_height)+
                "],\"client_pixels\":["+std::to_string(client.right)+","+std::to_string(client.bottom)+"],\"native_scrollbar_width\":"+std::to_string(native_bar)+"}";
        }
        sample+="],\"rename_active\":"+std::string(rename_?"true":"false")+"}";
        geometry_samples_.push_back(std::move(sample));
        if(dpi_!=96) {
            Bitmap frame(parent.right,parent.bottom); paint(frame.dc);
            const RECT main=physical(main_terminal_), bottom=physical(terminal_);
            selected(Group::main)->render_to(frame.dc,main.left,main.top);
            selected(Group::bottom)->render_to(frame.dc,bottom.left,bottom.top);
            frame.save((std::filesystem::path(image_).parent_path()/("frame-"+std::to_string(dpi_)+".bmp")).string());
        }
    };
    if(stage_==0 && active()->ready()) {
        transition(96); add(); stage_=1;
    } else if(stage_==1 && active()->ready()) {
        add(Group::main); stage_=2;
    } else if(stage_==2 && active()->ready() && tabs_[0].shell->settled() && tabs_[1].shell->settled()) {
        query(L"BASE"); stage_=3;
    } else if(stage_==3 && complete("BASE")) {
        record("96-baseline");
        mouse(WM_LBUTTONDOWN,main_group_.left+12,main_group_.top+10);
        require(focused_==Group::main,"physical main tab click did not focus main");
        shortcut(VK_OEM_PLUS,true); require(active()->zoom_percent()==125,"Ctrl+plus did not zoom main");
        SendMessageW(window_,WM_COMMAND,9,0); require(active()->zoom_percent()==150,"zoom menu action failed");
        shortcut(VK_SUBTRACT,true); require(active()->zoom_percent()==125,"Ctrl+subtract failed");
        shortcut('0',true); require(active()->zoom_percent()==100,"Ctrl+0 failed");
        for(int i=0;i<10;++i) shortcut(VK_ADD,true);
        require(active()->zoom_percent()==200,"zoom upper bound failed");
        for(int i=0;i<10;++i) shortcut(VK_SUBTRACT,true);
        require(active()->zoom_percent()==50,"zoom lower bound failed");
        shortcut('0',true); require(active()->zoom_percent()==100,"zoom reset after bounds failed");
        shortcut(VK_ADD,true);
        require(tabs_[0].shell->zoom_percent()==100 && tabs_[1].shell->zoom_percent()==100,"main zoom leaked to bottom sessions");
        tabs_[0].shell->command(L"echo "+std::wstring(keep_line.begin(),keep_line.end())+L"\r"); stage_=30;
    } else if(stage_==30 && tabs_[0].shell->contains_line(keep_line) && selected(Group::main)->settled()) {
        // A sub-cell client resize retains the grid but still needs an update
        // region: parent invalidation excludes WS_CLIPCHILDREN rectangles.
        auto* visible=tabs_[1].shell.get();
        const auto old_grid=visible->grid_dimensions();
        const RECT area=actual_rect(visible->handle());
        require(ValidateRect(visible->handle(),nullptr)!=FALSE,"DPI test validate failed");
        RECT text_client{}; require(GetClientRect(visible->handle(),&text_client)!=FALSE,"sub-cell client query failed");
        // A native scrollbar changes the remainder: +1 may cross a cell edge.
        const int delta=text_client.right%visible->pixel_cell_size().first? -1:1;
        require(MoveWindow(visible->handle(),area.left,area.top,area.right-area.left+delta,area.bottom-area.top,FALSE)!=FALSE,"sub-cell test resize failed");
        visible->sync_client_size();
        require(visible->grid_dimensions()==old_grid && GetUpdateRect(visible->handle(),nullptr,FALSE)!=FALSE,"same-grid client resize lost repaint");
        layout();
        mouse(WM_MBUTTONDOWN,main_group_.left+12,main_group_.top+10);
        require(rename_ && rename_group_==Group::main,"physical main middle click did not open editor");
        SetWindowTextW(rename_,L"Pending DPI");
        require(!shortcut(VK_ADD,true),"rename consumed terminal zoom shortcut");
        SendMessageW(window_,WM_COMMAND,9,0);
        require(selected(Group::main)->zoom_percent()==125,"rename allowed terminal menu zoom");
        const auto state=state_;
        // Controlled WM_DPICHANGED does not scale the OS-owned scrollbar.
        // Prepare the bottom grids to the independently expected 144-DPI
        // width so the transition still exercises a same-grid font change.
        const int bar=GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(visible->handle()));
        const std::pair<int,int> stable_grid{(MulDiv(1360,144,96)-bar)/18,old_grid.second};
        for(auto& tab:tabs_) {
            const RECT before=actual_rect(tab.shell->handle());
            require(MoveWindow(tab.shell->handle(),before.left,before.top,stable_grid.first*12+bar,
                before.bottom-before.top,FALSE)!=FALSE,"same-grid DPI preparation failed");
            tab.shell->sync_client_size();
            require(tab.shell->grid_dimensions()==stable_grid,"same-grid DPI preparation mismatch");
        }
        transition(144); editor_check();
        require(tabs_[0].shell->contains_line(keep_line),"DPI transition transiently clipped a hidden terminal line");
        // Check before cls/query can dirty the whole screen and hide a stale
        // surface bug. Both bottom tabs retain exactly the same grid.
        for(auto& tab:tabs_) {
            require(tab.shell->grid_dimensions()==stable_grid,"equal-scale DPI changed prepared bottom grid");
            require(tab.shell->frame_matches_full(),"same-grid DPI retained frame mismatch before new output");
        }
        require(selected(Group::main)->frame_matches_full(),"DPI main retained frame mismatch before new output");
        require(state_.left==state.left && state_.right==state.right && state_.upper==state.upper,"DPI changed saved layout proportions");
        stage_=4;
    } else if(stage_==4 && tabs_[0].shell->settled() && tabs_[1].shell->settled() && selected(Group::main)->settled()) {
        query(L"SCALE150"); stage_=5;
    } else if(stage_==5 && complete("SCALE150")) {
        editor_check(); record("144-renaming");
        PostMessageW(rename_,WM_KEYDOWN,VK_RETURN,0); stage_=6;
    } else if(stage_==6 && !rename_) {
        require(main_tabs_[0].title==L"Pending DPI","DPI rename commit failed");
        SendMessageW(window_,WM_MBUTTONDOWN,0,MAKELPARAM(physical(main_group_.left)-1,physical(main_group_.top+10)));
        require(!rename_,"150-percent hit test crossed the rounded main tab boundary");
        mouse(WM_LBUTTONDOWN,12,tab_bar_.top+10);
        require(focused_==Group::bottom && active_==0 && GetFocus()==tabs_[0].shell->handle(),"scaled bottom tab hit test failed");
        shortcut(VK_ADD,true);
        require(tabs_[0].shell->zoom_percent()==125 && tabs_[1].shell->zoom_percent()==100 && selected(Group::main)->zoom_percent()==125,"focused zoom changed another session");
        const double left=state_.left;
        mouse(WM_LBUTTONDOWN,left_,100); require(GetCapture()==window_,"scaled divider did not capture");
        mouse(WM_MOUSEMOVE,left_+40,100); require(state_.left!=left,"scaled divider did not drag");
        transition(192);
        require(dragging_==Divider::none && GetCapture()!=window_ && state_.left==left,"DPI did not cancel active drag");
        require(GetFocus()==tabs_[0].shell->handle(),"DPI transition stole terminal focus");
        stage_=7;
    } else if(stage_==7 && tabs_[0].shell->settled() && tabs_[1].shell->settled() && selected(Group::main)->settled()) {
        query(L"SCALE200"); stage_=8;
    } else if(stage_==8 && complete("SCALE200")) {
        record("192-drag-cancelled");
        mouse(WM_LBUTTONDOWN,left_,100); mouse(WM_MOUSEMOVE,300,100); mouse(WM_LBUTTONUP,300,100);
        require(left_==300 && dragging_==Divider::none && GetCapture()!=window_,"200-percent divider commit failed");
        mouse(WM_MBUTTONDOWN,12,tab_bar_.top+10);
        require(rename_ && rename_group_==Group::bottom,"scaled bottom rename hit test failed");
        SetWindowTextW(rename_,L"Pending DPI"); transition(96); editor_check();
        PostMessageW(rename_,WM_KEYDOWN,VK_ESCAPE,0); stage_=9;
    } else if(stage_==9 && !rename_) {
        require(tabs_[0].title==L"cmd 1" && left_==300,"DPI restore lost title or layout");
        shortcut(VK_OEM_MINUS,true); require(active()->zoom_percent()==100,"Ctrl+minus failed after DPI restore");
        mouse(WM_LBUTTONDOWN,main_group_.left+12,main_group_.top+10); shortcut('0',true);
        require(selected(Group::main)->zoom_percent()==100 && tabs_[0].shell->zoom_percent()==100,"zoom reset failed after DPI restore");
        stage_=10;
    } else if(stage_==10 && tabs_[0].shell->settled() && tabs_[1].shell->settled() && selected(Group::main)->settled()) {
        query(L"RESTORED"); stage_=11;
    } else if(stage_==11 && complete("RESTORED")) {
        record("96-restored");
        Bitmap frame(physical(width_),physical(height_)); paint(frame.dc);
        const RECT main=physical(main_terminal_), bottom=physical(terminal_);
        selected(Group::main)->render_to(frame.dc,main.left,main.top);
        selected(Group::bottom)->render_to(frame.dc,bottom.left,bottom.top); frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path()/"dpi.json");
        out<<"{\"schema\":\"termforge-ui-dpi-v2\",\"controlled_messages\":true,\"real_monitor_transition\":false,\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"]}\n"; require(bool(out),"DPI geometry report write failed");
        // Odd physical extents at a fractional scale do not round-trip through
        // integer DIPs: 2001x1406 becomes 1143x803, then only 2000x1405. Check
        // the real HWND and painted endpoints, without changing the four-phase
        // public geometry schema above.
        RECT odd{0,0,2001,1406};
        require(AdjustWindowRectExForDpi(&odd,WS_OVERLAPPEDWINDOW,FALSE,0,GetDpiForWindow(window_))!=FALSE,"fractional DPI frame calculation failed");
        SendMessageW(window_,WM_DPICHANGED,MAKEWPARAM(168,168),reinterpret_cast<LPARAM>(&odd));
        RECT actual{}; require(GetClientRect(window_,&actual)!=FALSE && actual.right==2001 && actual.bottom==1406,"fractional DPI actual client mismatch");
        for(auto& tab:tabs_) require(actual_rect(tab.shell->handle()).right==2001,"fractional DPI bottom child missed last pixel");
        require(hit_x(2000)==width_-1 && hit_x(2001)==width_ && hit_y(1405)==height_-1 && hit_y(1406)==height_,"fractional DPI endpoint hit mismatch");
        Bitmap coverage(actual.right,actual.bottom);
        const HBRUSH sentinel=CreateSolidBrush(RGB(18,171,52)); require(sentinel!=nullptr,"DPI sentinel brush failed");
        FillRect(coverage.dc,&actual,sentinel); DeleteObject(sentinel);
        paint(coverage.dc); GdiFlush();
        for(int y=0;y<actual.bottom;++y) require(coverage.rgb(actual.right-1,y)!=0x12ab34,"fractional DPI right edge was not painted");
        for(int x=0;x<actual.right;++x) require(coverage.rgb(x,actual.bottom-1)!=0x12ab34,"fractional DPI bottom edge was not painted");
        transition(96);
        // Close immediately after a resize request; late font operations must
        // be ignored while the owned sessions are being cancelled.
        active()->change_zoom(25);
        stage_=100; SendMessageW(window_,WM_CLOSE,0,0);
        for(const auto group:{Group::bottom,Group::main}) for(auto& tab:group_tabs(group)) {
            const int dpi=tab.shell->display_dpi(), zoom=tab.shell->zoom_percent();
            tab.shell->set_dpi(144); tab.shell->change_zoom(25); tab.shell->reset_zoom();
            require(tab.shell->closing() && tab.shell->display_dpi()==dpi && tab.shell->zoom_percent()==zoom,"closing session accepted font change");
        }
    }
}
