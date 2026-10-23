// Actual terminal/Session popups and queued workspace shortcuts, with raw PTYs.
#pragma once
#include <array>
#include <iomanip>
inline void Workspace::scenario_actions() {
    using tf_shell::TextAction;
    if(image_.empty() || closing_) return;
    require(ms(started_)<35000,("actions scenario timeout stage "+std::to_string(stage_)).c_str());
    const bool system=test_mode_=="ui-actions-system";
    const auto directory=std::filesystem::path(image_).parent_path();
    static std::unique_ptr<tf_shell_test::PopupMenuGesture> gesture;
    static std::array<BYTE,256> original_keys{};
    static bool restore_keys=false;
    static std::string last_popup;
    static uint64_t before_id=0,before_version=0,after_id=0,after_version=0;
    static std::array<DWORD,2> refocus_before{},refocus_after{};
    static std::wstring refocus_query,refocus_selected;
    static bool refocus_focused=false;
    if(restore_keys) { require(SetKeyboardState(original_keys.data())!=FALSE,"actions modifiers restore failed"); restore_keys=false; }
    if(gesture && gesture->complete()) {
        gesture->validate(); last_popup=gesture->json(); gesture.reset();
        require(tf_shell_test::PopupMenuGesture::observer_idle(),"actions popup observer did not clean up");
    }
    auto* bottom=selected(Group::bottom); auto* main=selected(Group::main);
    if(stage_>=80 && stage_<=84) for(const auto item:{std::pair<const char*,ShellWindow*>{"bottom",bottom},{"main",main}}) if(item.second) {
        const auto output=item.second->observed_output(); require(output.size()<=65536,"actions geometry transport bound exceeded");
        std::ofstream transport(directory/(std::string("actions-geometry-")+item.first+".transport.bin"),std::ios::binary);
        transport.write(output.data(),static_cast<std::streamsize>(output.size())); transport.close(); require(transport.good(),"actions geometry transport write failed");
    }
    if((stage_==70 || stage_==71) && main) {
        const auto output=main->observed_output(); require(output.size()<=16384,"actions overwrite transport bound exceeded");
        std::ofstream transport(directory/"actions-overwrite.transport.bin",std::ios::binary); transport.write(output.data(),static_cast<std::streamsize>(output.size()));
        transport.close(); require(transport.good(),"actions overwrite transport write failed");
    }
    const auto capture=[&](const char* name) {
        const auto path=directory/(std::string("actions-")+name+".bin");
        if(!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path)<=16384,"actions raw capture bound exceeded");
        std::ifstream in(path,std::ios::binary); require(bool(in),"actions raw capture missing"); return std::string(std::istreambuf_iterator<char>(in),{});
    };
    const auto line=[](const auto& row) { std::string text; for(const auto& cell:row) text+=cell.text; while(!text.empty() && text.back()==' ') text.pop_back(); return text; };
    const auto transition=[&](int dpi) {
        RECT next{0,0,MulDiv(1360,dpi,96),MulDiv(860,dpi,96)}; const UINT os_dpi=GetDpiForWindow(window_);
        require(os_dpi && AdjustWindowRectExForDpi(&next,WS_OVERLAPPEDWINDOW,FALSE,0,os_dpi),"actions controlled DPI frame calculation failed");
        SendMessageW(window_,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&next));
        require(dpi_==dpi && width_==1360 && height_==860,"actions controlled DPI changed logical client");
    };
    const auto key=[&](HWND target,WPARAM code,bool ctrl=false,bool shift=false) {
        require(target && GetFocus()==target,"actions queued key lost its actual focused control");
        require(GetKeyboardState(original_keys.data())!=FALSE,"actions modifiers read failed"); auto modified=original_keys;
        for(const int modifier:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU}) modified[static_cast<size_t>(modifier)]=0;
        modified[VK_CONTROL]=ctrl?0x80:0; modified[VK_SHIFT]=shift?0x80:0; modified[VK_MENU]=0;
        require(SetKeyboardState(modified.data())!=FALSE,"actions modifiers setup failed"); restore_keys=true;
        const LPARAM flags=1|(static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(code),MAPVK_VK_TO_VSC))<<16);
        require(PostMessageW(target,WM_KEYDOWN,code,flags) && PostMessageW(target,WM_KEYUP,code,flags|(LPARAM(3)<<30)),"actions real-loop key queue failed");
    };
    const auto popup=[&](ShellWindow* target,UINT command,bool session=false) {
        require(!gesture && last_popup.empty(),"actions popup overlap or unconsumed evidence");
        if(session) gesture=std::make_unique<tf_shell_test::PopupMenuGesture>(window_,POINT{physical(20),physical(16)},command);
        else {
            require(target && !target->closing(),"actions terminal popup target unavailable");
            const auto [width,height]=target->pixel_cell_size(); POINT at{width*2,height*2};
            require(ClientToScreen(target->handle(),&at)!=FALSE,"actions context popup screen mapping failed");
            gesture=std::make_unique<tf_shell_test::PopupMenuGesture>(target->handle(),at,command,2000,WM_CONTEXTMENU);
        }
    };
    const auto clipboard=[&]()->std::string {
        require(system,"local actions must never access system clipboard"); tf_shell_test::require_hosted_clipboard();
        const auto text=tf_shell::read_unicode_clipboard(window_,16384);
        return utf8(std::wstring(reinterpret_cast<const wchar_t*>(text.data()),text.size()).c_str());
    };
    const auto write_cf=[&](std::u16string_view text) { require(system,"local actions must never write system clipboard"); tf_shell_test::require_hosted_clipboard(); tf_shell::write_unicode_clipboard(window_,text); };
    const auto record=[&](const char* phase,const char* entry="none",bool with_popup=false,bool with_clipboard=false) {
        RECT parent{}; require(GetClientRect(window_,&parent),"actions physical client query failed");
        std::ostringstream out; out<<"{\"phase\":\""<<phase<<"\",\"entry\":\""<<entry<<"\",\"native_stage\":"<<stage_<<",\"session_count\":"<<session_count()
            <<",\"controlled_dpi\":"<<dpi_<<",\"os_window_dpi\":"<<GetDpiForWindow(window_)<<",\"logical_client\":["<<width_<<','<<height_
            <<"],\"physical_client\":["<<parent.right<<','<<parent.bottom<<"],\"workspace_actions\":{";
        bool first=true;
        for(const auto item:{std::pair<const char*,TextAction>{"copy",TextAction::copy},{"paste",TextAction::paste},{"all",TextAction::select_all},{"find",TextAction::find},{"top",TextAction::history_top},{"bottom",TextAction::history_bottom},{"cancel",TextAction::cancel_paste}}) {
            if(!first) out<<','; first=false; const auto state=text_action_state(item.second);
            out<<'"'<<item.first<<"\":{\"enabled\":"<<(state.enabled?"true":"false")<<",\"reason\":\""<<tf_shell::text_action_block_name(state.reason)<<"\"}";
        }
        out<<"},\"focus\":\""<<(rename_ && GetFocus()==rename_?"rename":bottom && GetFocus()==bottom->handle()?"bottom":main && GetFocus()==main->handle()?"main":bottom && GetFocus()==bottom->find_edit_handle()?"bottom-find":main && GetFocus()==main->find_edit_handle()?"main-find":main && main->paste_prompt_handle() && IsChild(main->paste_prompt_handle(),GetFocus())?"main-prompt":"other")<<"\",\"rename_text\":";
        if(rename_) { wchar_t text[65]{}; GetWindowTextW(rename_,text,65); termforge::Screen::quoted(out,utf8(text)); } else out<<"null";
        out<<",\"raw\":{\"bottom\":"; termforge::Screen::quoted(out,capture("bottom")); out<<",\"main\":"; termforge::Screen::quoted(out,capture("main")); out<<"},\"clipboard\":";
        if(with_clipboard) termforge::Screen::quoted(out,clipboard()); else out<<"null";
        out<<",\"overwrite\":{\"before_id\":"<<before_id<<",\"before_version\":"<<before_version<<",\"after_id\":"<<after_id<<",\"after_version\":"<<after_version<<'}';
        out<<",\"find_refocus\":";
        if(std::string_view(phase)=="find-editor" || std::string_view(phase)=="find-refocus-failure") {
            out<<"{\"input_path\":\"queued-Ctrl-Shift-F-in-Find-EDIT\",\"before\":["<<refocus_before[0]<<','<<refocus_before[1]
                <<"],\"after\":["<<refocus_after[0]<<','<<refocus_after[1]<<"],\"query_before\":";
            termforge::Screen::quoted(out,utf8(refocus_query.c_str())); out<<",\"selected\":"; termforge::Screen::quoted(out,utf8(refocus_selected.c_str()));
            out<<",\"focused\":"<<(refocus_focused?"true":"false")<<'}';
        } else out<<"null";
        out<<",\"popup\":"; if(with_popup) { require(!last_popup.empty(),"actions actual popup evidence missing"); out<<last_popup; last_popup.clear(); } else out<<"null";
        out<<",\"sessions\":["; first=true;
        for(const auto group:{Group::bottom,Group::main}) if(auto* target=selected(group)) {
            if(!first) out<<','; first=false;
            const auto state=target->history_state(); const auto selected=target->selected_text(); const auto match=target->current_find_range();
            const auto [columns,rows]=target->grid_dimensions(); const auto [cell_width,cell_height]=target->pixel_cell_size();
            size_t overlay=0; for(const auto& row:target->visible_snapshot().lines) for(const auto& cell:row) if(cell.style.bg==4 && cell.style.fg==15) ++overlay;
            out<<"{\"group\":\""<<(group==Group::bottom?"bottom":"main")<<"\",\"closing\":"<<(target->closing()?"true":"false")
                <<",\"active\":"<<(active()==target?"true":"false")<<",\"focused\":"<<(GetFocus()==target->handle()?"true":"false")
                <<",\"selected\":"<<(target->has_selection()?"true":"false")<<",\"selection_status\":\""<<(selected.status==termforge::SelectionCopyStatus::ok?"ok":selected.status==termforge::SelectionCopyStatus::invalidated?"invalidated":selected.status==termforge::SelectionCopyStatus::limit_exceeded?"limit-exceeded":"empty")<<"\",\"selection\":"; termforge::Screen::quoted(out,selected.text);
            out<<",\"paste_busy\":"<<(target->paste_busy()?"true":"false")<<",\"prompt\":"<<(target->paste_prompt_handle()?"true":"false")
                <<",\"prompt_focus\":"<<(target->paste_prompt_handle() && IsChild(target->paste_prompt_handle(),GetFocus())?GetDlgCtrlID(GetFocus()):0)
                <<",\"find\":"<<(target->find_handle()?"true":"false")<<",\"query\":";
            if(target->find_edit_handle()) { wchar_t text[4097]{}; GetWindowTextW(target->find_edit_handle(),text,4097); termforge::Screen::quoted(out,utf8(text)); } else out<<"null";
            out<<",\"find_line\":"; std::string matched;
            if(match) for(const auto& row:target->visible_snapshot().lines) if(row.id==match->begin.line_id) matched=line(row);
            if(match) termforge::Screen::quoted(out,matched); else out<<"null";
            out<<",\"find_range\":";
            if(match) out<<"{\"id\":"<<match->begin.line_id<<",\"version\":"<<match->begin.version<<",\"begin\":"<<match->begin.column<<",\"end\":"<<match->end.column<<'}'; else out<<"null";
            const auto valid_ranges=target->valid_find_ranges(); require(valid_ranges.size()<=1024,"actions live Find range bound exceeded");
            out<<",\"search_generation\":"<<target->search_generation()<<",\"valid_find_ranges\":[";
            for(size_t index=0;index<valid_ranges.size();++index) { if(index) out<<','; const auto& range=valid_ranges[index]; out<<"{\"id\":"<<range.begin.line_id<<",\"version\":"<<range.begin.version<<",\"begin\":"<<range.begin.column<<",\"end\":"<<range.end.column<<'}'; }
            out<<"],\"visible_lines\":[";
            if(std::string_view(phase)=="geometry-failure") for(size_t index=0;index<target->visible_snapshot().lines.size();++index) {
                if(index) out<<','; const auto& row=target->visible_snapshot().lines[index]; out<<"{\"id\":"<<row.id<<",\"version\":"<<row.version<<",\"columns\":"<<row.size()<<",\"ending\":"<<static_cast<int>(row.ending)<<",\"next_id\":"<<row.next_id<<",\"cropped\":"<<(row.cropped?"true":"false")<<",\"text\":"; termforge::Screen::quoted(out,line(row)); out<<'}';
            }
            out<<"],\"document_lines\":[";
            if(std::string_view(phase)=="context-find" || std::string_view(phase).rfind("geometry-",0)==0) {
                const auto document=target->observed_document_lines(); require(document.size()<=300,"actions document observation bound exceeded");
                for(size_t index=0;index<document.size();++index) {
                    if(index) out<<','; const auto& row=document[index]; out<<"{\"id\":"<<row.projection.id<<",\"version\":"<<row.projection.version<<",\"columns\":"<<row.columns
                        <<",\"ending\":"<<static_cast<int>(row.ending)<<",\"joins_next\":"<<(row.joins_next?"true":"false")<<",\"text\":"; termforge::Screen::quoted(out,row.projection.text); out<<'}';
                }
            }
            out<<']';
            out<<",\"display_dpi\":"<<target->display_dpi()<<",\"zoom_percent\":"<<target->zoom_percent()<<",\"grid\":["<<columns<<','<<rows<<"],\"cell_pixels\":["<<cell_width<<','<<cell_height<<"],\"overlay_cells\":"<<overlay<<",\"find_geometry\":";
            if(target->find_handle()) {
                RECT area{},edit{}; require(GetWindowRect(target->find_handle(),&area) && GetWindowRect(target->find_edit_handle(),&edit),"actions Find geometry query failed");
                LOGFONTW font{}; const auto handle=reinterpret_cast<HFONT>(SendMessageW(target->find_edit_handle(),WM_GETFONT,0,0));
                require(handle && GetObjectW(handle,sizeof(font),&font)==sizeof(font),"actions Find font query failed");
                out<<"{\"window\":["<<area.left<<','<<area.top<<','<<area.right<<','<<area.bottom<<"],\"edit\":["<<edit.left<<','<<edit.top<<','<<edit.right<<','<<edit.bottom<<"],\"font_height\":"<<font.lfHeight<<",\"os_dpi\":"<<GetDpiForWindow(target->find_handle())<<'}';
            } else out<<"null";
            out<<",\"top\":"<<state.top<<",\"maximum\":"<<state.maximum_top<<",\"ticks\":"<<target->observed_tick_count()
                <<",\"status\":"; termforge::Screen::quoted(out,utf8(target->status_text().c_str()));
            out<<",\"frame_matches_full\":"<<(target->frame_matches_full()?"true":"false")<<'}';
        }
        out<<"]}"; geometry_samples_.push_back(out.str());
        std::ofstream journal(directory/"actions-progress.jsonl",std::ios::binary|std::ios::app); journal<<out.str()<<'\n'; journal.close(); require(journal.good(),"actions progress write failed");
        if(std::string_view(phase).rfind("geometry-",0)==0) {
            Bitmap frame(parent.right,parent.bottom); paint(frame.dc);
            for(const auto group:{Group::bottom,Group::main}) if(auto* target=selected(group)) { const auto rect=physical(group==Group::bottom?terminal_:main_terminal_); target->render_to(frame.dc,rect.left,rect.top); }
            frame.save((directory/(std::string(phase)+".bmp")).string());
        }
    };
    if(stage_==0 && bottom && bottom->ready()) {
        gesture.reset(); last_popup.clear(); geometry_samples_.clear(); before_id=before_version=after_id=after_version=0;
        refocus_before={}; refocus_after={}; refocus_query.clear(); refocus_selected.clear(); refocus_focused=false;
        if(system) tf_shell_test::require_hosted_clipboard(); transition(96); add(Group::main); stage_=1;
    } else if(stage_==1 && main && main->ready() && bottom->settled() && main->settled()) {
        bottom->start_text_receiver(directory/"actions-bottom.bin"); main->start_text_receiver(directory/"actions-main.bin"); stage_=2;
    } else if(stage_==2 && bottom->live_contains_line("TF_FIND_READY 080") && main->live_contains_line("TF_FIND_READY 080")) {
        select(0,Group::bottom); record("ready"); popup(bottom,0); stage_=3;
    } else if(stage_==3 && !gesture) {
        record("terminal-empty","terminal-context-inspect",true); key(bottom->handle(),'C',true,true); stage_=60;
    } else if(stage_==60 && bottom->status_text()==L"No selected text") {
        record("empty-copy-rejected","queued-Ctrl-Shift-C-empty"); popup(bottom,3); stage_=4;
    } else if(stage_==4 && !gesture && bottom->has_selection()) {
        record("context-all","terminal-context-all",true); popup(bottom,4); stage_=5;
    } else if(stage_==5 && !gesture && bottom->find_edit_handle()) {
        SetWindowTextW(bottom->find_edit_handle(),L"TF_FIND_077"); stage_=6;
    } else if(stage_==6 && bottom->search_status()==termforge::SearchStatus::complete && bottom->current_find_range()) {
        record("context-find","terminal-context-find",true); bottom->begin_output_observation(); main->begin_output_observation(); transition(144); stage_=80;
    } else if(stage_>=80 && stage_<=84 && bottom->settled() && main->settled() && bottom->search_status()==termforge::SearchStatus::complete) {
        require(GetFocus()==bottom->find_edit_handle(),"actions geometry lost native Find EDIT focus");
        if(!bottom->current_find_range() || !bottom->has_selection()) {
            record("geometry-failure","controlled-DPI-selection-or-match-loss");
            require(bottom->current_find_range().has_value(),"actions geometry lost current Find match"); require(false,"actions geometry lost terminal selection");
        }
        if(stage_==80) { record("geometry-144","controlled-DPI-with-Find-selection"); transition(192); stage_=81; }
        else if(stage_==81) { record("geometry-192","controlled-DPI-with-Find-selection"); transition(96); stage_=82; }
        else if(stage_==82) { record("geometry-96","controlled-DPI-with-Find-selection"); bottom->change_zoom(-25); stage_=83; }
        else if(stage_==83) { record("geometry-zoom75","session-zoom-with-Find-selection"); bottom->reset_zoom(); stage_=84; }
        else { record("geometry-restored","session-zoom-with-Find-selection"); key(bottom->find_edit_handle(),'A',true); stage_=7; }
    } else if(stage_==7) {
        require(PostMessageW(bottom->find_edit_handle(),WM_CHAR,'z',1)!=FALSE,"actions native Find EDIT input queue failed"); stage_=8;
    } else if(stage_==8) {
        wchar_t query[4097]{}; GetWindowTextW(bottom->find_edit_handle(),query,4097); refocus_query=query;
        require(refocus_query==L"z","actions native Find Ctrl+A replacement differs");
        SendMessageW(bottom->find_edit_handle(),EM_SETSEL,1,1);
        SendMessageW(bottom->find_edit_handle(),EM_GETSEL,reinterpret_cast<WPARAM>(&refocus_before[0]),reinterpret_cast<LPARAM>(&refocus_before[1]));
        key(bottom->find_edit_handle(),'F',true,true); stage_=85;
    } else if(stage_==85) {
        SendMessageW(bottom->find_edit_handle(),EM_GETSEL,reinterpret_cast<WPARAM>(&refocus_after[0]),reinterpret_cast<LPARAM>(&refocus_after[1]));
        wchar_t query[4097]{}; GetWindowTextW(bottom->find_edit_handle(),query,4097); const std::wstring actual=query;
        require(refocus_after[0]<=refocus_after[1] && refocus_after[1]<=actual.size(),"actions refocus selected range invalid");
        refocus_selected=actual.substr(refocus_after[0],refocus_after[1]-refocus_after[0]); refocus_focused=GetFocus()==bottom->find_edit_handle();
        if(refocus_before!=std::array<DWORD,2>{1,1} || refocus_after!=std::array<DWORD,2>{0,1} || !refocus_focused || actual!=L"z") {
            record("find-refocus-failure","queued-Ctrl-Shift-F-in-Find-EDIT"); require(false,"actions Find refocus did not reselect its existing query");
        }
        require(PostMessageW(bottom->find_edit_handle(),WM_CHAR,'q',1)!=FALSE,"actions refocus replacement character queue failed"); stage_=86;
    } else if(stage_==86) {
        record("find-editor","native-edit-Ctrl-A-char-and-Ctrl-Shift-F-refocus"); key(bottom->find_edit_handle(),VK_ESCAPE); stage_=9;
    } else if(stage_==9 && !bottom->find_handle()) {
        select(0,Group::main); key(main->handle(),'F',true,true); stage_=10;
    } else if(stage_==10 && main->find_edit_handle()) {
        SetWindowTextW(main->find_edit_handle(),L"TF_FIND_075"); stage_=11;
    } else if(stage_==11 && main->search_status()==termforge::SearchStatus::complete && main->current_find_range()) {
        record("shortcut-find","queued-Ctrl-Shift-F"); key(main->find_edit_handle(),VK_ESCAPE); stage_=12;
    } else if(stage_==12 && !main->find_handle()) {
        key(main->handle(),VK_HOME,true,true); stage_=13;
    } else if(stage_==13 && main->history_state().top==0) {
        record("history-top","queued-Ctrl-Shift-Home"); key(main->handle(),VK_END,true,true); stage_=14;
    } else if(stage_==14 && main->history_state().top==main->history_state().maximum_top) {
        record("history-bottom","queued-Ctrl-Shift-End"); key(main->handle(),VK_F2); stage_=15;
    } else if(stage_==15 && rename_) {
        SendMessageW(rename_,EM_SETSEL,0,0); // Do not borrow begin_rename's initial select-all as evidence.
        key(rename_,'A',true); stage_=16;
    } else if(stage_==16) {
        require(PostMessageW(rename_,WM_CHAR,'R',1)!=FALSE,"actions native rename input queue failed"); stage_=17;
    } else if(stage_==17) {
        record("rename-editor","native-rename-Ctrl-A-and-char"); key(rename_,VK_ESCAPE); stage_=18;
    } else if(stage_==18 && !rename_) {
        main->synthetic_paste(u"HOLD\r\n"); stage_=19;
    } else if(stage_==19 && main->paste_prompt_handle()) {
        record("busy-prompt","synthetic-prompt-state-fixture"); SetFocus(main->handle()); key(main->handle(),'V',true,true); stage_=61;
    } else if(stage_==61 && main->paste_busy() && main->status_text()==L"Paste already in progress") {
        record("busy-shortcut-rejected","queued-Ctrl-Shift-V-busy"); popup(main,0); stage_=20;
    } else if(stage_==20 && !gesture) {
        record("busy-terminal","terminal-context-inspect",true); popup(main,0,true); stage_=21;
    } else if(stage_==21 && !gesture) {
        record("busy-session","session-menu-inspect",true); popup(main,13,true); stage_=22;
    } else if(stage_==22 && !gesture && !main->paste_busy()) {
        record("busy-cancelled","session-menu-cancel",true);
        if(system) { select(0,Group::bottom); popup(bottom,1); stage_=30; } else stage_=40;
    } else if(stage_==30 && !gesture) {
        record("system-copy","terminal-context-copy",true,true); select(0,Group::main);
        write_cf(u"SHIFT 中文 e\u0301"); key(main->handle(),VK_INSERT,false,true); stage_=31;
    } else if(stage_==31 && !main->paste_busy() && capture("main")=="SHIFT 中文 e\xcc\x81") {
        record("system-shift-insert","queued-Shift-Insert",false,true); write_cf(u"DEFAULT_ENTER 中文\r\nNEXT"); popup(main,2); stage_=35;
    } else if(stage_==35 && !gesture && main->paste_prompt_handle()) {
        require(GetFocus()==GetDlgItem(main->paste_prompt_handle(),IDOK),"actions system Paste default focus is not Paste");
        record("system-default-enter-prompt","terminal-context-default-paste",true,true); key(GetFocus(),VK_RETURN); stage_=36;
    } else if(stage_==36 && !main->paste_busy() && capture("main")=="SHIFT 中文 e\xcc\x81" "DEFAULT_ENTER 中文\rNEXT") {
        record("system-default-enter-accepted","queued-default-Enter",false,true); write_cf(u"CTX 中文 e\u0301\r\nNEXT"); popup(main,2); stage_=32;
    } else if(stage_==32 && !gesture && main->paste_prompt_handle()) {
        record("system-paste-prompt","terminal-context-paste",true,true); select(0,Group::bottom); record("system-target-switched","tab-switch-during-prompt");
        SetFocus(GetDlgItem(main->paste_prompt_handle(),IDCANCEL)); key(GetFocus(),VK_TAB); stage_=33;
    } else if(stage_==33 && GetFocus()==GetDlgItem(main->paste_prompt_handle(),IDOK)) {
        key(GetFocus(),VK_RETURN); stage_=34;
    } else if(stage_==34 && !main->paste_busy() && capture("main")=="SHIFT 中文 e\xcc\x81" "DEFAULT_ENTER 中文\rNEXT" "CTX 中文 e\xcc\x81\rNEXT") {
        record("system-confirmed","queued-prompt-Tab-Enter"); stage_=40;
    } else if(stage_==40) {
        select(0,Group::main); main->begin_output_observation(); main->fixture_command(L"~"); stage_=70;
    } else if(stage_==70 && main->observed_output().find("TF_STREAM_TICK_")!=std::string::npos) {
        const auto view=main->visible_snapshot(); int row=-1;
        for(size_t index=0;index<view.lines.size();++index) if(line(view.lines[index]).rfind("TF_STREAM_TICK_",0)==0) { row=static_cast<int>(index); break; }
        require(row>=0,"actions stream row is not visible"); const auto [width,height]=main->pixel_cell_size();
        const auto begin=MAKELPARAM(width/4,row*height+height/2),end=MAKELPARAM(15*width+width/4,row*height+height/2);
        SendMessageW(main->handle(),WM_LBUTTONDOWN,0,begin); require(GetCapture()==main->handle(),"actions stream drag did not capture");
        SendMessageW(main->handle(),WM_MOUSEMOVE,MK_LBUTTON,end); SendMessageW(main->handle(),WM_LBUTTONUP,0,end);
        require(GetCapture()!=main->handle(),"actions stream drag did not release capture");
        const auto actual=main->visible_snapshot().lines[static_cast<size_t>(row)]; before_id=actual.id; before_version=actual.version;
        require(main->selected_text().text=="TF_STREAM_TICK_","actions stream selection literal differs");
        record("live-selected","mouse-drag-live-prefix"); main->fixture_command(L"-"); stage_=71;
    } else if(stage_==71 && main->live_contains_line("TF_STREAM_STOPPED") && capture("main")==std::string(system?"SHIFT 中文 e\xcc\x81" "DEFAULT_ENTER 中文\rNEXT" "CTX 中文 e\xcc\x81\rNEXT":"")+"~-") {
        for(const auto& row:main->visible_snapshot().lines) if(line(row)=="TF_STREAM_STOPPED") { after_id=row.id; after_version=row.version; break; }
        require(before_id && before_id==after_id && after_version>before_version,"actions overwrite did not change the selected line version");
        require(main->selected_text().status==termforge::SelectionCopyStatus::invalidated && !main->has_selection(),"actions overwrite did not invalidate selection");
        main->perform_text_action(TextAction::copy);
        require(main->status_text()==L"Selected text changed; select it again","actions invalidated Copy lost changed-text feedback");
        record("invalidated-copy-rejected","public-copy-invalidated");
        require(PostMessageW(main->handle(),WM_CHAR,'x',1)!=FALSE,"actions ordinary input queue failed"); stage_=41;
    } else if(stage_==41 && !capture("main").empty() && capture("main").back()=='x') {
        record("ordinary-input","queued-plain-character"); action(2);
        require(!main->perform_text_action(TextAction::paste),"actions closing target admitted Paste");
        record("closing","close-focused-session"); stage_=42;
    } else if(stage_==42 && collected_==1 && main_tabs_.empty()) {
        select(0,Group::bottom); action(2); stage_=43;
    } else if(stage_==43 && collected_==2 && session_count()==0) {
        require(!perform_text_action(TextAction::select_all),"actions empty workspace admitted Select All");
        record("no-session"); popup(nullptr,0,true); stage_=44;
    } else if(stage_==44 && !gesture) {
        record("empty-session-menu","session-menu-inspect",true);
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc); frame.save(image_);
        std::ofstream out(directory/"actions.json",std::ios::binary);
        out<<"{\"schema\":\"termforge-window-actions-v1\",\"system_clipboard\":"<<(system?"true":"false")<<",\"fixture\":\"shell_text_client-80\",\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; } out<<"]}\n"; out.close(); require(out.good(),"actions final manifest write failed");
        stage_=100; close_all();
    }
}
