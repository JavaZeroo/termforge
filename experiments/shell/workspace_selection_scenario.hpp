// Real cmd text -> physical mouse messages -> independent copy observations.
#pragma once
#include <array>
#include <iomanip>
#include <iterator>
inline void Workspace::scenario_selection() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<30000,"selection scenario exceeded its bounded deadline");
    const bool system=test_mode_=="ui-copy-system";
    auto* bottom=tabs_.front().shell.get();
    auto* main=selected(Group::main);
    const auto directory=std::filesystem::path(image_).parent_path();
    static std::vector<std::string> interaction_samples;
    static std::array<BYTE,256> saved_keys{};
    static bool restore_keys=false;
    static int phase_tick=0;
    static size_t phase_top=0;
    static unsigned anchor_line=0;
    static int scroll_direction=0;
    static std::unique_ptr<tf_shell_test::PopupMenuGesture> popup_gesture;
    static std::string popup_evidence;
    if(restore_keys) { require(SetKeyboardState(saved_keys.data())!=FALSE,"selection keyboard state restoration failed"); restore_keys=false; }
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
    const auto capture=[&](const char* name) {
        const auto path=directory/name;
        if(!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path)<=4096,"selection raw receiver exceeded its observation bound");
        std::ifstream in(path,std::ios::binary); require(bool(in),"selection raw capture read failed");
        return std::string(std::istreambuf_iterator<char>(in),{});
    };
    const auto fixture_line=[](unsigned index) {
        std::ostringstream out; out<<"TF_FIND_"<<std::setfill('0')<<std::setw(3)<<index<<" needle 中文 e\xcc\x81";
        return out.str();
    };
    const auto queued_key=[&](WPARAM key,bool control=false,bool shift=false) {
        require(main && GetFocus()==main->handle() && active()==main,"selection shortcut lost its actual target focus");
        require(GetKeyboardState(saved_keys.data())!=FALSE,"selection keyboard state read failed");
        auto modified=saved_keys;
        if(control) modified[VK_CONTROL]|=0x80;
        if(shift) modified[VK_SHIFT]|=0x80;
        require(SetKeyboardState(modified.data())!=FALSE,"selection keyboard state setup failed"); restore_keys=true;
        const LPARAM flags=1|(static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC))<<16);
        require(PostMessageW(main->handle(),WM_KEYDOWN,key,flags)!=FALSE &&
                PostMessageW(main->handle(),WM_KEYUP,key,flags|(static_cast<LPARAM>(3)<<30))!=FALSE,
                "selection shortcut could not enter the real Workspace message loop");
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
        std::ofstream progress(directory/"selection-base.jsonl",std::ios::binary|std::ios::app);
        progress<<out.str()<<'\n'; progress.close(); require(progress.good(),"selection base progress write failed");
    };
    const auto interaction=[&](const char* phase,const char* action="none",bool transferred=false,std::optional<std::string> cf=std::nullopt) {
        const auto value=main->selected_text(); const auto state=main->history_state();
        SCROLLINFO bar{sizeof(bar),SIF_POS};
        require(GetScrollInfo(main->handle(),SB_VERT,&bar)!=FALSE && bar.nPos==static_cast<int>(state.top),
                "selection autoscroll UI bar and actual viewport disagree");
        const auto& view=main->visible_snapshot(); size_t highlighted=0;
        for(const auto& row:view.lines) for(const auto& cell:row)
            if(cell.style.bg==4 && cell.style.fg==15) ++highlighted;
        require(main->frame_matches_full(),"selection interaction dirty frame differs from full redraw");
        std::ostringstream out;
        out<<"{\"phase\":\""<<phase<<"\",\"action\":\""<<action<<"\",\"ticks\":"<<main->observed_tick_count()
           <<",\"top\":"<<state.top<<",\"maximum\":"<<state.maximum_top<<",\"page\":"<<state.page
           <<",\"scrollbar_top\":"<<bar.nPos<<",\"anchor_line\":"<<anchor_line<<",\"direction\":"<<scroll_direction
           <<",\"captured\":"<<(GetCapture()==main->handle()?"true":"false")
           <<",\"capture_transferred\":"<<(transferred?"true":"false")
           <<",\"main_focused\":"<<(GetFocus()==main->handle()?"true":"false")
           <<",\"main_active\":"<<(active()==main?"true":"false")<<",\"selected\":"<<(main->has_selection()?"true":"false")
           <<",\"status\":\""<<(value.status==termforge::SelectionCopyStatus::ok?"ok":
                value.status==termforge::SelectionCopyStatus::empty?"empty":"invalidated")<<"\",\"text\":";
        termforge::Screen::quoted(out,value.text);
        out<<",\"overlay_cells\":"<<highlighted<<",\"frame_matches_full\":true,\"visible_lines\":[";
        for(size_t row=0;row<view.lines.size();++row) { if(row) out<<','; termforge::Screen::quoted(out,line_text(view.lines[row])); }
        out<<"],\"raw_bottom\":"; termforge::Screen::quoted(out,capture("selection-bottom.bin"));
        out<<",\"raw_main\":"; termforge::Screen::quoted(out,capture("selection-main.bin"));
        out<<",\"product_status\":"; termforge::Screen::quoted(out,utf8(main->status_text().c_str()));
        out<<",\"clipboard\":"; if(cf) termforge::Screen::quoted(out,*cf); else out<<"null"; out<<'}';
        interaction_samples.push_back(out.str());
        std::ofstream progress(directory/"selection-interactions.jsonl",std::ios::binary|std::ios::app);
        progress<<out.str()<<'\n'; progress.close(); require(progress.good(),"selection interaction progress write failed");
    };
    const auto begin_autoscroll=[&](unsigned anchor,int direction) {
        select(0,Group::main); anchor_line=anchor; scroll_direction=direction;
        const int row=row_of(main,fixture_line(anchor));
        mouse(main,WM_LBUTTONDOWN,pixel(main,0,row));
        require(GetCapture()==main->handle(),"autoscroll drag has no actual capture");
        const auto [width,height]=main->pixel_cell_size();
        const int extent=static_cast<int>(main->history_state().page)*height;
        mouse(main,WM_MOUSEMOVE,POINT{width/4,direction<0?-height:extent+height});
        phase_tick=main->observed_tick_count(); phase_top=main->history_state().top;
    };
    const auto entry=[&](bool workspace) { return system?(workspace?"workspace-command-copy":"WM_COPY"):"none"; };
    if(stage_==0 && bottom->ready()) {
        interaction_samples.clear();
        popup_gesture.reset(); popup_evidence.clear();
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
        bottom->start_text_receiver(directory/L"selection-bottom.bin");
        main->start_text_receiver(directory/L"selection-main.bin"); stage_=6;
    } else if(stage_==6 && bottom->live_contains_line("TF_FIND_READY 080") && main->live_contains_line("TF_FIND_READY 080") &&
              main->history_state().total==82 && line_text(main->visible_snapshot().lines.back()).empty()) {
        select(0,Group::main); SendMessageW(main->handle(),WM_VSCROLL,SB_BOTTOM,0);
        begin_autoscroll(75,-1); interaction("autoscroll-up-start"); stage_=7;
    } else if(stage_==7 && main->observed_tick_count()-phase_tick>=3) {
        const auto ticks=main->observed_tick_count()-phase_tick;
        require(ticks<=12 && phase_top-main->history_state().top==static_cast<size_t>(ticks),"up autoscroll is not bounded to one row per real timer tick");
        interaction("autoscroll-up-held");
        const auto [width,height]=main->pixel_cell_size(); mouse(main,WM_LBUTTONUP,POINT{width/4,-height});
        interaction("autoscroll-up-release","mouse-up"); phase_tick=main->observed_tick_count(); phase_top=main->history_state().top; stage_=8;
    } else if(stage_==8 && main->observed_tick_count()-phase_tick>=3) {
        require(main->history_state().top==phase_top,"autoscroll continued after mouse release");
        interaction("autoscroll-up-stopped"); SendMessageW(main->handle(),WM_VSCROLL,SB_PAGEUP,0);
        begin_autoscroll(50,1); interaction("autoscroll-down-start"); stage_=9;
    } else if(stage_==9 && main->observed_tick_count()-phase_tick>=3) {
        const auto ticks=main->observed_tick_count()-phase_tick;
        require(ticks<=12 && main->history_state().top-phase_top==static_cast<size_t>(ticks),"down autoscroll is not bounded to one row per real timer tick");
        interaction("autoscroll-down-held"); select(0,Group::bottom);
        require(GetCapture()!=main->handle(),"focus loss retained the selection's capture");
        interaction("autoscroll-focus-release","focus-loss"); phase_tick=main->observed_tick_count(); phase_top=main->history_state().top; stage_=10;
    } else if(stage_==10 && main->observed_tick_count()-phase_tick>=3) {
        require(main->history_state().top==phase_top,"autoscroll continued after focus loss");
        interaction("autoscroll-focus-stopped"); select(0,Group::main);
        SendMessageW(main->handle(),WM_VSCROLL,SB_PAGEUP,0);
        begin_autoscroll(40,-1); interaction("autoscroll-capture-start"); stage_=11;
    } else if(stage_==11 && main->observed_tick_count()-phase_tick>=3) {
        const auto ticks=main->observed_tick_count()-phase_tick;
        require(ticks<=12 && phase_top-main->history_state().top==static_cast<size_t>(ticks),"capture-loss fixture did not exercise bounded upward autoscroll");
        interaction("autoscroll-capture-held");
        require(SetCapture(window_)==main->handle() && GetCapture()==window_,"capture was not transferred to the actual Workspace HWND");
        require(ReleaseCapture()!=FALSE,"Workspace test capture release failed");
        interaction("autoscroll-capture-release","capture-loss",true); phase_tick=main->observed_tick_count(); phase_top=main->history_state().top; stage_=12;
    } else if(stage_==12 && main->observed_tick_count()-phase_tick>=3) {
        require(main->history_state().top==phase_top,"autoscroll continued after capture loss");
        interaction("autoscroll-capture-stopped"); queued_key('A',true,true); stage_=13;
    } else if(stage_==13 && main->has_selection() && main->selected_text().text.find("TF_FIND_000")==0) {
        interaction("select-all-shortcut","queued-Ctrl-Shift-A"); queued_key(VK_ESCAPE); stage_=14;
    } else if(stage_==14 && !main->has_selection()) {
        interaction("select-all-shortcut-cleared","queued-Escape");
        SendMessageW(window_,WM_COMMAND,15,0); interaction("select-all-command","workspace-command-15");
        queued_key(VK_ESCAPE); stage_=15;
    } else if(stage_==15 && !main->has_selection()) {
        interaction("select-all-command-cleared","queued-Escape");
        require(PostMessageW(main->handle(),WM_CHAR,L'x',1)!=FALSE,"selection final input could not enter the Workspace loop"); stage_=16;
    } else if(stage_==16 && capture("selection-main.bin")=="x") {
        phase_tick=main->observed_tick_count(); stage_=17;
    } else if(stage_==17 && main->observed_tick_count()-phase_tick>=3) {
        interaction("input-after-select-all");
        require(!main->has_selection(),"empty Copy shortcut fixture unexpectedly retained a selection");
        if(system) sentinel();
        queued_key('C',true,true); stage_=18;
    } else if(stage_==18 && main->status_text()==L"No selected text") {
        interaction("empty-copy-shortcut","queued-Ctrl-Shift-C-empty",false,system?std::optional<std::string>(clipboard()):std::nullopt);
        popup_gesture=std::make_unique<tf_shell_test::PopupMenuGesture>(window_,POINT{physical(30),physical(15)},15);
        stage_=19;
    } else if(stage_==19 && popup_gesture && popup_gesture->complete()) {
        popup_evidence=popup_gesture->json();
        std::ofstream observed(directory/"selection-popup.json",std::ios::binary); observed<<popup_evidence<<'\n'; observed.close();
        require(observed.good(),"actual popup observations write failed");
        try { popup_gesture->validate(); } catch(...) { popup_gesture.reset(); throw; }
        popup_gesture.reset(); require(tf_shell_test::PopupMenuGesture::observer_idle(),"popup observer survived its actual menu workflow");
        interaction("select-all-popup","actual-Session-popup-Select-All");
        if(system) { sentinel(); queued_key('C',true,true); stage_=20; }
        else { queued_key(VK_ESCAPE); stage_=21; }
    } else if(stage_==20 && system && main->status_text()==L"Selected text copied") {
        interaction("system-copy-shortcut","queued-Ctrl-Shift-C",false,clipboard());
        queued_key(VK_ESCAPE); stage_=21;
    } else if(stage_==21 && !main->has_selection()) {
        interaction("select-all-popup-cleared","queued-Escape"); phase_tick=main->observed_tick_count(); stage_=22;
    } else if(stage_==22 && main->observed_tick_count()-phase_tick>=3) {
        interaction("input-after-popup");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for(const auto group:{Group::bottom,Group::main}) {
            const RECT rect=physical(group==Group::main?main_terminal_:terminal_);
            selected(group)->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path()/"selection.json");
        out<<"{\"schema\":\"termforge-window-selection-v3\",\"system_clipboard\":"<<(system?"true":"false")
           <<",\"input_source\":\"physical-pixel-mouse-messages\",\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"],\"interaction_input\":\"queued-keys-through-workspace-loop-and-native-timer-capture\",\"menu_popup_verified\":true,\"popup_observer_idle\":"
           <<(tf_shell_test::PopupMenuGesture::observer_idle()?"true":"false")<<",\"popup_menu\":"
           <<popup_evidence<<",\"interactions\":[";
        for(size_t i=0;i<interaction_samples.size();++i) { if(i) out<<','; out<<interaction_samples[i]; }
        out<<"]}\n"; out.close(); require(out.good(),"selection observations write failed");
        stage_=100; close_all();
    }
}
