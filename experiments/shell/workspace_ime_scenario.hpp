// Explicit hosted input-desktop scenarios. Never used by private-desktop tests.
#pragma once
inline void Workspace::scenario_ime() {
    using namespace tf_shell_test;
    if(image_.empty() || closing_) return;
    require_hosted_ime_input();
    const auto directory=std::filesystem::path(image_).parent_path();
    if(!ime_scenario_) {
        ime_scenario_=std::make_unique<ProductImeScenarioState>();
        auto* evidence=ime_scenario_.get(); stage_=1;
        evidence->progress_path=directory/"progress.jsonl";
        ime_check(!std::filesystem::exists(evidence->progress_path),"IME progress evidence path must be new");
        add(Group::main);
        if(test_mode_=="ui-ime-focus") add(Group::bottom);
        select(0,Group::bottom);
        const auto shell=[this](const std::string& name)->ShellWindow* {
            if(name=="main") return main_tabs_.empty()?nullptr:main_tabs_.front().shell.get();
            const size_t index=name=="hidden"?1:0;
            return index<tabs_.size()?tabs_[index].shell.get():nullptr;
        };
        const auto receiver=[evidence](const std::string& name)->HostedImeReceiver& {
            for(auto& row:evidence->receivers) if(row->name==name) return *row;
            throw std::runtime_error("IME scenario receiver missing");
        };
        const auto observe=[evidence,shell,receiver](const std::string& name,const std::string& checkpoint) {
            auto* target=shell(name); ime_check(target!=nullptr,"IME target disappeared before checkpoint");
            evidence->checkpoint(checkpoint,receiver(name),target->ime_observation(),target->display_dpi(),target->zoom_percent(),target->pixel_cell_size(),target->handle());
        };
        const auto focus=[this,shell](const std::string& name) {
            select(name=="hidden"?1:0,name=="main"?Group::main:Group::bottom);
            HostedPinyin::focus(shell(name)->handle());
        };
        const auto type=[evidence,shell,receiver](const std::string& name,const std::string& text) {
            for(unsigned char code:text) evidence->add("key-"+name,[evidence,shell,receiver,name,code] {
                auto* target=shell(name); ime_check(target && !target->closing(),"IME input target is closing");
                evidence->key(target->handle(),receiver(name).generation,target->ime_observation().generation,code);
            },[] { return true; },120);
        };
        const auto key=[evidence,shell,receiver](const std::string& name,WORD code) {
            evidence->add("key-"+name,[evidence,shell,receiver,name,code] {
                auto* target=shell(name); ime_check(target && !target->closing(),"IME input target is closing");
                evidence->key(target->handle(),receiver(name).generation,target->ime_observation().generation,code);
            },[] { return true; },150);
        };
        const auto preedit=[evidence,shell,receiver,observe](const std::string& name,const std::string& checkpoint) {
            evidence->add("wait-"+checkpoint,[] {},[shell,name] {
                const auto obs=shell(name)->ime_observation(); return obs.composing && !obs.preedit.empty();
            },350);
            evidence->add(checkpoint,[evidence,receiver,observe,name,checkpoint] {
                ime_check(receiver(name).bytes().empty(),"preedit leaked to raw PTY receiver"); observe(name,checkpoint);
                const auto& row=evidence->find(checkpoint);
                bool visible=false; for(const auto& candidate:row.candidates) visible|=HostedCandidateObserver::candidate(candidate);
                ime_check(visible && row.observation.result_events==0 && row.observation.committed.empty() && row.observation.character_units.empty(),"real product preedit/candidate was not observed without early commit");
            });
        };
        const auto committed=[evidence,shell,receiver,observe](const std::string& name,const std::string& checkpoint) {
            evidence->add("wait-"+checkpoint,[] {},[shell,receiver,name] {
                return receiver(name).bytes()=="\xe4\xbd\xa0\xe5\xa5\xbd" && !shell(name)->composing();
            },300);
            evidence->add(checkpoint,[evidence,observe,name,checkpoint] {
                observe(name,checkpoint); const auto& obs=evidence->find(checkpoint).observation;
                ime_check(obs.committed=="\xe4\xbd\xa0\xe5\xa5\xbd" && obs.result_events==1 && obs.character_units.empty(),"product IME commit was duplicated/lost or converted through WM_CHAR");
            });
        };
        const auto cancel=[evidence,shell,receiver,observe,key,type](const std::string& name,const std::string& checkpoint) {
            type(name,"NI");
            evidence->add("cancel-preedit-"+name,[] {},[shell,name] { const auto obs=shell(name)->ime_observation(); return obs.composing && !obs.preedit.empty(); },200);
            key(name,VK_ESCAPE);
            evidence->add("wait-"+checkpoint,[] {},[evidence,shell,receiver,name,checkpoint] {
                const auto obs=shell(name)->ime_observation();
                if(obs.composing || !obs.preedit.empty()) return false;
                ime_check(receiver(name).bytes()=="\xe4\xbd\xa0\xe5\xa5\xbd" && obs.result_events==1 && obs.committed=="\xe4\xbd\xa0\xe5\xa5\xbd" && obs.character_units.empty(),"cancel emitted extra PTY input");
                evidence->actions[checkpoint]=true; return true;
            },350);
            evidence->add(checkpoint,[observe,name,checkpoint] { observe(name,checkpoint); });
        };

        evidence->add("sessions-ready",[] {},[this] {
            for(const auto group:{Group::bottom,Group::main}) for(const auto& tab:group_tabs(group)) if(!tab.shell->ready()) return false;
            return true;
        });
        evidence->add("start-raw-receivers",[evidence,directory,shell,this] {
            unsigned generation=1;
            for(const std::string name:test_mode_=="ui-ime-focus"?std::vector<std::string>{"bottom","main","hidden"}:std::vector<std::string>{"bottom","main"}) {
                auto gate=std::make_unique<HostedImeReceiver>(directory/(name+".bin"),name,generation++);
                shell(name)->begin_output_observation(); shell(name)->fixture_command(gate->command()); evidence->receivers.push_back(std::move(gate));
            }
        },[evidence] { for(auto& row:evidence->receivers) if(!row->ready()) return false; return true; });
        evidence->add("activate-fixed-pinyin",[evidence,shell] {
            evidence->provider=std::make_unique<HostedPinyin>();
            for(auto& row:evidence->receivers) { shell(row->name)->begin_output_observation(); shell(row->name)->begin_ime_observation(row->generation); }
        });

        if(test_mode_=="ui-ime-basic") {
            evidence->add("focus-bottom",[focus] { focus("bottom"); });
            type("bottom","NIHAO"); preedit("bottom","bottom-preedit");
            evidence->add("move-workspace",[this] {
                RECT bounds{}; require(GetWindowRect(window_,&bounds)!=FALSE,"IME workspace rectangle unavailable");
                require(SetWindowPos(window_,nullptr,bounds.left+32,bounds.top+32,bounds.right-bounds.left-16,bounds.bottom-bounds.top-16,SWP_NOZORDER)!=FALSE,"IME workspace move/resize failed"); layout();
            },[] { return true; },400);
            evidence->add("bottom-moved",[observe,evidence] { observe("bottom","bottom-moved"); evidence->require_moved("bottom-preedit","bottom-moved"); evidence->actions["candidate_followed_workspace"]=true; });
            key("bottom",VK_SPACE); committed("bottom","bottom-commit"); cancel("bottom","bottom-cancel");
            evidence->add("focus-main",[focus] { focus("main"); });
            type("main","NIHAO"); preedit("main","main-preedit"); key("main",VK_SPACE); committed("main","main-commit"); cancel("main","main-cancel");
        } else if(test_mode_=="ui-ime-geometry") {
            evidence->add("focus-main",[focus] { focus("main"); });
            type("main","NIHAO"); preedit("main","before-output");
            evidence->add("twenty-output-rows",[receiver] { receiver("main").emit_output(); },[shell,receiver] {
                return receiver("main").output_done() && shell("main")->observed_output().find("TF_IME_OUTPUT_DONE")!=std::string::npos;
            },400);
            evidence->add("after-output",[observe,evidence,shell,receiver] {
                observe("main","after-output"); evidence->require_moved("before-output","after-output");
                ime_check(shell("main")->composing() && receiver("main").bytes().empty(),"output ended composition or sent preedit");
                for(auto text:shell("main")->ime_retained_lines()) {
                    while(!text.empty() && text.back()==' ') text.pop_back();
                    if(text.rfind("TF_IME_OUTPUT",0)==0) evidence->output_screen_lines.push_back(std::move(text));
                }
                std::vector<std::string> expected;
                for(int i=0;i<20;++i) expected.push_back("TF_IME_OUTPUT "+std::to_string(i)); expected.push_back("TF_IME_OUTPUT_DONE");
                ime_check(evidence->output_screen_lines==expected,"twenty output rows absent/reordered in actual retained product text");
                evidence->actions["twenty_output_rows"]=true;
            });
            evidence->add("resize-workspace",[this] {
                RECT bounds{}; require(GetWindowRect(window_,&bounds)!=FALSE,"IME workspace resize query failed");
                require(SetWindowPos(window_,nullptr,bounds.left+24,bounds.top+16,bounds.right-bounds.left-80,bounds.bottom-bounds.top-64,SWP_NOZORDER)!=FALSE,"IME workspace resize failed"); layout();
            },[] { return true; },400);
            evidence->add("after-resize",[observe,evidence,shell,receiver] {
                observe("main","after-resize"); evidence->require_moved("after-output","after-resize");
                ime_check(shell("main")->composing() && receiver("main").bytes().empty(),"resize ended composition or sent preedit"); evidence->actions["resize"]=true;
            });
            evidence->add("zoom-125",[this,shell] { ime_check(GetFocus()==shell("main")->handle(),"geometry zoom lost main focus"); action(9); },[] { return true; },400);
            evidence->add("after-zoom",[observe,evidence,shell,receiver] {
                observe("main","after-zoom"); evidence->require_moved("after-resize","after-zoom");
                ime_check(shell("main")->composing() && shell("main")->zoom_percent()==125 && receiver("main").bytes().empty(),"zoom lost composition/metrics"); evidence->actions["zoom_125"]=true;
            });
            evidence->add("controlled-dpi-120",[this] { set_dpi(120); layout(); },[] { return true; },400);
            evidence->add("after-dpi",[observe,evidence,shell,receiver] {
                observe("main","after-dpi"); evidence->require_moved("after-zoom","after-dpi");
                ime_check(shell("main")->composing() && shell("main")->display_dpi()==120 && receiver("main").bytes().empty(),"controlled DPI lost composition/metrics"); evidence->actions["controlled_dpi_120"]=true;
            });
            key("main",VK_SPACE); committed("main","committed"); cancel("main","cancelled");
        } else if(test_mode_=="ui-ime-focus") {
            evidence->add("focus-bottom",[focus] { focus("bottom"); });
            type("bottom","NIHAO"); preedit("bottom","bottom-preedit");
            evidence->add("hide-bottom",[focus] { focus("hidden"); },[] { return true; },400);
            evidence->add("bottom-hidden",[observe,evidence,shell,receiver] {
                observe("bottom","bottom-hidden"); const auto obs=shell("bottom")->ime_observation();
                ime_check(!IsWindowVisible(shell("bottom")->handle()) && !obs.composing && obs.preedit.empty() && receiver("bottom").bytes().empty(),"hidden session retained/sent preedit"); evidence->actions["hidden_cancelled"]=true;
                for(const auto& candidate:evidence->find("bottom-hidden").candidates) for(const auto& old:evidence->find("bottom-preedit").candidates)
                    if(HostedCandidateObserver::candidate(candidate) && HostedCandidateObserver::candidate(old))
                        ime_check(candidate.instance!=old.instance,"hidden target left its candidate visible");
            });
            evidence->add("focus-main",[focus] { focus("main"); }); type("main","NIHAO"); key("main",VK_SPACE); committed("main","main-commit");
            evidence->add("resume-bottom",[focus] { focus("bottom"); }); type("bottom","NIHAO"); key("bottom",VK_SPACE); committed("bottom","bottom-resumed");
            evidence->add("rename-start-preedit",[focus] { focus("hidden"); });
            type("hidden","NI"); preedit("hidden","hidden-preedit");
            evidence->add("begin-rename",[this] { begin_rename(1,Group::bottom); HostedPinyin::focus(rename_); },[] { return true; },350);
            evidence->add("rename-cancelled-old",[evidence,shell,receiver] {
                const auto obs=shell("hidden")->ime_observation();
                ime_check(!obs.composing && obs.preedit.empty() && receiver("hidden").bytes().empty(),"rename did not cancel shell preedit"); evidence->actions["rename_cancelled_old"]=true;
            });
            for(char code:std::string("NIHAO")) evidence->add("rename-key",[this,evidence,code] {
                ime_check(rename_ && GetFocus()==rename_,"rename lost its actual EDIT focus"); evidence->key(rename_,4,4,static_cast<WORD>(code));
            },[] { return true; },120);
            evidence->add("wait-rename-preedit",[] {},[this] {
                HIMC context=ImmGetContext(rename_); if(!context) return false;
                const LONG size=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0); ImmReleaseContext(rename_,context);
                return size>0 && size<=512 && size%2==0;
            },350);
            evidence->add("rename-preedit",[this,evidence] {
                HIMC context=ImmGetContext(rename_); ime_check(context!=nullptr,"rename EDIT context missing");
                const LONG size=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);
                ime_check(size>0 && size<=512 && size%2==0,"rename EDIT has no real preedit");
                std::wstring text(static_cast<size_t>(size)/2,0);
                const LONG read=ImmGetCompositionStringW(context,GCS_COMPSTR,text.data(),size); ImmReleaseContext(rename_,context);
                ime_check(read==size,"rename preedit read changed"); evidence->rename_preedit=ime_utf8(text);
                evidence->rename_candidates=evidence->candidate_observer.sample();
            },[] { return true; },350);
            evidence->add("rename-commit-key",[this,evidence] { evidence->key(rename_,4,4,VK_SPACE); },[] { return true; },350);
            evidence->add("rename-commit",[this,evidence,receiver] {
                wchar_t text[256]{}; const auto length=GetWindowTextW(rename_,text,256); evidence->rename_text=ime_utf8(std::wstring(text,static_cast<size_t>(length)));
                ime_check(evidence->rename_text=="\xe4\xbd\xa0\xe5\xa5\xbd" && receiver("bottom").bytes()=="\xe4\xbd\xa0\xe5\xa5\xbd" && receiver("main").bytes()=="\xe4\xbd\xa0\xe5\xa5\xbd" && receiver("hidden").bytes().empty(),"rename text leaked/duplicated into PTY receivers");
                end_rename(true); evidence->actions["rename_text_verified"]=true;
            });
        } else if(test_mode_=="ui-ime-close") {
            evidence->add("focus-bottom",[focus] { focus("bottom"); }); type("bottom","NIHAO"); preedit("bottom","before-close");
            evidence->add("close-composing-target",[this,evidence,shell,receiver] {
                evidence->closed_target=shell("bottom")->handle(); evidence->terminal_output["bottom"]=shell("bottom")->observed_output();
                receiver("bottom").stopped_by_close=true; action(2);
            },[this,evidence,receiver] { return collected_==1 && !IsWindow(evidence->closed_target) && receiver("bottom").process_signaled(); },350);
            evidence->add("closed-target-checked",[evidence,receiver] {
                ime_check(receiver("bottom").bytes().empty(),"closing target emitted preedit"); evidence->actions["closed_target_destroyed"]=true; evidence->actions["closed_receiver_reaped"]=true;
                evidence->after_close_candidates=evidence->candidate_observer.sample();
                for(const auto& candidate:evidence->after_close_candidates) for(const auto& old:evidence->find("before-close").candidates)
                    if(HostedCandidateObserver::candidate(candidate) && HostedCandidateObserver::candidate(old))
                        ime_check(candidate.instance!=old.instance,"closed target left its candidate visible");
                evidence->actions["closed_candidate_gone"]=true;
            });
            evidence->add("focus-main",[focus] { focus("main"); }); type("main","NIHAO"); key("main",VK_SPACE); committed("main","survivor-commit"); cancel("main","survivor-cancel");
        } else throw std::runtime_error("unrecognized hosted IME scenario");
        evidence->add("stop-raw-receivers",[evidence] { for(auto& row:evidence->receivers) if(!row->stopped_by_close) row->stop(); },[evidence] {
            for(const auto& row:evidence->receivers) if(row->stopped_by_close?!row->process_signaled():!row->finished()) return false;
            return true;
        },350);
        evidence->add("preserve-independent-evidence",[this,evidence,directory,shell] {
            for(const auto& row:evidence->receivers) if(!row->stopped_by_close) evidence->terminal_output[row->name]=shell(row->name)->observed_output();
            evidence->save(directory/"product-ime.json",test_mode_);
            Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
            for(const auto group:{Group::bottom,Group::main}) if(auto* target=selected(group)) {
                const RECT rect=physical(group==Group::main?main_terminal_:terminal_); target->render_to(frame.dc,rect.left,rect.top);
            }
            frame.save(image_);
        });
    }
    if(ime_scenario_->tick()) { stage_=100; close_all(); }
}
