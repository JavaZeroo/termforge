// Message-path keyboard check. Actual IME and physical-key latency are separate.
#pragma once
inline void Workspace::scenario_keyboard() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<15000,"keyboard scenario timeout");
    auto* shell=tabs_.front().shell.get();
    if(stage_==0 && shell->ready()) {
        const auto capture=std::filesystem::path(image_).parent_path()/L"input.bin";
        shell->start_input_receiver(capture); stage_=1;
    } else if(stage_==1 && shell->live_contains_line("TF_KEY_READY")) {
        BYTE saved[256]{}; require(GetKeyboardState(saved)!=FALSE,"read scripted keyboard state");
        auto modifiers=[&](bool shift,bool ctrl,bool alt) {
            BYTE state[256]{}; state[VK_SHIFT]=shift?0x80:0; state[VK_CONTROL]=ctrl?0x80:0; state[VK_MENU]=alt?0x80:0;
            require(SetKeyboardState(state)!=FALSE,"set scripted keyboard state");
        };
        auto character=[&](wchar_t value,UINT message=WM_CHAR) { SendMessageW(shell->handle(),message,value,1); };
        auto key=[&](WPARAM value) {
            MSG message{}; message.hwnd=shell->handle(); message.message=WM_KEYDOWN; message.wParam=value;
            message.lParam=1|(static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(value),MAPVK_VK_TO_VSC))<<16);
            ShellWindow::dispatch_message(message);
            // TranslateMessage queues WM_CHAR; drain it before the next
            // independent sequence, using the same window dispatch path.
            while(PeekMessageW(&message,shell->handle(),WM_CHAR,WM_SYSCHAR,PM_REMOVE)) DispatchMessageW(&message);
        };
        try {
            modifiers(false,false,false);
            const std::wstring sample=L"A\x6c49" L"e\x0301\xd83d\xde42" L"\xd83d\xdc69\x200d\xd83d\xdcbb" L"1\xfe0f\x20e3";
            for(wchar_t value:sample) character(value);
            key(VK_TAB);
            modifiers(true,false,false); key(VK_TAB);
            modifiers(false,true,false); key(VK_SPACE); key(VK_LEFT);
            modifiers(true,false,true); key(VK_DELETE);
            modifiers(false,false,true); character(L'x',WM_SYSCHAR);
            modifiers(false,true,true); character(L'\x00e9',WM_SYSCHAR);
            modifiers(false,false,false);
            character(0); character(L'\b');
            for(auto value:{VK_HOME,VK_END,VK_INSERT,VK_DELETE,VK_PRIOR,VK_NEXT}) key(value);
            character(0xd800); character(0xd83d); character(0xde42);
            character(0xd800);
            begin_rename(0); require(GetFocus()==rename_,"keyboard test did not change focus");
            end_rename(false); require(GetFocus()==shell->handle(),"keyboard test did not restore focus");
            character(0xdc00); character(4);
        } catch(...) { SetKeyboardState(saved); throw; }
        require(SetKeyboardState(saved)!=FALSE,"restore scripted keyboard state"); stage_=2;
    } else if(stage_==2 && shell->live_contains_line("TF_KEY_DONE")) {
        require(shell->frame_matches_full(),"keyboard retained frame mismatch");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        const RECT rect=physical(terminal_); shell->render_to(frame.dc,rect.left,rect.top); frame.save(image_);
        stage_=100; close_all();
    }
}
