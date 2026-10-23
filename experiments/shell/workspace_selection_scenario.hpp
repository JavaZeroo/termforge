// Real cmd text -> physical mouse messages -> independent copy observations.
#pragma once
inline void Workspace::scenario_selection() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<30000,"selection scenario exceeded its bounded deadline");
    const bool system=test_mode_=="ui-copy-system";
    auto* bottom=tabs_.front().shell.get();
    auto* main=selected(Group::main);
    const auto line_text=[](const auto& line) {
        std::string value; for(const auto& cell:line) value+=cell.text;
        while(!value.empty() && value.back()==' ') value.pop_back();
        return value;
    };
    const auto row_of=[&](ShellWindow* shell,const std::string& text) {
        const auto& view=shell->visible_snapshot();
        for(size_t row=0;row<view.lines.size();++row)
            if(line_text(view.lines[row])==text) return static_cast<int>(row);
        throw std::runtime_error("selection fixture output row is not visible: "+text);
    };
    const auto pixel=[](ShellWindow* shell,int column,int row) {
        const auto [width,height]=shell->pixel_cell_size();
        return POINT{column*width+width/4,row*height+height/2};
    };
    const auto mouse=[](ShellWindow* shell,UINT message,POINT point) {
        SendMessageW(shell->handle(),message,message==WM_MOUSEMOVE?MK_LBUTTON:0,MAKELPARAM(point.x,point.y));
    };
    const auto click=[&](ShellWindow* shell,POINT point) {
        mouse(shell,WM_LBUTTONDOWN,point); mouse(shell,WM_LBUTTONUP,point);
    };
    const auto drag=[&](ShellWindow* shell,POINT begin,POINT end) {
        mouse(shell,WM_LBUTTONDOWN,begin);
        require(GetCapture()==shell->handle(),"selection drag did not capture its session HWND");
        mouse(shell,WM_MOUSEMOVE,end); mouse(shell,WM_LBUTTONUP,end);
        require(GetCapture()!=shell->handle(),"selection drag did not release capture");
    };
    const auto clipboard=[&]() {
        require(system,"local selection scenario must not read the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        const auto value=tf_shell::read_unicode_clipboard(window_);
        const std::wstring wide(value.begin(),value.end()); return utf8(wide.c_str());
    };
    const auto sentinel=[&]() {
        require(system,"local selection scenario must not write the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        tf_shell::write_unicode_clipboard(window_,u"TF_COPY_SENTINEL 中文 e\u0301");
    };
    const auto copy=[&](ShellWindow* shell,bool workspace) -> std::optional<std::string> {
        if(!system) return std::nullopt;
        require(GetFocus()==shell->handle() && active()==shell,"copy action targets a different focused session");
        if(workspace) SendMessageW(window_,WM_COMMAND,14,0);
        else SendMessageW(shell->handle(),WM_COPY,0,0);
        return clipboard();
    };
    const auto record=[&](const char* phase,const char* entry="none",std::optional<std::string> cf=std::nullopt) {
        std::ostringstream out; out<<"{\"phase\":\""<<phase<<"\",\"copy_entry\":\""<<entry<<"\",\"clipboard\":";
        if(cf) termforge::Screen::quoted(out,*cf); else out<<"null";
        out<<",\"sessions\":[";
        bool first=true;
        for(const auto group:{Group::bottom,Group::main}) {
            auto* shell=selected(group); require(shell!=nullptr,"selection scenario requires both live regions");
            const auto value=shell->selected_text(); const auto state=shell->history_state();
            const auto& view=shell->visible_snapshot();
            size_t overlays=0;
            for(const auto& row:view.lines) for(const auto& cell:row)
                if(cell.style.bg==4 && cell.style.fg==15) ++overlays;
            const bool frame=shell->frame_matches_full();
            require(frame,"selection dirty drawing differs from a complete redraw");
            if(!first) out<<','; first=false;
            out<<"{\"group\":\""<<(group==Group::bottom?"bottom":"main")<<"\",\"selected\":"
               <<(shell->has_selection()?"true":"false")<<",\"focused\":"<<(GetFocus()==shell->handle()?"true":"false")
               <<",\"workspace_active\":"<<(active()==shell?"true":"false")<<",\"status\":\"";
            switch(value.status) {
                case termforge::SelectionCopyStatus::ok: out<<"ok"; break;
                case termforge::SelectionCopyStatus::empty: out<<"empty"; break;
                case termforge::SelectionCopyStatus::invalidated: out<<"invalidated"; break;
                case termforge::SelectionCopyStatus::limit_exceeded: out<<"limit-exceeded"; break;
            }
            out<<"\",\"text\":"; termforge::Screen::quoted(out,value.text);
            out<<",\"overlay_cells\":"<<overlays<<",\"frame_matches_full\":"<<(frame?"true":"false")
               <<",\"zoom\":"<<shell->zoom_percent()<<",\"top\":"<<state.top<<",\"total\":"<<state.total
               <<",\"page\":"<<state.page<<",\"maximum\":"<<state.maximum_top<<",\"anchor\":"<<state.anchor
               <<",\"visible_lines\":[";
            for(size_t row=0;row<view.lines.size();++row) { if(row) out<<','; termforge::Screen::quoted(out,line_text(view.lines[row])); }
            out<<"]}";
        }
        out<<"]}"; geometry_samples_.push_back(out.str());
    };
    const auto entry=[&](bool workspace) { return system?(workspace?"workspace-command-copy":"WM_COPY"):"none"; };
    if(stage_==0 && bottom->ready()) {
        if(system) tf_shell_test::require_hosted_clipboard();
        add(Group::main); stage_=1;
    } else if(stage_==1 && main && main->ready() && bottom->settled() && main->settled()) {
        bottom->command(L"cls\recho ALPHA  tail&echo BETA 中文 e\u0301&echo TF_SELECT_READY\r");
        main->command(L"cls\recho OTHER  text&echo TF_SELECT_OTHER\r"); stage_=2;
    } else if(stage_==2 && bottom->live_contains_line("TF_SELECT_READY") && main->live_contains_line("TF_SELECT_OTHER") &&
              bottom->settled() && main->settled()) {
        select(0,Group::bottom);
        SendMessageW(bottom->handle(),WM_KEYDOWN,VK_ESCAPE,0);
        if(system) sentinel();
        record("empty",entry(false),copy(bottom,false));
        const int alpha=row_of(bottom,"ALPHA  tail"), beta=row_of(bottom,"BETA 中文 e\xcc\x81");
        require(beta==alpha+1,"selection fixture has unexpected command echo or separator between output lines");
        drag(bottom,pixel(bottom,0,alpha),pixel(bottom,11,beta));
        record("forward",entry(false),copy(bottom,false));
        drag(bottom,pixel(bottom,11,beta),pixel(bottom,0,alpha));
        record("reverse",entry(true),copy(bottom,true));
        // Normal down/up sequences exercise click counting, not forced DBLCLK.
        const auto word=pixel(bottom,8,alpha);
        click(bottom,word); click(bottom,word);
        record("word",entry(false),copy(bottom,false));
        click(bottom,word); record("line",entry(true),copy(bottom,true));
        record("zoom-before"); bottom->change_zoom(25); stage_=3;
    } else if(stage_==3 && bottom->settled() && main && main->settled()) {
        record("zoom-after");
        select(0,Group::main);
        const int other=row_of(main,"OTHER  text");
        drag(main,pixel(main,0,other),pixel(main,11,other));
        record("main-selected",entry(false),copy(main,false));
        select(0,Group::bottom); record("bottom-restored",entry(true),copy(bottom,true));
        select(0,Group::main); bottom->fixture_command(L"echo TF_SELECT_APPEND\r"); stage_=4;
    } else if(stage_==4 && bottom->live_contains_line("TF_SELECT_APPEND")) {
        record("append-retained");
        bottom->fixture_command(L"for /L %i in (1,1,500) do @echo TF_SEL_%i\r"); stage_=5;
    } else if(stage_==5 && bottom->live_contains_line("TF_SEL_500") && bottom->live_contains_line("TF>")) {
        record("evicted"); select(0,Group::bottom);
        if(system) sentinel();
        record("invalid-copy",entry(false),copy(bottom,false));
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for(const auto group:{Group::bottom,Group::main}) {
            const RECT rect=physical(group==Group::main?main_terminal_:terminal_);
            selected(group)->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path()/"selection.json");
        out<<"{\"schema\":\"termforge-window-selection-v1\",\"system_clipboard\":"<<(system?"true":"false")
           <<",\"input_source\":\"physical-pixel-mouse-messages\",\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"]}\n"; out.close(); require(out.good(),"selection observations write failed");
        stage_=100; close_all();
    }
}
