// Hosted-only CF_UNICODETEXT and actual Microsoft Pinyin within the eight-session run.
#pragma once
inline void Workspace::workflow_system_initialize() {
    if(!workflow_system_) return;
    tf_shell_test::require_hosted_ime_input();auto& system=*workflow_system_;
    system.journal=std::filesystem::path(image_).parent_path()/"workflow-system-progress.jsonl";
    require(!std::filesystem::exists(system.journal),"workflow system journal must be fresh");
    system.provider=std::make_unique<tf_shell_test::HostedPinyin>();
    workflow_shell("A")->enable_operation_observation();
}
inline std::string Workspace::workflow_system_identity(const std::string& name) {
    for(auto slot=workflow_->slots.rbegin();slot!=workflow_->slots.rend();++slot) if(slot->name==name && !slot->retired)
        return ",\"role\":"+tf_shell_test::ime_quote(name)+",\"session_id\":"+std::to_string(slot->session_id)+
            ",\"generation\":"+std::to_string(slot->generation)+",\"window_token\":"+tf_shell_test::WorkflowSystemState::window_token(slot->window);
    throw std::runtime_error("workflow system identity is missing");
}
inline void Workspace::workflow_system_event(const std::string& kind,const std::string& fields) {
    if(kind!="checkpoint") workflow_->reserve_observations(1);
    workflow_system_->event(kind,fields);
}
inline std::string Workspace::workflow_operation_top() {
    auto* target=workflow_shell("A");const auto& view=target->visible_snapshot();
    require(!view.lines.empty(),"workflow operation has no actual viewport");
    const auto& line=view.lines.front();std::string text;for(const auto& cell:line) text+=cell.text;
    while(!text.empty() && text.back()==' ') text.pop_back();
    std::ostringstream out;out<<"{\"published_revision\":"<<target->runtime_snapshot().published_revision
        <<",\"top_line\":{\"id\":"<<line.id<<",\"version\":"<<line.version<<",\"text\":"<<tf_shell_test::ime_quote(text)<<"}}";return out.str();
}
inline void Workspace::workflow_operation_arm(uint64_t id,UINT command) {
    auto* target=workflow_shell("A");workflow_system_->operation_before=workflow_operation_top();
    target->arm_scroll_observation(id,2,command);
    require(PostMessageW(target->handle(),WM_VSCROLL,command,0)!=FALSE,"workflow actual scroll message failed");
}
inline bool Workspace::workflow_operation_complete(uint64_t id) {
    auto* target=workflow_shell("A");const auto values=target->operation_observations();
    require(!values.empty() && values.back().operation_id==id,"workflow scroll observation missing");const auto& row=values.back();
    if(row.reason==tf_shell::OperationReason::armed || row.reason==tf_shell::OperationReason::awaiting_state || row.reason==tf_shell::OperationReason::awaiting_paint) return false;
    require(row.reason==tf_shell::OperationReason::submitted,"workflow scroll did not reach its own actual WM_PAINT submission");
    workflow_->reserve_observations(1);workflow_system_->operation_records.push_back(tf_shell::operation_observation_json(row));
    std::ostringstream out;out<<"{\"operation_id\":"<<id<<",\"session_id\":"<<row.session_id<<",\"generation\":"<<row.generation
        <<",\"window_creation_serial\":"<<row.window_creation_serial<<",\"window_token\":"<<tf_shell_test::WorkflowSystemState::window_token(target->handle())
        <<",\"before\":"<<workflow_system_->operation_before<<",\"after\":"<<workflow_operation_top()<<'}';
    workflow_system_->operation_context.push_back(out.str());return true;
}
inline void Workspace::workflow_system_shortcut(const std::string& name,bool copy) {
    tf_shell_test::require_hosted_ime_input();auto* target=workflow_shell(name);
    require(target && !target->closing() && IsWindowVisible(target->handle()),"workflow shortcut target is not own live visible shell");
    BYTE saved[256]{},keys[256]{};require(GetKeyboardState(saved)!=FALSE,"workflow keyboard state read failed");
    std::copy(std::begin(saved),std::end(saved),std::begin(keys));
    for(int key:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU}) keys[key]=0;
    keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;
    require(SetKeyboardState(keys)!=FALSE,"workflow shortcut keyboard state failed");
    // Synchronous delivery to our own HWND; no WM_CHAR translation is generated.
    SendMessageW(target->handle(),WM_KEYDOWN,copy?'C':'V',1);
    SendMessageW(target->handle(),WM_KEYUP,copy?'C':'V',1);
    require(SetKeyboardState(saved)!=FALSE,"workflow keyboard state restore failed");
    ++workflow_system_->shortcuts;
    workflow_system_event("shortcut",",\"code\":"+tf_shell_test::ime_quote(copy?"CtrlShiftC":"CtrlShiftV")+
        ",\"source\":\"native-WM_KEYDOWN\""+workflow_system_identity(name));
}
inline HWND Workspace::workflow_system_prompt() {
    struct Query {HWND owner,found=nullptr;bool duplicate=false;};Query query{window_};
    require(EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
        auto& q=*reinterpret_cast<Query*>(data);wchar_t cls[64]{};GetClassNameW(window,cls,64);
        if(std::wstring_view(cls)==L"TermForgePastePrompt" && GetWindow(window,GW_OWNER)==q.owner) {
            if(q.found) q.duplicate=true;else q.found=window;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&query))!=FALSE && !query.duplicate,"workflow owned paste prompt enumeration failed");
    return query.found;
}
inline void Workspace::workflow_system_point(const char* phase,bool clipboard,bool prompt,bool ime) {
    using namespace tf_shell_test;require_hosted_ime_input();auto& system=*workflow_system_;
    require(system.points.size()<16 && session_count()==8,"workflow system point count/live session bound");
    const auto directory=std::filesystem::path(image_).parent_path();
    const auto received=[&](const std::string& name) {for(auto slot=workflow_->slots.rbegin();slot!=workflow_->slots.rend();++slot)
        if(slot->name==name && !slot->retired) return slot->controller->bytes();throw std::runtime_error("workflow raw receiver missing");};
    const bool large=std::string_view(phase).rfind("large-",0)==0;
    size_t units=1;std::ostringstream out;
    out<<"{\"seq\":"<<system.points.size()+1<<",\"phase\":"<<ime_quote(phase)<<",\"elapsed_ms\":"<<GetTickCount64()-system.started
       <<",\"active_sessions\":"<<session_count()<<",\"key_batches\":"<<system.key_batches<<",\"targets\":[";
    bool first=true;for(const auto& slot:workflow_->slots) if(!slot.retired) {
        if(!first) out<<',';first=false;++units;
        out<<"{\"name\":"<<ime_quote(slot.name)<<",\"session_id\":"<<slot.session_id<<",\"generation\":"<<slot.generation
           <<",\"window_creation_serial\":"<<workflow_shell(slot.name)->runtime_snapshot().window_creation_serial
           <<",\"window_token\":"<<WorkflowSystemState::window_token(slot.window)<<",\"received_hex\":"<<ime_quote(ime_hex(slot.controller->bytes()))<<'}';
    }
    out<<']';for(const std::string name:large?std::vector<std::string>{"A","B","C"}:std::vector<std::string>{"A","B"}) {
        auto* target=workflow_shell(name);const auto runtime=target->runtime_snapshot();++units;
        out<<','<<ime_quote(name)<<":{\"runtime\":"<<tf_shell::runtime_snapshot_json(runtime);
        if(name=="A") {
            const auto selected=target->selected_text();const auto current=target->current_find_range();
            const char* status=selected.status==termforge::SelectionCopyStatus::ok?"ok":selected.status==termforge::SelectionCopyStatus::empty?"empty":selected.status==termforge::SelectionCopyStatus::invalidated?"invalidated":"limit-exceeded";
            out<<",\"selected_text_hex\":"<<ime_quote(ime_hex(selected.text))<<",\"selection_status\":"<<ime_quote(status)<<",\"current_find\":";
            if(current) out<<"{\"line_id\":"<<current->begin.line_id<<",\"version\":"<<current->begin.version<<",\"column\":"<<current->begin.column<<",\"end_column\":"<<current->end.column<<'}';else out<<"null";
        }
        out<<'}';
    }
    out<<",\"clipboard\":";
    if(clipboard) {
        ++units;const auto text=tf_shell::read_unicode_clipboard(window_,1024);
        const std::wstring wide(reinterpret_cast<const wchar_t*>(text.data()),text.size());
        out<<"{\"sequence\":"<<GetClipboardSequenceNumber()<<",\"owner_window_token\":"<<WorkflowSystemState::window_token(GetClipboardOwner())
           <<",\"utf16_hex\":"<<ime_quote(WorkflowSystemState::utf16_hex(text))<<",\"text_utf8_hex\":"<<ime_quote(ime_hex(ime_utf8(wide)))<<'}';
    } else out<<"null";
    out<<",\"prompt\":";
    const HWND dialog=prompt?workflow_system_prompt():nullptr;
    if(dialog) {
        ++units;wchar_t title[128]{};require(GetWindowTextW(dialog,title,128)>0,"workflow prompt title missing");
        const auto runtime=workflow_shell("B")->runtime_snapshot();
        out<<"{\"present\":true,\"window_token\":"<<WorkflowSystemState::window_token(dialog)<<",\"owner_window_token\":"<<WorkflowSystemState::window_token(GetWindow(dialog,GW_OWNER))
           <<",\"target_window_token\":"<<WorkflowSystemState::window_token(workflow_shell("B")->handle())<<",\"target_session\":"<<runtime.session_id
           <<",\"title\":"<<ime_quote(ime_utf8(title))<<",\"visible\":"<<(IsWindowVisible(dialog)?"true":"false")<<",\"confirm_button\":"<<(GetDlgItem(dialog,IDOK)?"true":"false")<<'}';
    } else out<<"null";
    out<<",\"ime\":";
    if(ime) {
        ++units;auto* target=workflow_shell("B");HostedImeCheckpoint row{phase,"B",received("B"),target->ime_observation(),system.candidates.sample(),target->display_dpi(),target->zoom_percent(),target->pixel_cell_size()};
        MONITORINFO monitor{sizeof(monitor)};require(GetMonitorInfoW(MonitorFromRect(&row.observation.caret_screen,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,"workflow IME monitor query failed");
        row.monitor_work=monitor.rcWork;row.os_dpi=GetDpiForWindow(target->handle());out<<WorkflowSystemState::ime_json(row);
    } else out<<"null";
    out<<'}';const auto point=out.str();require(point.size()<=32768,"workflow system point byte bound");
    system.observations+=units;workflow_->reserve_observations(units);system.points.push_back(point);
    workflow_system_event("checkpoint",",\"snapshot\":"+point);workflow_write();
}
inline bool Workspace::workflow_system_tick() {
    using namespace tf_shell_test;auto& system=*workflow_system_;auto* a=workflow_shell("A");auto* b=workflow_shell("B");
    const auto received=[&] {for(auto& slot:workflow_->slots) if(slot.name=="B" && !slot.retired) return slot.controller->bytes();return std::string{};};
    const auto key=[&](WORD code,const char* name) {
        const auto generation=b->ime_observation().generation;HostedPinyin::key(b->handle(),3,generation,code);++system.key_batches;system.last_key=GetTickCount64();
        workflow_system_event("ime-key",",\"code\":"+ime_quote(name)+",\"guarded\":true,\"actual_generation\":"+std::to_string(generation)+workflow_system_identity("B"));
    };
    const auto elapsed=[&](ULONGLONG delay) {return GetTickCount64()-system.last_key>=delay;};
    if(system.step==0) {
        workflow_select("A");const auto [cw,ch]=a->pixel_cell_size();
        SendMessageW(a->handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(1,ch/2));
        SendMessageW(a->handle(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(21*cw,ch+ch/2));
        SendMessageW(a->handle(),WM_LBUTTONUP,0,MAKELPARAM(21*cw,ch+ch/2));
        require(a->selected_text().text==system.copy,"workflow selected text differs from independent literal");
        tf_shell::write_unicode_clipboard(window_,u"TF_R109_COPY_SENTINEL");system.sentinel_sequence=GetClipboardSequenceNumber();
        workflow_system_point("selected",true,false,false);workflow_system_shortcut("A",true);
        require(GetClipboardSequenceNumber()!=system.sentinel_sequence,"workflow Copy did not change clipboard sequence");
        workflow_system_point("copied",true,false,false);workflow_select("B");workflow_system_shortcut("B",false);
        workflow_system_point("paste-requested",true,false,false);system.step=1;
    } else if(system.step==1) {
        if(!b->runtime_snapshot().paste_prompt_present || !workflow_system_prompt()) return false;
        require(received().empty(),"workflow sent multiline paste before confirmation");
        system.prompt=workflow_system_prompt();workflow_system_point("paste-confirmation",true,true,false);
        const HWND button=GetDlgItem(system.prompt,IDOK);require(button!=nullptr,"workflow confirmation button missing");
        SendMessageW(button,BM_CLICK,0,0);workflow_system_event("confirm",",\"source\":\"native-BM_CLICK\",\"button\":1,\"dialog_window_token\":"+WorkflowSystemState::window_token(system.prompt)+workflow_system_identity("B"));
        workflow_select("A");system.step=2;
    } else if(system.step==2) {
        if(!system.input.before_commit(received()) || b->paste_busy()) return false;
        require(!IsWindow(system.prompt) && !workflow_system_prompt(),"workflow confirmation did not close");
        workflow_system_point("paste-complete",true,true,false);workflow_select("B");b->begin_ime_observation(3);HostedPinyin::focus(b->handle());system.last_key=GetTickCount64();system.step=3;
    } else if(system.step==3) {
        if(system.key_batches<5) {if(elapsed(120)) {const char code=std::string("NIHAO")[system.key_batches];const char name[]{code,0};key(code,name);}return false;}
        if(!elapsed(350) || !b->composing() || b->ime_observation().preedit.empty()) return false;
        require(system.input.before_commit(received()),"workflow preedit altered accepted prefix");workflow_system_point("commit-preedit",false,false,true);key(VK_SPACE,"SPACE");system.step=4;
    } else if(system.step==4) {
        if(!elapsed(300) || b->composing() || !system.input.after_commit(received())) return false;
        workflow_system_point("committed",false,false,true);system.step=5;
    } else if(system.step==5) {
        if(system.key_batches<8) {if(elapsed(120)) {const char code=system.key_batches==6?'N':'I';const char name[]{code,0};key(code,name);}return false;}
        if(!elapsed(350) || !b->composing() || b->ime_observation().preedit.empty()) return false;
        require(system.input.after_commit(received()),"workflow cancel preedit altered accepted input");workflow_system_point("cancel-preedit",false,false,true);key(VK_ESCAPE,"ESC");system.step=6;
    } else if(system.step==6) {
        if(!elapsed(350) || b->composing() || !b->ime_observation().preedit.empty()) return false;
        require(system.input.after_commit(received()),"workflow cancellation emitted extra input");workflow_system_point("cancelled",false,false,true);system.step=7;
    }
    return system.step==7;
}
inline void Workspace::workflow_system_finish() {
    using namespace tf_shell_test;auto& system=*workflow_system_;
    require(stage_==100 && workflow_->closed.size()==18 && system.points.size()==13,"workflow system final state incomplete");
    workflow_system_event("completed",",\"checkpoints\":13,\"key_batches\":"+std::to_string(system.key_batches)+",\"shortcuts\":"+std::to_string(system.shortcuts));workflow_write();
    const auto path=std::filesystem::path(image_).parent_path()/"workflow-system.json";std::ofstream out(path,std::ios::binary);
    const auto desktop=require_hosted_ime_input();
    out<<"{\"schema\":\"termforge-workflow-system-v1\",\"mode\":\"ui-workflow-system\",\"data_only\":false,\"system_clipboard\":true,\"real_ime\":true"
       <<",\"workspace_window_token\":"<<WorkflowSystemState::window_token(window_)<<",\"input_desktop\":{\"station\":\"WinSta0\",\"thread_name\":"<<ime_quote(ime_utf8(desktop))<<",\"input_name\":"<<ime_quote(ime_utf8(desktop))<<",\"query_only\":true}"
       <<",\"provider\":{\"clsid\":\"{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}\",\"profile\":\"{FA550B04-5AD7-411F-A5AC-CA038EC515D7}\",\"langid\":2052,\"language_hresult\":"<<system.provider->language_result<<",\"activation_hresult\":"<<system.provider->activation_result<<'}'
       <<",\"expected_copy_utf8_hex\":"<<ime_quote(ime_hex(system.copy))<<",\"expected_prefix_utf8_hex\":"<<ime_quote(ime_hex(system.input.prefix()))
       <<",\"expected_committed_utf8_hex\":"<<ime_quote(ime_hex(system.input.prefix()+u8"你好"))<<",\"expected_final_utf8_hex\":"<<ime_quote(ime_hex(system.input.prefix()+u8"你好!"))
       <<",\"key_batches\":"<<system.key_batches<<",\"guarded_key_batches\":"<<system.key_batches<<",\"shortcut_batches\":"<<system.shortcuts
       <<",\"observations\":"<<system.observations<<",\"combined_observations\":"<<workflow_->observations<<",\"snapshots\":[";
    for(size_t i=0;i<system.points.size();++i) {if(i) out<<',';out<<system.points[i];}
    out<<"]}\n";out.close();require(out.good(),"workflow system final evidence write failed");
}
