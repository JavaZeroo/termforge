// Selection ownership through real tab/EDIT/CR-output/close message paths.
#pragma once
#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

inline void Workspace::scenario_selection_context() {
    if (image_.empty() || closing_) return;
    const bool system = test_mode_ == "ui-copy-context-system";
    const auto directory = std::filesystem::path(image_).parent_path();
    auto* a = tabs_.empty() ? nullptr : tabs_.front().shell.get();
    auto* b = main_tabs_.empty() ? nullptr : main_tabs_.front().shell.get();
    auto* c = main_tabs_.size() < 2 ? nullptr : main_tabs_[1].shell.get();
    static std::vector<std::string> samples;
    static std::array<BYTE,256> saved_keys{};
    static bool restore_keys = false;
    static int phase_tick = 0;
    static uint64_t before_id = 0, before_version = 0, after_id = 0, after_version = 0;
    static HWND c_hwnd = nullptr, edit_hwnd = nullptr;
    if (restore_keys) {
        require(SetKeyboardState(saved_keys.data()) != FALSE, "selection context keyboard restoration failed");
        restore_keys = false;
    }
    const auto save = [](const std::filesystem::path& path, const std::string& value, bool append = false) {
        std::ofstream out(path,std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        require(bool(out), "selection context evidence creation failed");
        out.write(value.data(),static_cast<std::streamsize>(value.size()));
        out.close(); require(out.good(), "selection context evidence write failed");
    };
    if (stage_ >= 12 && b) save(directory / "selection-context-overwrite.transport.bin",b->observed_output());
    require(ms(started_) < 30000, "selection context scenario exceeded its bounded deadline");
    const auto line_text = [](const auto& line) {
        return termforge::project_line(line).text;
    };
    const auto fixture_line = [](unsigned number) {
        std::ostringstream out; out << "TF_FIND_" << std::setw(3) << std::setfill('0') << number << " needle 中文 e\xcc\x81";
        return out.str();
    };
    const auto capture = [&](const char* slot) {
        const auto path = directory / (std::string("selection-context-") + slot + ".bin");
        if (!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path) <= 4096, "selection context capture exceeded its bound");
        std::ifstream in(path,std::ios::binary); require(bool(in), "selection context raw capture read failed");
        return std::string(std::istreambuf_iterator<char>(in),{});
    };
    const auto row_of = [&](ShellWindow* shell, const std::string& text, bool prefix = false) {
        const auto& view = shell->visible_snapshot();
        for (size_t index = 0; index < view.lines.size(); ++index) {
            const auto value = line_text(view.lines[index]);
            if (prefix ? value.rfind(text,0) == 0 : value == text) return static_cast<int>(index);
        }
        throw std::runtime_error("selection context fixture row is not visible: " + text);
    };
    const auto drag = [](ShellWindow* shell, int row, int columns) {
        const auto [width,height] = shell->pixel_cell_size();
        const auto begin = MAKELPARAM(width / 4,row * height + height / 2);
        const auto end = MAKELPARAM(columns * width + width / 4,row * height + height / 2);
        SendMessageW(shell->handle(),WM_LBUTTONDOWN,0,begin);
        require(GetCapture() == shell->handle(), "selection context drag did not establish capture");
        SendMessageW(shell->handle(),WM_MOUSEMOVE,MK_LBUTTON,end);
        SendMessageW(shell->handle(),WM_LBUTTONUP,0,end);
        require(GetCapture() != shell->handle(), "selection context drag did not release capture");
    };
    const auto queue_tab = [&](Group group, int index) {
        const RECT rect = tab_rect(group,index);
        const LPARAM point = MAKELPARAM(physical(rect.left + 12),physical(rect.top + 12));
        require(PostMessageW(window_,WM_LBUTTONDOWN,MK_LBUTTON,point) &&
                PostMessageW(window_,WM_LBUTTONUP,0,point), "selection context native tab mouse messages failed");
    };
    const auto queue_key = [&](HWND target, WPARAM key, bool control = false, bool shift = false) {
        require(target && IsWindow(target) && GetFocus() == target, "selection context queued key lost its actual focused HWND");
        require(GetKeyboardState(saved_keys.data()) != FALSE, "selection context keyboard state read failed");
        auto keys = saved_keys;
        for (int modifier : {VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})
            keys[static_cast<size_t>(modifier)] = 0;
        if (control) keys[VK_CONTROL] = 0x80;
        if (shift) keys[VK_SHIFT] = 0x80;
        require(SetKeyboardState(keys.data()) != FALSE, "selection context keyboard setup failed"); restore_keys = true;
        const LPARAM flags = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC)) << 16);
        const bool posted = PostMessageW(target,WM_KEYDOWN,key,flags) &&
            PostMessageW(target,WM_KEYUP,key,flags | (static_cast<LPARAM>(3) << 30));
        if (!posted) {
            const bool restored = SetKeyboardState(saved_keys.data()) != FALSE; restore_keys = false;
            require(restored, "selection context failed-post keyboard cleanup failed");
            require(false, "selection context key could not enter the Workspace message loop");
        }
    };
    const auto sentinel = [&]() {
        require(system, "local selection context must not write the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        tf_shell::write_unicode_clipboard(window_,u"TF_CONTEXT_SENTINEL 中文 e\u0301");
    };
    const auto clipboard = [&]() {
        require(system, "local selection context must not read the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        const auto value = tf_shell::read_unicode_clipboard(window_);
        const std::wstring wide(value.begin(),value.end()); return utf8(wide.c_str());
    };
    const auto queue_copy = [&](ShellWindow* shell) {
        if (system) { sentinel(); queue_key(shell->handle(),'C',true,true); }
        phase_tick = b->observed_tick_count();
    };
    const auto record = [&](const char* phase, const char* entry = "none", bool read_cf = false) {
        std::ostringstream out;
        const char* focus = rename_ && GetFocus() == rename_ ? "editor" :
            a && GetFocus() == a->handle() ? "A" : b && GetFocus() == b->handle() ? "B" :
            c && GetFocus() == c->handle() ? "C" : "other";
        const char* active_slot = active() == a ? "A" : active() == b ? "B" : c && active() == c ? "C" : "none";
        out << "{\"phase\":\"" << phase << "\",\"entry\":\"" << entry << "\",\"focus\":\"" << focus
            << "\",\"active\":\"" << active_slot << "\",\"main_tab_count\":" << main_tabs_.size()
            << ",\"clipboard\":";
        if (read_cf) termforge::Screen::quoted(out,clipboard()); else out << "null";
        out << ",\"raw\":{\"A\":"; termforge::Screen::quoted(out,capture("A"));
        out << ",\"B\":"; termforge::Screen::quoted(out,capture("B"));
        out << ",\"C\":"; termforge::Screen::quoted(out,capture("C")); out << '}';
        out << ",\"overwrite\":{\"before_id\":" << before_id << ",\"before_version\":" << before_version
            << ",\"after_id\":" << after_id << ",\"after_version\":" << after_version << '}';
        out << ",\"rename\":{\"present\":" << (rename_ ? "true" : "false")
            << ",\"old_editor_gone\":" << (edit_hwnd && !IsWindow(edit_hwnd) ? "true" : "false")
            << ",\"focused\":" << (rename_ && GetFocus() == rename_ ? "true" : "false") << ",\"text\":";
        if (rename_) {
            wchar_t text[65]{}, name[32]{}; DWORD first = 0, last = 0;
            GetWindowTextW(rename_,text,65); GetClassNameW(rename_,name,32);
            SendMessageW(rename_,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
            termforge::Screen::quoted(out,utf8(text)); out << ",\"class\":"; termforge::Screen::quoted(out,utf8(name));
            out << ",\"selection_begin\":" << first << ",\"selection_end\":" << last;
        } else out << "null,\"class\":null,\"selection_begin\":null,\"selection_end\":null";
        out << "},\"sessions\":[";
        const std::array<ShellWindow*,3> shells{a,b,c};
        for (size_t index = 0; index < shells.size(); ++index) {
            if (index) out << ',';
            auto* shell = shells[index];
            out << "{\"slot\":\"" << static_cast<char>('A' + index) << "\",\"present\":" << (shell ? "true" : "false");
            if (!shell) {
                out << ",\"closed_hwnd_gone\":" << (c_hwnd && !IsWindow(c_hwnd) ? "true" : "false") << '}';
                continue;
            }
            const auto value = shell->selected_text(); const auto state = shell->history_state();
            const auto& view = shell->visible_snapshot(); const auto [columns,rows] = shell->grid_dimensions();
            size_t overlays = 0;
            for (const auto& row : view.lines) for (const auto& cell : row)
                if (cell.style.bg == 4 && cell.style.fg == 15) ++overlays;
            out << ",\"visible\":" << (IsWindowVisible(shell->handle()) ? "true" : "false")
                << ",\"focused\":" << (GetFocus() == shell->handle() ? "true" : "false")
                << ",\"workspace_active\":" << (active() == shell ? "true" : "false")
                << ",\"closing\":" << (shell->closing() ? "true" : "false")
                << ",\"captured\":" << (GetCapture() == shell->handle() ? "true" : "false")
                << ",\"selected\":" << (shell->has_selection() ? "true" : "false")
                << ",\"status\":\"" << (value.status == termforge::SelectionCopyStatus::ok ? "ok" :
                    value.status == termforge::SelectionCopyStatus::empty ? "empty" :
                    value.status == termforge::SelectionCopyStatus::invalidated ? "invalidated" : "limit-exceeded")
                << "\",\"text\":";
            termforge::Screen::quoted(out,value.text);
            out << ",\"overlay_cells\":" << overlays << ",\"frame_matches_full\":" << (shell->frame_matches_full() ? "true" : "false")
                << ",\"columns\":" << columns << ",\"rows\":" << rows << ",\"ticks\":" << shell->observed_tick_count()
                << ",\"top\":" << state.top << ",\"total\":" << state.total << ",\"maximum\":" << state.maximum_top
                << ",\"tab_title\":";
            termforge::Screen::quoted(out,utf8(index == 0 ? tabs_.front().title.c_str() : main_tabs_[index-1].title.c_str()));
            out << ",\"product_status\":"; termforge::Screen::quoted(out,utf8(shell->status_text().c_str()));
            out << ",\"visible_lines\":[";
            for (size_t row = 0; row < view.lines.size(); ++row) {
                if (row) out << ','; out << "{\"id\":" << view.lines[row].id << ",\"version\":" << view.lines[row].version << ",\"text\":";
                termforge::Screen::quoted(out,line_text(view.lines[row])); out << '}';
            }
            out << "]}";
        }
        out << "]}"; samples.push_back(out.str());
        save(directory / "selection-context-progress.jsonl",out.str() + "\n",true);
    };
    const auto ticks = [&]() { return b && b->observed_tick_count() - phase_tick >= 3; };
    const auto selected_focus = [&](ShellWindow* shell) { return shell && active() == shell && GetFocus() == shell->handle(); };
    const auto ready = [](ShellWindow* shell) {
        return shell && shell->live_contains_line("TF_FIND_READY 080") && shell->settled();
    };
    const auto copy_entry = system ? "queued-Ctrl-Shift-C" : "none";
    if (stage_ == 0 && a && a->ready()) {
        samples.clear(); before_id = before_version = after_id = after_version = 0; c_hwnd = edit_hwnd = nullptr;
        if (system) tf_shell_test::require_hosted_clipboard();
        add(Group::main); add(Group::main); stage_ = 1;
    } else if (stage_ == 1 && b && c && b->ready() && c->ready() && a->settled() && b->settled() && c->settled()) {
        a->start_text_receiver(directory / L"selection-context-A.bin");
        b->start_text_receiver(directory / L"selection-context-B.bin");
        c->start_text_receiver(directory / L"selection-context-C.bin");
        c_hwnd = c->handle(); stage_ = 2;
    } else if (stage_ == 2 && ready(a) && ready(b) && ready(c)) {
        phase_tick = b->observed_tick_count(); stage_ = 3;
    } else if (stage_ == 3 && ticks()) {
        queue_tab(Group::bottom,0); stage_ = 4;
    } else if (stage_ == 4 && selected_focus(a)) {
        drag(a,row_of(a,fixture_line(77)),11); record("bottom-selected");
        queue_tab(Group::main,0); stage_ = 5;
    } else if (stage_ == 5 && selected_focus(b)) {
        drag(b,row_of(b,fixture_line(75)),11); record("main-a-selected");
        queue_tab(Group::main,1); stage_ = 6;
    } else if (stage_ == 6 && selected_focus(c)) {
        drag(c,row_of(c,fixture_line(79)),11); record("main-b-selected");
        phase_tick = b->observed_tick_count(); stage_ = 7;
    } else if (stage_ == 7 && ticks()) {
        record("main-a-hidden"); queue_tab(Group::main,0); stage_ = 8;
    } else if (stage_ == 8 && selected_focus(b)) {
        queue_copy(b); stage_ = 9;
    } else if (stage_ == 9 && ticks()) {
        record("main-a-restored",copy_entry,system);
        b->begin_output_observation(); b->fixture_command(L"~"); stage_ = 12;
    } else if (stage_ == 12 && b->observed_output().find("TF_STREAM_TICK_") != std::string::npos) {
        const int row = row_of(b,"TF_STREAM_TICK_",true);
        drag(b,row,15);
        const auto& actual = b->visible_snapshot().lines[static_cast<size_t>(row)];
        before_id = actual.id; before_version = actual.version; record("live-before");
        b->fixture_command(L"-"); stage_ = 13;
    } else if (stage_ == 13 && b->live_contains_line("TF_STREAM_STOPPED") && capture("B") == "~-") {
        const int row = row_of(b,"TF_STREAM_STOPPED");
        const auto& actual = b->visible_snapshot().lines[static_cast<size_t>(row)];
        after_id = actual.id; after_version = actual.version;
        queue_copy(b); stage_ = 14;
    } else if (stage_ == 14 && ticks()) {
        record("live-overwritten",copy_entry,system);
        drag(b,row_of(b,fixture_line(75)),11); record("main-a-reselected");
        queue_key(b->handle(),VK_F2); stage_ = 15;
    } else if (stage_ == 15 && rename_ && GetFocus() == rename_) {
        edit_hwnd = rename_;
        require(SetWindowTextW(rename_,L"Context rename 中文"), "selection context native EDIT text setup failed");
        SendMessageW(rename_,EM_SETSEL,0,-1); record("rename-editor");
        if (system) { sentinel(); queue_key(rename_,'C',true); }
        phase_tick = b->observed_tick_count(); stage_ = 16;
    } else if (stage_ == 16 && ticks()) {
        record("rename-copy",system ? "queued-Ctrl-C-native-EDIT" : "none",system);
        queue_key(rename_,VK_ESCAPE); stage_ = 17;
    } else if (stage_ == 17 && !rename_ && selected_focus(b)) {
        record("rename-cancelled"); queue_tab(Group::main,1); stage_ = 18;
    } else if (stage_ == 18 && selected_focus(c)) {
        queue_copy(c); stage_ = 19;
    } else if (stage_ == 19 && ticks()) {
        record("main-b-restored",copy_entry,system); record("main-b-close-prepared");
        queue_key(c->handle(),'W',true); stage_ = 20;
    } else if (stage_ == 20 && !c && main_tabs_.size() == 1 && c_hwnd && !IsWindow(c_hwnd) && selected_focus(b)) {
        record("main-b-closed"); queue_copy(b); stage_ = 21;
    } else if (stage_ == 21 && ticks()) {
        record("remaining-copy",copy_entry,system); queue_key(b->handle(),'X');
        phase_tick = b->observed_tick_count(); stage_ = 22;
    } else if (stage_ == 22 && ticks() && capture("B") == "~-x") {
        record("input-after");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for (const auto group : {Group::bottom,Group::main}) if (auto* shell = selected(group)) {
            const RECT rect = physical(group == Group::main ? main_terminal_ : terminal_);
            shell->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save(image_);
        std::ostringstream out;
        out << "{\"schema\":\"termforge-window-selection-context-v1\",\"system_clipboard\":" << (system ? "true" : "false")
            << ",\"input_source\":\"owned-tab-mouse-and-queued-workspace-edit-keys\",\"fixture\":\"shell_text_client-80-tilde-minus\""
            << ",\"local_cf_actions\":false,\"snapshots\":[";
        for (size_t index = 0; index < samples.size(); ++index) { if (index) out << ','; out << samples[index]; }
        out << "]}\n"; save(directory / "selection-context.json",out.str());
        stage_ = 100; close_all();
    }
}
