// Real eight-session workspace on the existing owned non-input desktop runner.
#pragma once
inline ShellWindow* Workspace::workflow_shell(const std::string& name) {
    require(workflow_!=nullptr,"workflow scenario is disabled");
    for(auto slot=workflow_->slots.rbegin();slot!=workflow_->slots.rend();++slot) if(slot->name==name && !slot->retired) {
        for(const auto group:{Group::main,Group::bottom}) for(auto& tab:group_tabs(group)) if(tab.id==slot->tab_id) return tab.shell.get();
    }
    return nullptr;
}
inline void Workspace::workflow_select(const std::string& name) {
    auto* shell=workflow_shell(name);require(shell!=nullptr,"workflow selection target is missing");
    for(const auto group:{Group::main,Group::bottom}) {
        auto& rows=group_tabs(group);
        for(size_t i=0;i<rows.size();++i) if(rows[i].shell.get()==shell) {select(static_cast<int>(i),group);return;}
    }
    throw std::runtime_error("workflow selection target is gone");
}
inline void Workspace::workflow_attach(const std::string& name,const std::string& role,Group group,bool create) {
    auto& evidence=*workflow_;if(create) add(group);
    auto& tab=group_tabs(group).back();auto* shell=tab.shell.get();shell->enable_runtime_observation();
    if(role.size()==2 && role[0]=='F') shell->begin_output_observation();
    const auto generation=evidence.next_generation++;
    const auto directory=std::filesystem::path(image_).parent_path()/"fixtures"/(name+"-"+std::to_string(generation));
    require(std::filesystem::create_directories(directory),"workflow fixture directory must be fresh");
    tf_shell_test::WorkflowSlot slot;slot.name=name;slot.fixture_role=role;slot.tab_id=tab.id;slot.generation=generation;
    slot.window=shell->handle();slot.session_id=shell->runtime_snapshot().session_id;
    slot.controller=std::make_unique<tf_shell_test::WorkflowController>(directory/"capture.bin",role,generation);
    evidence.slots.push_back(std::move(slot));
}
inline void Workspace::workflow_resource_mark(const char* phase,unsigned cycle) {
    auto& evidence=*workflow_;
    const std::string resource_phase=phase;
    if(resource_phase=="eight-ready" || resource_phase=="load-started" || resource_phase=="background-complete" ||
       resource_phase=="cycle-six" || resource_phase=="cycle-six-end" || resource_phase=="cycle-eight" || resource_phase=="after-paste-close") {
        require(evidence.resource_markers<32,"workflow resource marker budget exceeded");evidence.reserve_observations(1);
        LARGE_INTEGER counter{},frequency{};
        require(QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0,"workflow resource clock unavailable");
        if(!evidence.resource_phase_file.get()) evidence.resource_phase_file.reset(tf_shell_test::workflow_new_file(std::filesystem::path(image_).parent_path()/"resource-phases.jsonl"));
        std::ostringstream mark;mark<<"{\"schema\":\"termforge-workflow-resource-phase-v1\",\"seq\":"<<++evidence.resource_markers
            <<",\"phase\":"<<tf_shell_test::workflow_quote(resource_phase)<<",\"cycle\":"<<cycle
            <<",\"qpc_counter\":"<<counter.QuadPart<<",\"qpc_frequency\":"<<frequency.QuadPart
            <<",\"live_sessions\":"<<session_count()<<",\"window_creations\":"<<ShellWindow::successful_window_creations()<<"}\n";
        tf_shell_test::workflow_save(evidence.resource_phase_file.get(),mark.str());
        require(FlushFileBuffers(evidence.resource_phase_file.get())!=FALSE,"workflow resource phase flush failed");
    }
}
inline void Workspace::workflow_checkpoint(const char* phase,unsigned cycle,const std::vector<std::string>& names) {
    auto& evidence=*workflow_;evidence.reserve_observations(names.empty()?static_cast<size_t>(session_count()):names.size());
    workflow_resource_mark(phase,cycle);
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION job{};
    require(QueryInformationJobObject(nullptr,JobObjectBasicAccountingInformation,&job,sizeof(job),nullptr)!=FALSE,"workflow current Job accounting unavailable");
    std::ostringstream out;
    out<<"{\"seq\":"<<evidence.checkpoints.size()+1<<",\"phase\":"<<tf_shell_test::workflow_quote(phase)
       <<",\"elapsed_ms\":"<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started_).count()<<",\"cycle\":"<<cycle<<",\"attempted_creations\":"<<evidence.attempted_creations
       <<",\"window_creations\":"<<ShellWindow::successful_window_creations()<<",\"job\":{\"active\":"<<job.ActiveProcesses
       <<",\"total\":"<<job.TotalProcesses<<",\"terminated\":"<<job.TotalTerminatedProcesses<<"},\"background_load\":[";
    bool first=true;
    const std::string point_name=phase;
    if(point_name=="paste-cancel-pending" || point_name=="paste-close-pending") {
        for(const auto& slot:evidence.slots) if(!slot.retired && slot.name.size()==2 && slot.name[0]=='F' &&
           (point_name=="paste-cancel-pending" || slot.name=="F4" || slot.name=="F5")) {
            evidence.reserve_observations(1);if(!first) out<<',';first=false;
            out<<"{\"name\":"<<tf_shell_test::workflow_quote(slot.name)<<",\"generation\":"<<slot.generation
               <<",\"source_bytes\":"<<std::filesystem::file_size(slot.controller->path.wstring()+L".source.bin")
               <<",\"output_done\":"<<(slot.controller->done(tf_shell_test::WorkflowPhase::output)?"true":"false")
               <<",\"fixture_running\":"<<(!slot.controller->process_signaled()?"true":"false")<<'}';
        }
    }
    out<<"],\"slots\":[";
    first=true;
    for(auto& slot:evidence.slots) {
        if(slot.retired) continue;
        if(!names.empty() && std::find(names.begin(),names.end(),slot.name)==names.end()) continue;
        auto* shell=workflow_shell(slot.name); require(shell!=nullptr && shell->handle()==slot.window,"workflow slot window changed");
        if(!first) out<<','; first=false;
        const auto runtime=shell->runtime_snapshot();
        const auto raw=slot.controller->bytes();
        const auto document=shell->observed_document_lines();
        if(point_name=="background-complete" && slot.name.size()==2 && slot.name[0]=='F') {
            tf_conpty::Handle transport(tf_shell_test::workflow_new_file(slot.controller->path.wstring()+L".transport.bin"));
            tf_shell_test::workflow_save(transport.get(),shell->observed_output());
            require(FlushFileBuffers(transport.get())!=FALSE,"workflow transport tail flush failed");
        }
        const HWND find_edit=shell->find_edit_handle();std::wstring find_query;
        if(find_edit) {
            const int units=GetWindowTextLengthW(find_edit);require(units>=0 && units<=1024,"workflow Find query observation exceeds bound");
            find_query.resize(static_cast<size_t>(units)+1);
            require(GetWindowTextW(find_edit,find_query.data(),units+1)==units,"workflow Find query observation failed");find_query.resize(static_cast<size_t>(units));
        }
        const auto identity=std::to_string(slot.controller->process_id())+":"+std::to_string(slot.controller->creation_time());
        Group group=Group::main; bool selected=false;
        for(const auto candidate:{Group::main,Group::bottom}) {
            auto& rows=group_tabs(candidate);
            for(size_t i=0;i<rows.size();++i) if(rows[i].id==slot.tab_id) { group=candidate; selected=selection(candidate)==static_cast<int>(i); }
        }
        const auto capture=std::filesystem::relative(slot.controller->path,std::filesystem::path(image_).parent_path()).generic_string();
        out<<"{\"name\":"<<tf_shell_test::workflow_quote(slot.name)<<",\"fixture_role\":"<<tf_shell_test::workflow_quote(slot.fixture_role)
           <<",\"session_id\":"<<slot.session_id<<",\"generation\":"<<slot.generation<<",\"window_token\":\""<<reinterpret_cast<ULONG_PTR>(slot.window)
           <<"\",\"window_creation_serial\":"<<runtime.window_creation_serial<<",\"exists\":"<<(IsWindow(slot.window)?"true":"false")
           <<",\"visible\":"<<(IsWindowVisible(slot.window)?"true":"false")<<",\"selected\":"<<(selected?"true":"false")
           <<",\"group\":\""<<(group==Group::main?"main":"bottom")<<"\",\"capture\":"<<tf_shell_test::workflow_quote(capture)
           <<",\"fixture_ready\":"<<(slot.controller->ready()?"true":"false")<<",\"fixture_bound\":"<<(slot.controller->bound()?"true":"false")
           <<",\"fixture_running\":"<<(!slot.controller->process_signaled()?"true":"false")<<",\"process_identity\":"<<tf_shell_test::workflow_quote(tf_shell_test::workflow_digest(identity))
           <<",\"raw_bytes\":"<<raw.size()<<",\"raw_sha256\":"<<tf_shell_test::workflow_quote(tf_shell_test::workflow_digest(raw))
           <<",\"raw_hex\":\"";
        constexpr char hex[]="0123456789abcdef";for(unsigned char byte:raw) out<<hex[byte>>4]<<hex[byte&15];
        out<<"\",\"source_bytes\":"<<std::filesystem::file_size(slot.controller->path.wstring()+L".source.bin")
           <<",\"output_done\":"<<(slot.controller->done(tf_shell_test::WorkflowPhase::output)?"true":"false")
           <<",\"runtime\":"<<tf_shell::runtime_snapshot_json(runtime)<<",\"find_query\":"<<(find_edit?tf_shell_test::workflow_quote(utf8(find_query.c_str())):"null")
           <<",\"find_edit_token\":"<<(find_edit?tf_shell_test::workflow_quote(std::to_string(reinterpret_cast<ULONG_PTR>(find_edit))):"null")
           <<",\"find_edit_exists\":"<<(find_edit && IsWindow(find_edit)?"true":"false")<<",\"document_first_line_id\":"<<(document.empty()?0:document.front().projection.id)
           <<",\"document_tail\":[";
        const size_t begin=document.size()>10?document.size()-10:0;
        for(size_t i=begin;i<document.size();++i) {if(i>begin) out<<',';termforge::Screen::quoted(out,document[i].projection.text);}
        out<<"],\"visible_lines\":[";
        const auto& view=shell->visible_snapshot();
        for(size_t i=0;i<view.lines.size();++i) {
            if(i) out<<',';std::string text=termforge::project_line(view.lines[i]).text;
            out<<"{\"id\":"<<view.lines[i].id<<",\"version\":"<<view.lines[i].version<<",\"text\":";termforge::Screen::quoted(out,text);out<<'}';
        }
        const auto current=shell->current_find_range();
        out<<"],\"current_find\":";
        if(current) out<<"{\"line_id\":"<<current->begin.line_id<<",\"version\":"<<current->begin.version<<",\"column\":"<<current->begin.column
                       <<",\"end_column\":"<<current->end.column<<'}';else out<<"null";
        out<<'}';
    }
    out<<"]}";evidence.checkpoints.push_back(out.str()); workflow_write();
}
inline void Workspace::workflow_write() {
    const auto path=std::filesystem::path(image_).parent_path()/"sessions.json";
    std::ofstream out(path,std::ios::binary|std::ios::trunc); require(out.good(),"workflow evidence open failed");
    out<<"{\"schema\":\"termforge-workflow-sessions-v1\",\"fixture\":\"r108-console-v1\",\"data_only\":"<<(workflow_system_?"false":"true")
       <<",\"system_clipboard\":"<<(workflow_system_?"true":"false")<<",\"ime\":"<<(workflow_system_?"true":"false")<<",\"observations\":"<<workflow_->observations;
    if(workflow_system_) {
        out<<",\"system_observations\":"<<workflow_system_->observations<<",\"operation_observations\":"<<workflow_system_->operation_records.size()
           <<",\"base_observations\":"<<workflow_->observations-workflow_system_->observations-workflow_system_->operation_records.size()<<",\"observation_limit\":"<<workflow_->observation_limit
           <<",\"operation_records\":[";
        for(size_t i=0;i<workflow_system_->operation_records.size();++i) {if(i) out<<',';out<<workflow_system_->operation_records[i];}
        out<<"],\"operation_context\":[";for(size_t i=0;i<workflow_system_->operation_context.size();++i) {if(i) out<<',';out<<workflow_system_->operation_context[i];}out<<']';
    }
    out<<",\"resource_markers\":"<<workflow_->resource_markers<<",\"checkpoints\":[";
    bool first=true;for(const auto& row:workflow_->checkpoints) {if(!first) out<<',';first=false;out<<row;}
    out<<"],\"closed\":[";first=true;for(const auto& row:workflow_->closed) {if(!first) out<<',';first=false;out<<row;}
    out<<"],\"paste_cases\":[";first=true;
    for(const auto& row:paste_scenario_.results) {
        if(!first) out<<',';first=false;const auto& after=row.after;
        out<<"{\"name\":"<<tf_shell_test::workflow_quote(row.name)<<",\"target\":"<<after.target<<",\"total\":"<<after.total
           <<",\"before_sent\":"<<row.before.sent<<",\"before_accepted\":"<<row.before.accepted<<",\"accepted\":"<<after.accepted
           <<",\"sent\":"<<after.sent<<",\"in_flight\":"<<after.in_flight<<",\"retained_capacity\":"<<after.retained_capacity
           <<",\"state\":\""<<(after.state==tf_shell::PasteState::cancelled?"cancelled":"failed")<<"\",\"checkpoint_bytes\":"<<row.checkpoint_bytes
           <<",\"received\":"<<row.received<<",\"flood_observed\":"<<(row.flood?"true":"false");
        tf_shell_test::PasteScenarioState::write_fence(out,row);out<<'}';
    }
    out<<"]}\n";out.close();require(out.good(),"workflow evidence write failed");
}
inline void Workspace::workflow_collected(ShellWindow* child,int id) {
    for(auto& slot:workflow_->slots) if(slot.tab_id==id && !slot.retired) {
        const auto before=child->runtime_before_close(),final=child->runtime_final();
        workflow_->reserve_observations(2);
        std::ostringstream out;
        out<<"{\"name\":"<<tf_shell_test::workflow_quote(slot.name)<<",\"session_id\":"<<slot.session_id<<",\"generation\":"<<slot.generation
           <<",\"window_token\":\""<<reinterpret_cast<ULONG_PTR>(slot.window)<<"\",\"window_gone\":"<<(!IsWindow(slot.window)?"true":"false")
           <<",\"fixture_signaled\":"<<(slot.controller->process_signaled()?"true":"false")<<",\"fixture_exit_code\":"<<slot.controller->exit_code()
           <<",\"fixture_closed_while_running\":"<<(slot.closed_while_fixture_running?"true":"false")
           <<",\"find_window_gone\":"<<(!slot.closing_find || !IsWindow(slot.closing_find)?"true":"false")
           <<",\"before_close\":"<<(before?tf_shell::runtime_snapshot_json(*before):"null")
           <<",\"final\":"<<(final?tf_shell::runtime_snapshot_json(*final):"null")<<'}';
        slot.retired=true;workflow_->closed.push_back(out.str());workflow_write();
        if(workflow_system_ && stage_==100 && workflow_->closed.size()==18) workflow_system_finish();return;
    }
}
inline void Workspace::scenario_workflow() {
    if(image_.empty() || closing_) return;
    require(workflow_!=nullptr,"workflow scenario state is missing");
    if(ms(started_)>=70000) throw std::runtime_error("workflow sessions scenario timeout at stage "+std::to_string(stage_));
    auto& evidence=*workflow_;
    const auto gate=[&](const std::string& name)->tf_shell_test::WorkflowController& {
        for(auto slot=evidence.slots.rbegin();slot!=evidence.slots.rend();++slot) if(slot->name==name && !slot->retired) return *slot->controller;
        throw std::runtime_error("workflow fixture is missing");
    };
    const auto backgrounds_running=[&] {
        for(const std::string name:{"F1","F2","F3","F4","F5"}) require(!gate(name).done(tf_shell_test::WorkflowPhase::output),"background load completed before foreground interaction");
    };
    if(stage_==0 && active()->ready()) {
        workflow_attach("C","B",Group::bottom,false);
        workflow_attach("A","A",Group::main,true);workflow_attach("B","B",Group::main,true);
        workflow_attach("F1","F1",Group::main,true);workflow_attach("F2","F2",Group::main,true);
        workflow_attach("F3","F3",Group::bottom,true);workflow_attach("F4","F4",Group::bottom,true);workflow_attach("F5","F5",Group::bottom,true);
        stage_=1;
    } else if(stage_==1) {
        for(auto& slot:evidence.slots) if(!workflow_shell(slot.name)->ready()) return;
        for(auto& slot:evidence.slots) workflow_shell(slot.name)->fixture_command(slot.controller->command());
        stage_=2;
    } else if(stage_==2) {
        for(auto& slot:evidence.slots) if(!slot.controller->ready()) return;
        workflow_system_initialize();
        workflow_checkpoint("eight-ready"); add(Group::main);workflow_checkpoint("reject-ninth");
        gate("A").start(tf_shell_test::WorkflowPhase::output);
        for(const std::string name:{"F1","F2","F3","F4","F5"}) gate(name).start(tf_shell_test::WorkflowPhase::output);
        workflow_select("A");
        stage_=3;
    } else if(stage_==3) {
        auto* shell=workflow_shell("A");
        if(!gate("A").done(tf_shell_test::WorkflowPhase::output) || !shell->live_contains_line("TF_R108_A_OUTPUT_DONE 0160")) return;
        if(workflow_system_) {
            const auto document=shell->observed_document_lines();
            if(document.size()<2 || !document.back().projection.text.empty() || document[document.size()-2].projection.text!="TF_R108_A_OUTPUT_DONE 0160") return;
        }
        for(const std::string name:{"F1","F2","F3","F4","F5"}) if(std::filesystem::file_size(gate(name).path.wstring()+L".source.bin")<4096) return;
        backgrounds_running();workflow_checkpoint("load-started");
        SendMessageW(shell->handle(),WM_VSCROLL,SB_TOP,0);
        const auto top_text=[&] {return termforge::project_line(shell->visible_snapshot().lines.front()).text;};
        for(unsigned i=0;i<200 && top_text()!=u8"TF_R108_A_0110 中文 é";++i) SendMessageW(shell->handle(),WM_VSCROLL,SB_LINEDOWN,0);
        require(top_text()==u8"TF_R108_A_0110 中文 é" && !shell->history_state().following,"workflow could not read retained line110");
        if(workflow_system_) {workflow_operation_arm(1,SB_LINEUP);stage_=301;}
        else {
            evidence.reading_anchor=shell->history_state().anchor;workflow_checkpoint("reading-before-append");
            gate("A").start(tf_shell_test::WorkflowPhase::append);stage_=4;
        }
    } else if(stage_==301) {
        if(!workflow_operation_complete(1)) return;workflow_operation_arm(2,SB_LINEDOWN);stage_=302;
    } else if(stage_==302) {
        if(!workflow_operation_complete(2)) return;
        evidence.reading_anchor=workflow_shell("A")->history_state().anchor;workflow_checkpoint("reading-before-append");
        gate("A").start(tf_shell_test::WorkflowPhase::append);stage_=4;
    } else if(stage_==4) {
        auto* shell=workflow_shell("A");
        if(!gate("A").done(tf_shell_test::WorkflowPhase::append) || !shell->live_contains_line("TF_R108_A_APPEND_DONE 0020")) return;
        require(!shell->history_state().following && shell->history_state().anchor==evidence.reading_anchor,"append moved the history reader");
        backgrounds_running();workflow_checkpoint("reading-after-append");
        workflow_select("B");workflow_select("A");
        shell->set_search_work_budget(64);action(16);
        require(shell->find_edit_handle() && SetWindowTextW(shell->find_edit_handle(),L"TF_R108_A_"),"workflow Find EDIT query failed");stage_=5;
    } else if(stage_==5) {
        auto* shell=workflow_shell("A");if(shell->search_status()!=termforge::SearchStatus::running) return;
        backgrounds_running();workflow_checkpoint("search-running");
        require(SetWindowTextW(shell->find_edit_handle(),L"TF_R108_A_0110")!=FALSE,"workflow Find replacement failed");stage_=6;
    } else if(stage_==6) {
        auto* shell=workflow_shell("A");
        if(shell->search_status()!=termforge::SearchStatus::complete || !shell->current_find_range()) return;
        backgrounds_running();workflow_checkpoint("find-complete");
        if(workflow_system_) stage_=60;
        else {
            workflow_select("B");
            for(wchar_t c:std::wstring(L"TF_R108_FOREGROUND 中文\r")) require(PostMessageW(workflow_shell("B")->handle(),WM_CHAR,c,1)!=FALSE,"workflow foreground key failed");
            stage_=7;
        }
    } else if(stage_==60) {
        backgrounds_running();if(workflow_system_tick()) stage_=7;
    } else if(stage_==7) {
        if(workflow_system_?!workflow_system_->input.after_commit(gate("B").bytes()):gate("B").bytes()!=u8"TF_R108_FOREGROUND 中文\r") return;
        backgrounds_running();workflow_checkpoint("foreground-input");action(10);stage_=8;
    } else if(stage_==8) {
        auto* target=workflow_shell("B");if(!target->settled()) return;
        require(target->runtime_snapshot().zoom_percent==75 && workflow_shell("A")->runtime_snapshot().zoom_percent==100,"workflow zoom leaked across sessions");
        backgrounds_running();workflow_checkpoint("zoom-75");action(11);
        workflow_select("F2");workflow_select("A");
        auto* shell=workflow_shell("A");require(shell->current_find_range().has_value(),"switch lost Find range");
        SetFocus(shell->find_edit_handle());require(PostMessageW(shell->find_edit_handle(),WM_KEYDOWN,VK_ESCAPE,1)!=FALSE,"workflow Find cancel key failed");stage_=9;
    } else if(stage_==9) {
        auto* shell=workflow_shell("A");if(shell->find_handle() || !shell->settled() || !workflow_shell("B")->settled()) return;
        workflow_checkpoint("switch-zoom");gate("A").start(tf_shell_test::WorkflowPhase::evict);stage_=10;
    } else if(stage_==10) {
        auto* shell=workflow_shell("A");
        if(!gate("A").done(tf_shell_test::WorkflowPhase::evict) || !shell->live_contains_line("TF_R108_A_EVICT_DONE 0500")) return;
        const auto viewport=shell->history_state();require(!viewport.following && viewport.top==0,"eviction did not clamp the reader to retained history");
        for(const auto& line:shell->observed_document_lines()) require(line.projection.text.find("TF_R108_A_0110 ")==std::string::npos,"evicted line110 remains in the document");
        workflow_checkpoint("history-evicted");gate("C").stop();stage_=20;
    } else if(stage_==20) {
        if(!gate("C").finished() || !workflow_shell("C")->ready()) return;
        const auto path=std::filesystem::path(image_).parent_path()/"cancel.bin";
        paste_scenario_.start_receiver(path,"cancel");
        auto* target=workflow_shell("C");target->begin_output_observation();target->fixture_command(paste_scenario_.receiver_command(path));stage_=21;
    } else if(stage_==21 && paste_scenario_.receiver->ready()) {
        auto* target=workflow_shell("C");
        target->arm_next_paste_fence(1,started_+std::chrono::milliseconds(70000));
        if(workflow_system_) {
            tf_shell_test::require_hosted_ime_input();workflow_select("C");tf_shell::write_unicode_clipboard(window_,std::u16string(paste_scenario_.large_units,u'汉'));
            workflow_system_shortcut("C",false);workflow_system_point("large-cancel-requested");stage_=221;
        } else {
            target->synthetic_paste(std::u16string(paste_scenario_.large_units,u'汉'));
            paste_scenario_.transfer=target->observed_paste();require(paste_scenario_.transfer!=nullptr,"workflow cancel transfer missing");stage_=22;
            paste_scenario_.fence=target->observed_paste_fence();
        }
    } else if(stage_==221) {
        paste_scenario_.transfer=workflow_shell("C")->observed_paste();if(!paste_scenario_.transfer) return;
        paste_scenario_.fence=workflow_shell("C")->observed_paste_fence();
        workflow_system_point("large-cancel-ready");stage_=22;
    } else if(stage_==22 && paste_scenario_.held()) {
        paste_scenario_.other_consumed=workflow_shell("F1")->runtime_snapshot().consumed_bytes;stage_=222;
    } else if(stage_==222 && workflow_shell("F1")->runtime_snapshot().consumed_bytes>paste_scenario_.other_consumed) {
        paste_scenario_.pending.other_responsive=true;
        require(!tf_shell::paste_terminal(paste_scenario_.transfer->progress().state),"workflow cancel transfer completed before gate");
        paste_scenario_.receiver->allow_read();stage_=23;
    } else if(stage_==23 && paste_scenario_.receiver->checkpoint() && workflow_shell("C")->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos) {
        paste_scenario_.before_action(std::filesystem::path(image_).parent_path()/"cancel.bin",workflow_shell("C")->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos);
        backgrounds_running();workflow_checkpoint("paste-cancel-pending",0,{"B","C"});
        workflow_select("C");paste_scenario_.key(workflow_shell("C")->handle(),VK_ESCAPE);stage_=24;
    } else if(stage_==24 && (paste_scenario_.transfer->progress().state==tf_shell::PasteState::cancelling || paste_scenario_.transfer->progress().state==tf_shell::PasteState::cancelled)) {
        paste_scenario_.receiver->release();stage_=25;
    } else if(stage_==25 && tf_shell::paste_terminal(paste_scenario_.transfer->progress().state) && !workflow_shell("C")->paste_busy()) {
        require(paste_scenario_.transfer->progress().state==tf_shell::PasteState::cancelled,"workflow Escape did not cancel paste");
        workflow_shell("C")->command(std::wstring(1,4));stage_=26;
    } else if(stage_==26 && workflow_shell("C")->live_contains_line("TF_KEY_DONE")) {
        paste_scenario_.finish(std::filesystem::path(image_).parent_path()/"cancel.bin",true);
        workflow_checkpoint("paste-cancelled",0,{"B","C"});stage_=11;
    } else if(stage_==11) {
        for(const std::string name:{"F1","F2","F3","F4","F5"}) {
            if(!gate(name).done(tf_shell_test::WorkflowPhase::output)) return;
            const auto document=workflow_shell(name)->observed_document_lines();
            // Seeing the marker's text does not prove its terminating newline
            // has reached the parser. Wait for the complete document boundary.
            if(document.size()<2 || !document.back().projection.text.empty() ||
               document[document.size()-2].projection.text!="TF_R108_"+name+"_OUTPUT_DONE 0500") return;
        }
        workflow_checkpoint("background-complete");
        for(auto& slot:evidence.slots) if(slot.name!="B") slot.controller->stop();stage_=12;
    } else if(stage_==12) {
        for(auto& slot:evidence.slots) if(slot.name!="B" && !slot.controller->finished()) return;
        evidence.cycle=1;stage_=30;
    } else if(stage_==30) {
        auto* target=workflow_shell("F4");workflow_select("F4");target->set_search_work_budget(1);target->request_find();
        require(target->find_edit_handle() && SetWindowTextW(target->find_edit_handle(),L"TF_R108_F4_"),"cycle Find query failed");stage_=31;
    } else if(stage_==31) {
        auto* target=workflow_shell("F4");if(target->search_status()!=termforge::SearchStatus::running) return;
        for(const std::string name:{"F4","F5"}) for(auto& slot:evidence.slots) if(slot.name==name && !slot.retired) {
            auto* shell=workflow_shell(name);slot.closing_find=shell->find_handle();slot.closed_while_fixture_running=!slot.controller->process_signaled();shell->close();
        }
        stage_=32;
    } else if(stage_==32) {
        if(session_count()!=6 || collected_!=static_cast<int>(evidence.cycle*2)) return;
        for(const std::string name:{"A","B","C","F1","F2","F3"}) if(!workflow_shell(name)->settled()) return;
        workflow_checkpoint("cycle-six",evidence.cycle);evidence.six_since=Clock::now();stage_=36;
    } else if(stage_==36) {
        if(ms(evidence.six_since)<1000) return;
        require(session_count()==6,"cycle-six dwell changed live count");
        for(const std::string name:{"A","B","C","F1","F2","F3"}) require(workflow_shell(name) && workflow_shell(name)->settled() && !workflow_shell(name)->paste_busy(),"six-resident dwell left live work");
        workflow_resource_mark("cycle-six-end",evidence.cycle);
        workflow_attach("F4","F4",Group::bottom,true);workflow_attach("F5","F5",Group::bottom,true);stage_=33;
    } else if(stage_==33) {
        if(!workflow_shell("F4")->ready() || !workflow_shell("F5")->ready()) return;
        workflow_shell("F4")->fixture_command(gate("F4").command());workflow_shell("F5")->fixture_command(gate("F5").command());stage_=34;
    } else if(stage_==34) {
        if(!gate("F4").ready() || !gate("F5").ready()) return;
        gate("F4").start(tf_shell_test::WorkflowPhase::output);gate("F5").start(tf_shell_test::WorkflowPhase::output);stage_=35;
    } else if(stage_==35) {
        for(const std::string name:{"F4","F5"}) {
            if(std::filesystem::file_size(gate(name).path.wstring()+L".source.bin")<4096) return;
            const auto prefix="TF_R108_"+name+'_';bool consumed=false;
            for(const auto& row:workflow_shell(name)->observed_document_lines()) {
                const auto& text=row.projection.text;
                if(text.rfind(prefix,0)==0 && text.size()>prefix.size() && text[prefix.size()]>='0' && text[prefix.size()]<='9') consumed=true;
            }
            if(!consumed) return;
        }
        workflow_checkpoint("cycle-eight",evidence.cycle);
        if(evidence.cycle<5) {++evidence.cycle;stage_=30;} else stage_=40;
    } else if(stage_==40) {
        paste_scenario_.start_receiver(std::filesystem::path(image_).parent_path()/"close.bin","close");
        auto* target=workflow_shell("C");paste_scenario_.target=target->handle();target->begin_output_observation();target->fixture_command(paste_scenario_.receiver_command(std::filesystem::path(image_).parent_path()/"close.bin"));stage_=41;
    } else if(stage_==41 && paste_scenario_.receiver->ready()) {
        auto* target=workflow_shell("C");
        target->arm_next_paste_fence(2,started_+std::chrono::milliseconds(70000));
        if(workflow_system_) {
            tf_shell_test::require_hosted_ime_input();workflow_select("C");tf_shell::write_unicode_clipboard(window_,std::u16string(paste_scenario_.large_units,u'汉'));
            workflow_system_shortcut("C",false);workflow_system_point("large-close-requested");stage_=241;
        } else {
            target->synthetic_paste(std::u16string(paste_scenario_.large_units,u'汉'));
            paste_scenario_.transfer=target->observed_paste();require(paste_scenario_.transfer!=nullptr,"workflow close transfer missing");stage_=42;
            paste_scenario_.fence=target->observed_paste_fence();
        }
    } else if(stage_==241) {
        paste_scenario_.transfer=workflow_shell("C")->observed_paste();if(!paste_scenario_.transfer) return;
        paste_scenario_.fence=workflow_shell("C")->observed_paste_fence();
        workflow_system_point("large-close-ready");stage_=42;
    } else if(stage_==42 && paste_scenario_.held()) {
        paste_scenario_.other_consumed=workflow_shell("F4")->runtime_snapshot().consumed_bytes;stage_=242;
    } else if(stage_==242 && workflow_shell("F4")->runtime_snapshot().consumed_bytes>paste_scenario_.other_consumed) {
        paste_scenario_.pending.other_responsive=true;
        require(!tf_shell::paste_terminal(paste_scenario_.transfer->progress().state),"workflow close transfer completed before gate");paste_scenario_.receiver->allow_read();stage_=43;
    } else if(stage_==43 && paste_scenario_.receiver->checkpoint() && workflow_shell("C")->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos) {
        paste_scenario_.before_action(std::filesystem::path(image_).parent_path()/"close.bin",workflow_shell("C")->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos);
        workflow_checkpoint("paste-close-pending",5,{"B","C"});workflow_select("C");workflow_shell("C")->close();stage_=44;
    } else if(stage_==44) {
        if(session_count()!=7 || collected_!=11 || IsWindow(paste_scenario_.target)) return;
        paste_scenario_.finish(std::filesystem::path(image_).parent_path()/"close.bin",false);
        workflow_select("B");require(PostMessageW(workflow_shell("B")->handle(),WM_CHAR,'!',1)!=FALSE,"post-close target key failed");stage_=45;
    } else if(stage_==45) {
        if(gate("B").bytes()!=(workflow_system_?workflow_system_->input.prefix()+u8"你好!":u8"TF_R108_FOREGROUND 中文\r!")) return;
        workflow_checkpoint("after-paste-close",5,{"B"});gate("F4").stop();gate("F5").stop();gate("B").stop();stage_=46;
    } else if(stage_==46) {
        if(!gate("F4").finished() || !gate("F5").finished() || !gate("B").finished()) return;
        Bitmap frame(physical_width_,physical_height_);paint(frame.dc);
        for(auto& tab:main_tabs_) if(IsWindowVisible(tab.shell->handle())) {const auto rect=physical(main_terminal_);tab.shell->render_to(frame.dc,rect.left,rect.top);}
        for(auto& tab:tabs_) if(IsWindowVisible(tab.shell->handle())) {const auto rect=physical(terminal_);tab.shell->render_to(frame.dc,rect.left,rect.top);}
        frame.save(image_);stage_=100;close_all();
    }
}
