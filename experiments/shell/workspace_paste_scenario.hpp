// Message-pump confirmation, target isolation, cancellation and owned closure.
#pragma once
#include "clipboard.hpp"
inline void Workspace::scenario_paste() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<35000,"paste window scenario timeout");
    auto& evidence=paste_scenario_;
    const auto directory=std::filesystem::path(image_).parent_path();
    // The close case destroys the first tab. Never retain its C++ address
    // across a message-pump turn; the saved HWND is only checked for liveness.
    auto* first=stage_<30?tabs_.front().shell.get():nullptr;
    const bool system=test_mode_=="ui-paste-system";
    auto request=[&](std::u16string text) {
        if(system) {
            const auto env=[](const wchar_t* name,const wchar_t* expected) {
                wchar_t value[128]{}; const auto size=GetEnvironmentVariableW(name,value,128);
                return size && size<128 && std::wstring(value)==expected;
            };
            require(env(L"GITHUB_ACTIONS",L"true") && env(L"RUNNER_ENVIRONMENT",L"github-hosted"),
                "system clipboard scenario requires a disposable GitHub-hosted VM");
            tf_shell::write_unicode_clipboard(window_,text);
            BYTE saved[256]{}, keys[256]{};
            require(GetKeyboardState(saved)!=FALSE,"save paste key state");
            keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;
            require(SetKeyboardState(keys)!=FALSE,"set paste key state");
            MSG key{}; key.hwnd=first->handle(); key.message=WM_KEYDOWN; key.wParam='V'; key.lParam=1;
            ShellWindow::dispatch_message(key);
            require(SetKeyboardState(saved)!=FALSE,"restore paste key state");
        } else first->synthetic_paste(std::move(text));
    };
    auto prompt_key=[&](int focused_control,WPARAM key) {
        const HWND prompt=first->paste_prompt_handle();
        require(prompt && GetFocus()==GetDlgItem(prompt,focused_control),"paste prompt keyboard focus mismatch");
        evidence.key(GetFocus(),key);
    };
    if(stage_==0 && first->ready()) {
        first->start_input_receiver(directory/L"input.bin"); stage_=1;
    } else if(stage_==1 && first->live_contains_line("TF_KEY_READY")) {
        request(u"DEFAULT_ENTER_MUST_CANCEL\r"); stage_=2;
    } else if(stage_==2 && first->paste_prompt_handle()) {
        prompt_key(IDCANCEL,VK_RETURN); stage_=3;
    } else if(stage_==3 && !first->paste_busy()) {
        evidence.default_enter=true; request(u"ESCAPE_MUST_CANCEL\r"); stage_=4;
    } else if(stage_==4 && first->paste_prompt_handle()) {
        prompt_key(IDCANCEL,VK_TAB); stage_=5;
    } else if(stage_==5 && GetFocus()==GetDlgItem(first->paste_prompt_handle(),IDOK)) {
        prompt_key(IDOK,VK_ESCAPE); stage_=6;
    } else if(stage_==6 && !first->paste_busy()) {
        evidence.tab_escape=true;
        std::u16string text; for(int i=0;i<5000;++i) text+=u"P\u6c49\r\n";
        request(std::move(text)); stage_=7;
    } else if(stage_==7 && first->paste_prompt_handle()) {
        const HWND prompt=first->paste_prompt_handle(); RECT bounds{}; GetWindowRect(prompt,&bounds);
        Bitmap confirmation(bounds.right-bounds.left,bounds.bottom-bounds.top);
        SendMessageW(prompt,WM_PRINT,reinterpret_cast<WPARAM>(confirmation.dc),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
        confirmation.save((directory/"confirmation.bmp").string());
        add(); stage_=8;
    } else if(stage_==8 && active()->ready()) {
        require(active()!=first && !IsWindowVisible(first->handle()),"paste target was not hidden");
        SetFocus(GetDlgItem(first->paste_prompt_handle(),IDCANCEL));
        prompt_key(IDCANCEL,VK_TAB); stage_=9;
    } else if(stage_==9 && GetFocus()==GetDlgItem(first->paste_prompt_handle(),IDOK)) {
        prompt_key(IDOK,VK_RETURN); stage_=10;
    } else if(stage_==10 && !first->paste_busy()) {
        evidence.tab_enter=true;
        require(GetFocus()==active()->handle(),"paste confirmation focused a hidden session");
        first->command(std::wstring(1,4));
        active()->command(L"echo TF_PASTE_OTHER\r"); stage_=11;
    } else if(stage_==11 && first->live_contains_line("TF_KEY_DONE") && active()->live_contains_line("TF_PASTE_OTHER")) {
        SendMessageW(GetFocus(),WM_CHAR,'x',1); stage_=12;
    } else if(stage_==12 && active()->live_contains_line("TF>x")) {
        require(!first->live_contains_line("TF>x"),"ordinary input reached the hidden paste target");
        evidence.start_receiver(directory/"cancel.bin","cancel");
        first->begin_output_observation(); first->command(evidence.receiver_command(directory/"cancel.bin")); stage_=13;
    } else if(stage_==13 && evidence.receiver->ready()) {
        first->synthetic_paste(std::u16string(evidence.large_units,u'\u6c49'));
        evidence.transfer=first->observed_paste();
        require(evidence.transfer!=nullptr,"cancel test could not observe its window transfer");
        active()->command(L"\recho TF_CANCEL_RESPONSIVE\r"); stage_=14;
    } else if(stage_==14 && evidence.transfer->progress().sent>=16384 && active()->live_contains_line("TF_CANCEL_RESPONSIVE")) {
        require(!tf_shell::paste_terminal(evidence.transfer->progress().state),"cancel transfer completed before receiver gate");
        evidence.receiver->allow_read(); stage_=15;
    } else if(stage_==15 && evidence.receiver->checkpoint()) {
        evidence.before_action(directory/"cancel.bin",first->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos);
        select(0); evidence.key(first->handle(),VK_ESCAPE); stage_=16;
    } else if(stage_==16 && (evidence.transfer->progress().state==tf_shell::PasteState::cancelling ||
                           evidence.transfer->progress().state==tf_shell::PasteState::cancelled)) {
        evidence.receiver->release(); stage_=17;
    } else if(stage_==17 && tf_shell::paste_terminal(evidence.transfer->progress().state) && !first->paste_busy()) {
        require(evidence.transfer->progress().state==tf_shell::PasteState::cancelled,"Escape did not cancel window paste");
        first->command(std::wstring(1,4)); stage_=18;
    } else if(stage_==18 && first->observed_output().find("TF_KEY_DONE")!=std::string::npos) {
        evidence.finish(directory/"cancel.bin",true);
        evidence.start_receiver(directory/"close.bin","close"); evidence.target=first->handle();
        first->begin_output_observation(); first->command(evidence.receiver_command(directory/"close.bin")); stage_=19;
    } else if(stage_==19 && evidence.receiver->ready()) {
        first->synthetic_paste(std::u16string(evidence.large_units,u'\u6c49'));
        evidence.transfer=first->observed_paste();
        require(evidence.transfer!=nullptr,"close test could not observe its window transfer");
        select(1); active()->command(L"echo TF_CLOSE_RESPONSIVE\r"); stage_=20;
    } else if(stage_==20 && evidence.transfer->progress().sent>=16384 && active()->live_contains_line("TF_CLOSE_RESPONSIVE")) {
        require(!tf_shell::paste_terminal(evidence.transfer->progress().state),"close transfer completed before receiver gate");
        evidence.receiver->allow_read(); stage_=21;
    } else if(stage_==21 && evidence.receiver->checkpoint()) {
        evidence.before_action(directory/"close.bin",first->observed_output().find("TF_PASTE_FLOOD")!=std::string::npos);
        select(0); SendMessageW(window_,WM_COMMAND,2,0); stage_=30;
    } else if(stage_==30 && collected_==1 && !IsWindow(evidence.target)) {
        evidence.finish(directory/"close.bin",false); evidence.save(directory/"paste-workflow.json");
        require(session_count()==1 && active() && !active()->closing(),"closing paste target damaged other session");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        const RECT rect=physical(terminal_); active()->render_to(frame.dc,rect.left,rect.top); frame.save(image_);
        stage_=100; close_all();
    }
}
