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
    static std::string copy_popup_evidence, copy_selection_before;
    static DWORD copy_sequence_before=0, copy_sequence_after=0;
    static std::unique_ptr<tf_shell_test::ClipboardScenarioOwner> copy_owner;
    static std::optional<tf_shell_test::ClipboardOwnerReleaseResult> copy_owner_release;
    static DWORD occupied_sequence_before=0, occupied_sequence_error=0, occupied_sequence_joined=0, occupied_sequence_retry=0;
    static DWORD copy_native_error=0;
    static std::string copy_error_status, copy_error_title;
    static bool owner_before=false, owner_at_error=false, copy_error_visible=false;
    if(restore_keys) { require(SetKeyboardState(saved_keys.data())!=FALSE,"selection keyboard state restoration failed"); restore_keys=false; }
    const auto line_text=[](const auto& line) {
        return termforge::project_line(line).text;
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
        // A native PageUp exposes fewer fixture rows on a short workspace.
        // Put the same literal anchor at the opposite edge of the drag using
        // real scrollbar messages. Separate drags then also begin at different
        // pixels instead of accidentally becoming a double-click on short pages.
        const auto initial=main->history_state();
        const size_t offset=direction<0?initial.page-1:0;
        const size_t target=(std::min)(initial.maximum_top,anchor>offset?anchor-offset:size_t{0});
        for(size_t steps=0;;++steps) {
            const auto state=main->history_state();
            if(state.top==target) break;
            require(steps<82,"selection anchor reveal exceeded the fixed fixture");
            SendMessageW(main->handle(),WM_VSCROLL,target<state.top?SB_LINEUP:SB_LINEDOWN,0);
            require(main->history_state().top!=state.top,"selection anchor reveal made no scrollbar progress");
        }
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
        popup_gesture.reset(); popup_evidence.clear(); copy_popup_evidence.clear(); copy_selection_before.clear();
        copy_sequence_before=copy_sequence_after=0;
        copy_owner.reset(); copy_owner_release.reset(); copy_native_error=0; copy_error_status.clear(); copy_error_title.clear();
        occupied_sequence_before=occupied_sequence_error=occupied_sequence_joined=occupied_sequence_retry=0;
        owner_before=owner_at_error=copy_error_visible=false;
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
        // Zoom can move the still-valid selected row above a short viewport.
        // Reveal it before observing the existing visible-highlight contract.
        SendMessageW(bottom->handle(),WM_VSCROLL,SB_TOP,0);
        record("zoom-after");
        select(0,Group::main);
        const int other=row_of(main,"OTHER  text");
        drag(main,pixel(main,0,other),pixel(main,11,other));
        record("main-selected",entry(false),copy(main,false));
        select(0,Group::bottom); record("bottom-restored",entry(true),copy(bottom,true));
        select(0,Group::main);
        SendMessageW(bottom->handle(),WM_VSCROLL,SB_BOTTOM,0);
        bottom->fixture_command(L"echo TF_SELECT_APPEND\r"); stage_=4;
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
        require(GetFocus()==main->handle() && active()==main && main->has_selection(),"actual Copy popup lost its selected target");
        const auto selected=main->selected_text();
        require(selected.status==termforge::SelectionCopyStatus::ok && !selected.text.empty(),"actual Copy popup requires nonempty valid text");
        copy_selection_before=selected.text;
        sentinel(); require(clipboard()=="TF_COPY_SENTINEL 中文 e\xcc\x81","actual Copy popup sentinel readback differs");
        copy_sequence_before=GetClipboardSequenceNumber(); require(copy_sequence_before!=0,"actual Copy popup clipboard sequence unavailable");
        interaction("system-copy-popup-prepared","prepare-Copy-popup",false,clipboard());
        popup_gesture=std::make_unique<tf_shell_test::PopupMenuGesture>(window_,POINT{physical(30),physical(15)},14);
        stage_=23;
    } else if(stage_==23 && system && popup_gesture && popup_gesture->complete()) {
        copy_popup_evidence=popup_gesture->json();
        std::ofstream observed(directory/"selection-copy-popup.json",std::ios::binary); observed<<copy_popup_evidence<<'\n'; observed.close();
        require(observed.good(),"actual Copy popup observations write failed");
        try { popup_gesture->validate(); } catch(...) { popup_gesture.reset(); throw; }
        popup_gesture.reset(); require(tf_shell_test::PopupMenuGesture::observer_idle(),"Copy popup observer survived its actual menu workflow");
        const auto selected=main->selected_text(); const auto cf=clipboard();
        copy_sequence_after=GetClipboardSequenceNumber();
        require(GetFocus()==main->handle() && active()==main && main->has_selection() &&
                selected.status==termforge::SelectionCopyStatus::ok && selected.text==copy_selection_before &&
                cf==copy_selection_before && copy_sequence_after!=0 && copy_sequence_after!=copy_sequence_before &&
                main->status_text()==L"Selected text copied","actual Copy popup did not replace its sentinel for the focused selected target");
        interaction("system-copy-popup","actual-Session-popup-Copy",false,cf);
        sentinel(); require(clipboard()=="TF_COPY_SENTINEL 中文 e\xcc\x81","occupied Copy sentinel readback differs");
        occupied_sequence_before=GetClipboardSequenceNumber(); require(occupied_sequence_before!=0,"occupied Copy clipboard sequence unavailable");
        interaction("system-copy-occupied-prepared","prepare-occupied-Copy",false,clipboard());
        copy_owner=std::make_unique<tf_shell_test::ClipboardScenarioOwner>(false);
        owner_before=copy_owner->occupied();
        require(owner_before && GetClipboardSequenceNumber()==occupied_sequence_before,"Copy fixture did not retain its real clipboard lock/sentinel");
        queued_key('C',true,true); stage_=24;
    } else if(stage_==24 && system && main->status_text().rfind(L"Copy failed: OpenClipboard: ",0)==0) {
        const auto status=main->status_text(); const std::wstring prefix=L"Copy failed: OpenClipboard: ";
        copy_error_status=utf8(status.c_str());
        uint64_t error=0; require(status.size()>prefix.size(),"Copy failure omitted its actual native error");
        for(size_t index=prefix.size();index<status.size();++index) {
            require(status[index]>=L'0' && status[index]<=L'9' && error<=0xffffffffu/10,"Copy failure native error is malformed");
            error=error*10+static_cast<unsigned>(status[index]-L'0'); require(error<=0xffffffffu,"Copy failure native error overflow");
        }
        copy_native_error=static_cast<DWORD>(error); owner_at_error=copy_owner && copy_owner->occupied();
        occupied_sequence_error=GetClipboardSequenceNumber();
        wchar_t title[2048]{}; const int length=GetWindowTextW(main->handle(),title,2048);
        require(length>0 && length<2047,"Copy error native window title unavailable"); copy_error_title=utf8(title);
        const auto selection_value=main->selected_text();
        copy_error_visible=IsWindowVisible(main->handle()) && GetFocus()==main->handle() && active()==main;
        interaction("system-copy-occupied","queued-Ctrl-Shift-C-occupied");
        require(copy_native_error!=0 && owner_at_error && occupied_sequence_error==occupied_sequence_before && copy_error_visible &&
                copy_error_title=="TermForge — "+copy_error_status && main->has_selection() &&
                selection_value.status==termforge::SelectionCopyStatus::ok && selection_value.text==copy_selection_before,
                "occupied product Copy failed without preserving its actual visible error, selected target or clipboard sequence");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for(const auto group:{Group::bottom,Group::main}) {
            const RECT rect=physical(group==Group::main?main_terminal_:terminal_); selected(group)->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save((directory/"selection-copy-occupied-error.bmp").string());
        copy_owner->release(); stage_=25;
    } else if(stage_==25 && system && copy_owner && copy_owner->released()) {
        copy_owner_release=copy_owner->finish(); copy_owner.reset();
        require(copy_owner_release->reason==tf_shell_test::ClipboardReleaseReason::ordinary && !copy_owner_release->expected_requester_gone,
                "ordinary occupied Copy owner used a requester-exit exception");
        const auto cf=clipboard(); occupied_sequence_joined=GetClipboardSequenceNumber();
        require(cf=="TF_COPY_SENTINEL 中文 e\xcc\x81" && occupied_sequence_joined==occupied_sequence_before &&
                GetFocus()==main->handle() && active()==main && main->has_selection() && main->selected_text().text==copy_selection_before,
                "failed Copy changed the actual clipboard/selection/focus before its owner joined");
        interaction("system-copy-occupied-released","release-and-join-occupied-owner",false,cf);
        queued_key('C',true,true); stage_=26;
    } else if(stage_==26 && system && main->status_text()==L"Selected text copied") {
        const auto cf=clipboard(); occupied_sequence_retry=GetClipboardSequenceNumber();
        require(!copy_owner && copy_owner_release && cf==copy_selection_before && occupied_sequence_retry!=0 &&
                occupied_sequence_retry!=occupied_sequence_before && GetFocus()==main->handle() && active()==main &&
                main->has_selection() && main->selected_text().text==copy_selection_before,
                "queued Copy retry did not copy its original selected target after real owner release/join");
        interaction("system-copy-occupied-retry","queued-Ctrl-Shift-C-retry",false,cf);
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
        out<<"{\"schema\":\""<<(system?"termforge-window-selection-v5":"termforge-window-selection-v3")<<"\",\"system_clipboard\":"<<(system?"true":"false")
           <<",\"input_source\":\"physical-pixel-mouse-messages\",\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"],\"interaction_input\":\"queued-keys-through-workspace-loop-and-native-timer-capture\",\"menu_popup_verified\":true,\"popup_observer_idle\":"
           <<(tf_shell_test::PopupMenuGesture::observer_idle()?"true":"false")<<",\"popup_menu\":"
           <<popup_evidence;
        if(system) out<<",\"copy_popup_verified\":true,\"copy_popup_observer_idle\":"
           <<(tf_shell_test::PopupMenuGesture::observer_idle()?"true":"false")<<",\"copy_popup_sequence_before\":"<<copy_sequence_before
           <<",\"copy_popup_sequence_after\":"<<copy_sequence_after<<",\"copy_popup_menu\":"<<copy_popup_evidence;
        if(system) {
            require(copy_owner_release.has_value() && !copy_owner,"occupied Copy owner was not joined before final evidence");
            const auto& release=*copy_owner_release; const auto& observed=release.observation;
            const auto flag=[&](const char* name,bool value) { out<<",\""<<name<<"\":"<<(value?"true":"false"); };
            out<<",\"copy_contention\":{\"schema\":\"termforge-product-copy-contention-v1\",\"entry\":\"queued-Ctrl-Shift-C\",\"target\":\"main\"";
            flag("owner_occupied_before",owner_before); flag("owner_occupied_at_error",owner_at_error); flag("error_visible",copy_error_visible);
            out<<",\"native_error\":"<<copy_native_error<<",\"failure_step\":\"OpenClipboard\",\"error_status\":";
            termforge::Screen::quoted(out,copy_error_status); out<<",\"native_window_title\":"; termforge::Screen::quoted(out,copy_error_title);
            out<<",\"error_frame\":\"selection-copy-occupied-error.bmp\",\"sequence_before\":"<<occupied_sequence_before
               <<",\"sequence_at_error\":"<<occupied_sequence_error<<",\"sequence_after_owner_join\":"<<occupied_sequence_joined
               <<",\"sequence_after_retry\":"<<occupied_sequence_retry<<",\"owner_release\":{\"reason\":\"ordinary\",\"proof\":{\"owner_entered\":"
               <<(release.proof.owner_entered?"true":"false")<<",\"requester_exited\":"<<(release.proof.requester_exited?"true":"false")
               <<",\"target_quiescent\":"<<(release.proof.target_quiescent?"true":"false")<<",\"payload_unchanged\":"<<(release.proof.payload_unchanged?"true":"false")<<'}';
            flag("publication_attempted",observed.publication_attempted); flag("publication_succeeded",observed.publication_succeeded);
            flag("release_event",observed.release_event); flag("thread_exited",observed.thread_exited);
            out<<",\"native_error\":"<<observed.native_error<<",\"worker_error\":"<<observed.worker_error<<",\"failure_step\":";
            termforge::Screen::quoted(out,std::string(observed.failure_step)); out<<",\"secondary_native_error\":"<<observed.secondary_native_error
               <<",\"secondary_worker_error\":"<<observed.secondary_worker_error<<",\"secondary_failure_step\":";
            termforge::Screen::quoted(out,std::string(observed.secondary_failure_step)); flag("expected_requester_gone",release.expected_requester_gone); out<<"}}";
        }
        out<<",\"interactions\":[";
        for(size_t i=0;i<interaction_samples.size();++i) { if(i) out<<','; out<<interaction_samples[i]; }
        out<<"]}\n"; out.close(); require(out.good(),"selection observations write failed");
        stage_=100; close_all();
    }
}
