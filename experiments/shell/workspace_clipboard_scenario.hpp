// Product Paste/read/cancel/close acceptance on a disposable hosted VM only.
#pragma once
inline void Workspace::scenario_clipboard() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<45000,"clipboard window scenario timeout");
    auto& evidence=clipboard_scenario_;
    const auto directory=std::filesystem::path(image_).parent_path();
    const bool target_gone=evidence.index==3 && stage_>=5;
    auto* first=target_gone?nullptr:tabs_.front().shell.get();
    auto* other=tabs_.size()>1?tabs_[1].shell.get():(target_gone?tabs_.front().shell.get():nullptr);
    const auto captured=[&] { return tf_shell_test::PasteScenarioState::read(evidence.capture(directory)); };
    const auto ordinary=[&](ShellWindow* target,const std::string& text) {
        require(target && GetFocus()==target->handle(),"clipboard test input requires its focused session");
        for(const unsigned char character:text) SendMessageW(target->handle(),WM_CHAR,character,1);
    };
    const auto start_receiver=[&] {
        evidence.pending={}; evidence.pending.name=evidence.name(); evidence.helper.reset();
        evidence.receiver=std::make_unique<tf_shell_test::PasteReceiverGate>(100+evidence.index);
        require(!std::filesystem::exists(evidence.capture(directory)),"clipboard capture must be new");
        wchar_t path[32768]{}; const auto count=GetModuleFileNameW(nullptr,path,32768);
        require(count && count<32768,"clipboard receiver location failed");
        const auto client=std::filesystem::path(path).parent_path()/L"shell_input_client.exe";
        first->begin_output_observation();
        first->command(L"\""+client.wstring()+L"\" --paste-gated \""+evidence.capture(directory).wstring()+
            L"\" \""+evidence.receiver->prefix+L"\"\r");
    };
    const auto no_payload=[&] {
        require(!evidence.pending.prompt_seen && !evidence.pending.transfer_seen && captured()==evidence.baseline(),
                "clipboard reading created a prompt, transfer, or unexpected receiver bytes");
    };
    const auto finish_case=[&] {
        evidence.pending.receiver_bytes=captured().size();
        evidence.cases.push_back(evidence.pending); evidence.receiver.reset(); evidence.helper.reset();
    };
    // Observe every message-pump turn until retry is explicitly begun. A
    // transient confirmation or transfer must not disappear from the record.
    if(first && stage_>=4 && stage_<8) {
        evidence.pending.prompt_seen|=first->paste_prompt_handle()!=nullptr;
        evidence.pending.transfer_seen|=first->observed_paste()!=nullptr;
    }
    if(stage_==0 && first->ready()) {
        tf_shell_test::require_hosted_clipboard(); add(); stage_=1;
    } else if(stage_==1 && other && other->ready()) {
        select(0); start_receiver(); stage_=2;
    } else if(stage_==2 && evidence.receiver->ready()) {
        first->command(std::wstring(256,L'B')); evidence.receiver->allow_read(); stage_=3;
    } else if(stage_==3 && evidence.receiver->checkpoint()) {
        require(captured()==evidence.baseline(),"clipboard receiver baseline differs");
        evidence.receiver->release();
        // Setup writes are not the product behaviour under test; Explorer or a
        // previous case can hold the desktop clipboard for a moment, so give the
        // fixture a small bounded retry instead of failing the whole scenario.
        auto prepare_clipboard = [&](auto&& write) {
            std::string last;
            for (int attempt = 0; attempt < 4; ++attempt) {
                try { write(); return; } catch (const std::exception& error) { last = error.what(); }
                if (attempt + 1 != 4) Sleep(50 * (attempt + 1));
            }
            throw std::runtime_error(last);
        };
        try {
            if(evidence.index==0) {
                prepare_clipboard([&] { tf_shell::write_unicode_clipboard(window_,u"OCCUPIED_MUST_NOT_SEND"); });
                evidence.owner=std::make_unique<tf_shell_test::ClipboardScenarioOwner>(false);
            } else if(evidence.index==1) prepare_clipboard([&] { tf_shell_test::write_oversized_clipboard(window_); });
            else evidence.owner=std::make_unique<tf_shell_test::ClipboardScenarioOwner>(true);
        } catch(const std::exception& error) {
            throw std::runtime_error(std::string("clipboard case=")+evidence.name()+
                " phase=prepare-owner before-product-paste: "+error.what());
        }
        select(0); SendMessageW(window_,WM_COMMAND,12,0);
        const auto progress=first->observed_clipboard_read();
        require(progress && progress->state==tf_shell::ClipboardReadState::running && first->paste_busy(),
                "product Paste did not start an asynchronous clipboard read");
        evidence.pending.helper_pid=progress->process_id;
        evidence.helper.reset(OpenProcess(SYNCHRONIZE,FALSE,progress->process_id));
        require(evidence.helper.get()!=nullptr,"product clipboard helper observation failed");
        evidence.target=first->handle();
        select(1); ordinary(other,"echo "+evidence.marker(false)+"\r"); stage_=4;
    } else if(stage_==4 && other->live_contains_line(evidence.marker(false))) {
        evidence.pending.other_during=true;
        if(evidence.index<2) {
            if(first->paste_busy() || !evidence.helper_exited()) return;
            if(evidence.index==0) require(evidence.owner->occupied(),"clipboard owner stopped holding before failure");
            select(0); no_payload();
            require(first->status_text()==L"system clipboard read failed" && IsWindowVisible(first->handle()) &&
                    active()==first,"clipboard read error is not the selected session's visible status");
            Bitmap error_frame(physical_width_,physical_height_); paint(error_frame.dc);
            error_frame.save((directory/(std::string(evidence.name())+"-error.bmp")).string());
            evidence.pending.error_visible=true; stage_=5;
        } else if(evidence.owner->entered()) {
            const auto progress=first->observed_clipboard_read();
            require(progress && progress->state==tf_shell::ClipboardReadState::running && first->paste_busy() &&
                    !evidence.helper_exited(),"clipboard action missed the held system read");
            no_payload(); evidence.pending.owner_entered=true;
            select(0);
            if(evidence.index==2) paste_scenario_.key(first->handle(),VK_ESCAPE);
            else SendMessageW(window_,WM_COMMAND,2,0);
            stage_=5;
        }
    } else if(stage_==5 && evidence.helper_exited() &&
              (target_gone?(collected_==1 && !IsWindow(evidence.target)):!first->paste_busy())) {
        no_payload(); evidence.pending.helper_exited=true; evidence.pending.busy_cleared=true;
        evidence.pending.target_closed=target_gone;
        if(evidence.index==2) require(first->status_text()==L"clipboard read cancelled","Escape did not cancel the product read");
        if(evidence.owner) {
            if(evidence.index<2) evidence.owner->release();
            else {
                tf_shell_test::ClipboardReleaseProof proof;
                proof.target_quiescent=target_gone?(collected_==1 && !IsWindow(evidence.target)):!first->paste_busy();
                proof.payload_unchanged=!evidence.pending.prompt_seen && !evidence.pending.transfer_seen && captured()==evidence.baseline();
                evidence.owner->release_after_requester_exit(evidence.helper.get(),target_gone?
                    tf_shell_test::ClipboardReleaseReason::target_closed:tf_shell_test::ClipboardReleaseReason::requester_cancelled,proof);
            }
        }
        stage_=6;
    } else if(stage_==6 && (!evidence.owner || evidence.owner->released())) {
        if(evidence.owner) evidence.pending.owner_release=evidence.owner->finish();
        evidence.owner.reset();
        select(target_gone?0:1); ordinary(other,"echo "+evidence.marker(true)+"\r"); stage_=7;
    } else if(stage_==7 && other->live_contains_line(evidence.marker(true))) {
        evidence.pending.other_after=true; no_payload();
        if(target_gone) {
            finish_case(); evidence.save(directory/"clipboard-workflow.json");
            Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
            const RECT rect=physical(terminal_); active()->render_to(frame.dc,rect.left,rect.top); frame.save(image_);
            stage_=100; close_all();
        } else {
            require(!first->paste_busy(),"late clipboard result restarted input after cancellation/error");
            select(0); const auto retry=evidence.retry();
            tf_shell::write_unicode_clipboard(window_,std::u16string(retry.begin(),retry.end()));
            SendMessageW(window_,WM_COMMAND,12,0); stage_=8;
        }
    } else if(stage_==8 && !first->paste_busy() && captured()==evidence.baseline()+evidence.retry()) {
        require(!first->paste_prompt_handle(),"single-line retry unexpectedly confirmed");
        evidence.pending.retry_verified=true; first->command(std::wstring(1,4)); stage_=9;
    } else if(stage_==9 && first->observed_output().find("TF_KEY_DONE")!=std::string::npos) {
        require(captured()==evidence.baseline()+evidence.retry(),"retry contains late data, Escape or missing bytes");
        finish_case(); ++evidence.index; start_receiver(); stage_=2;
    }
}
