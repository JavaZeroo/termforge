// Real ConPTY output, pixel drag, queued Copy and controlled DPI acceptance.
// This file deliberately does not include or feed a parser fixture.
#pragma once
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include "scenario_tick_gate.hpp"

inline void Workspace::scenario_selection_bytes() {
    if (image_.empty() || closing_) return;
    const bool system = test_mode_ == "ui-copy-bytes-system";
    const auto directory = std::filesystem::path(image_).parent_path();
    auto* bottom = tabs_.front().shell.get();
    auto* main = selected(Group::main);
    static std::vector<std::string> samples;
    static std::array<std::pair<int,int>,2> initial_grids{};
    static std::array<BYTE,256> saved_keys{};
    static bool restore_keys = false;
    static tf_shell_test::TwoSessionTickGate phase_ticks;
    const auto begin_phase = [&]() {
        phase_ticks.begin({bottom->observed_tick_count(),main ? main->observed_tick_count() : 0});
    };
    if (restore_keys) {
        require(SetKeyboardState(saved_keys.data()) != FALSE, "selection bytes keyboard state restoration failed");
        restore_keys = false;
    }
    const auto save = [](const std::filesystem::path& path, const std::string& value, bool append = false) {
        std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        require(bool(out), "selection bytes evidence creation failed");
        out.write(value.data(), static_cast<std::streamsize>(value.size()));
        out.close(); require(out.good(), "selection bytes evidence write failed");
    };
    // Preserve the real transport even when a following assertion fails.
    if (stage_ >= 3) {
        save(directory / "selection-bytes-bottom.transport.bin", bottom->observed_output());
        if (main) save(directory / "selection-bytes-main.transport.bin", main->observed_output());
    }
    require(ms(started_) < 30000, "selection bytes scenario exceeded its bounded deadline");
    const auto capture = [&](const wchar_t* region) {
        const auto path = directory / (std::wstring(L"selection-bytes-") + region + L".bin");
        if (!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path) <= 4096, "selection bytes raw capture exceeded its bound");
        std::ifstream in(path, std::ios::binary); require(bool(in), "selection bytes raw capture read failed");
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto line_text = [](const auto& line) {
        return termforge::project_line(line).text;
    };
    const auto row_of = [&](ShellWindow* shell, const std::string& text, bool prefix = false) {
        const auto& view = shell->visible_snapshot();
        for (size_t row = 0; row < view.lines.size(); ++row) {
            auto actual = line_text(view.lines[row]);
            while (!actual.empty() && actual.back() == ' ') actual.pop_back();
            if (prefix ? actual.rfind(text, 0) == 0 : actual == text) return static_cast<int>(row);
        }
        throw std::runtime_error("selection bytes fixture row is not visible: " + text);
    };
    const auto pixel = [](ShellWindow* shell, int column, int row) {
        const auto [width,height] = shell->pixel_cell_size();
        // Both endpoints stay below the rounding midpoint used by drag.
        return POINT{column * width + width / 4, row * height + height / 2};
    };
    const auto drag = [&](POINT begin, POINT end) {
        SendMessageW(main->handle(), WM_LBUTTONDOWN, 0, MAKELPARAM(begin.x,begin.y));
        require(GetCapture() == main->handle(), "selection bytes drag did not capture its actual session");
        SendMessageW(main->handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x,end.y));
        SendMessageW(main->handle(), WM_LBUTTONUP, 0, MAKELPARAM(end.x,end.y));
        require(GetCapture() != main->handle(), "selection bytes drag did not release capture");
    };
    const auto queued_key = [&](WPARAM key, bool control = false, bool shift = false) {
        require(main && GetFocus() == main->handle() && active() == main, "selection bytes queued key lost its target");
        require(GetKeyboardState(saved_keys.data()) != FALSE, "selection bytes keyboard state read failed");
        auto modified = saved_keys;
        for (int modifier : {VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})
            modified[static_cast<size_t>(modifier)] = 0;
        if (control) modified[VK_CONTROL] = 0x80;
        if (shift) modified[VK_SHIFT] = 0x80;
        require(SetKeyboardState(modified.data()) != FALSE, "selection bytes keyboard state setup failed");
        restore_keys = true;
        const LPARAM flags = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC)) << 16);
        const bool posted = PostMessageW(main->handle(),WM_KEYDOWN,key,flags) != FALSE &&
            PostMessageW(main->handle(),WM_KEYUP,key,flags | (static_cast<LPARAM>(3) << 30)) != FALSE;
        if (!posted) {
            const bool restored = SetKeyboardState(saved_keys.data()) != FALSE; restore_keys = false;
            require(restored, "selection bytes failed-post keyboard cleanup failed");
            require(false, "selection bytes key could not enter the real Workspace message loop");
        }
    };
    const auto sentinel = [&]() {
        require(system, "local selection bytes mode must not write the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        tf_shell::write_unicode_clipboard(window_,u"TF_BYTES_SENTINEL 中文 e\u0301");
    };
    const auto read_clipboard = [&]() {
        require(system, "local selection bytes mode must not read the system clipboard");
        tf_shell_test::require_hosted_clipboard();
        const auto value = tf_shell::read_unicode_clipboard(window_);
        const std::wstring wide(value.begin(),value.end()); return utf8(wide.c_str());
    };
    const auto copy_if_system = [&]() {
        if (system) { sentinel(); queued_key('C',true,true); }
        begin_phase();
    };
    const auto transition = [&](int dpi) {
        // A controlled message never changes the OS monitor or the display.
        RECT next{0,0,MulDiv(1360,dpi,96),MulDiv(860,dpi,96)};
        const UINT os_dpi = GetDpiForWindow(window_);
        require(os_dpi && AdjustWindowRectExForDpi(&next,WS_OVERLAPPEDWINDOW,FALSE,0,os_dpi) != FALSE,
            "selection bytes DPI frame calculation failed");
        SendMessageW(window_,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&next));
        require(dpi_ == dpi && width_ == 1360 && height_ == 860, "selection bytes controlled DPI changed logical client size");
        begin_phase();
    };
    const auto record = [&](const char* phase, bool copy, bool empty_copy = false) {
        std::ostringstream out;
        out << "{\"phase\":\"" << phase << "\",\"controlled_dpi\":" << dpi_ << ",\"os_window_dpi\":" << GetDpiForWindow(window_)
            << ",\"copy_entry\":\"" << (copy ? "queued-Ctrl-Shift-C" : empty_copy ? "queued-Ctrl-Shift-C-empty" : "none")
            << "\",\"clipboard\":";
        if (system && (copy || empty_copy)) termforge::Screen::quoted(out,read_clipboard()); else out << "null";
        out << ",\"raw_bottom\":"; termforge::Screen::quoted(out,capture(L"bottom"));
        out << ",\"raw_main\":"; termforge::Screen::quoted(out,capture(L"main"));
        out << ",\"sessions\":[";
        for (size_t index = 0; index < 2; ++index) {
            auto* shell = index ? main : bottom;
            const auto value = shell->selected_text();
            const auto [columns,rows] = shell->grid_dimensions();
            const auto [cell_width,cell_height] = shell->pixel_cell_size();
            const auto& view = shell->visible_snapshot(); size_t overlays = 0;
            for (const auto& row : view.lines) for (const auto& cell : row)
                if (cell.style.bg == 4 && cell.style.fg == 15) ++overlays;
            const bool frame = shell->frame_matches_full();
            if (index) out << ',';
            out << "{\"group\":\"" << (index ? "main" : "bottom") << "\",\"selected\":" << (shell->has_selection() ? "true" : "false")
                << ",\"focused\":" << (GetFocus() == shell->handle() ? "true" : "false")
                << ",\"workspace_active\":" << (active() == shell ? "true" : "false")
                << ",\"captured\":" << (GetCapture() == shell->handle() ? "true" : "false")
                << ",\"status\":\"" << (value.status == termforge::SelectionCopyStatus::ok ? "ok" :
                    value.status == termforge::SelectionCopyStatus::empty ? "empty" :
                    value.status == termforge::SelectionCopyStatus::invalidated ? "invalidated" : "limit-exceeded") << "\",\"text\":";
            termforge::Screen::quoted(out,value.text);
            out << ",\"overlay_cells\":" << overlays << ",\"frame_matches_full\":" << (frame ? "true" : "false")
                << ",\"display_dpi\":" << shell->display_dpi() << ",\"zoom\":" << shell->zoom_percent()
                << ",\"ticks\":" << shell->observed_tick_count() << ",\"columns\":" << columns << ",\"rows\":" << rows
                << ",\"initial_columns\":" << initial_grids[index].first << ",\"initial_rows\":" << initial_grids[index].second
                << ",\"cell_width\":" << cell_width << ",\"cell_height\":" << cell_height << ",\"visible_lines\":[";
            for (size_t row = 0; row < view.lines.size(); ++row) {
                if (row) out << ','; termforge::Screen::quoted(out,line_text(view.lines[row]));
            }
            out << "],\"product_status\":"; termforge::Screen::quoted(out,utf8(shell->status_text().c_str())); out << '}';
        }
        out << "]}"; samples.push_back(out.str());
        save(directory / "selection-bytes-progress.jsonl",out.str() + "\n",true);
    };
    const auto launch = [&](ShellWindow* shell, const wchar_t* region) {
        wchar_t executable[32768]{};
        const auto count = GetModuleFileNameW(nullptr,executable,32768);
        require(count && count < 32768, "selection bytes producer location failed");
        const auto client = std::filesystem::path(executable).parent_path() / L"shell_selection_text_client.exe";
        const auto prefix = directory / (std::wstring(L"selection-bytes-") + region);
        shell->begin_output_observation();
        shell->command(L"\"" + client.wstring() + L"\" \"" + prefix.wstring() + L".bin\" \"" + prefix.wstring() + L"\" styled-tail-v1\r");
    };
    const auto three_ticks = [&]() {
        return main && phase_ticks.ready({bottom->observed_tick_count(),main->observed_tick_count()});
    };
    const auto settled = [&]() { return main && main->settled() && bottom->settled(); };
    if (stage_ == 0 && bottom->ready()) {
        samples.clear();
        if (system) tf_shell_test::require_hosted_clipboard();
        add(Group::main); stage_ = 1;
    } else if (stage_ == 1 && main && main->ready() && settled()) {
        transition(96); stage_ = 2;
    } else if (stage_ == 2 && settled()) {
        initial_grids = {bottom->grid_dimensions(),main->grid_dimensions()};
        launch(bottom,L"bottom"); launch(main,L"main"); stage_ = 3;
    } else if (stage_ == 3 && main && main->live_contains_line("TF_SELECT_BYTES_READY") &&
               bottom->live_contains_line("TF_SELECT_BYTES_READY") && settled()) {
        begin_phase(); stage_ = 4;
    } else if (stage_ == 4 && three_ticks() && settled()) {
        select(0,Group::main);
        const int soft = row_of(main,"SOFT_",true), continuation = row_of(main,"CONT 中文 e\xcc\x81"), hard = row_of(main,"HARD 中文 e\xcc\x81");
        require(continuation == soft + 1 && hard == soft + 2, "selection bytes ConPTY physical rows differ");
        drag(pixel(main,0,soft),pixel(main,13,hard)); copy_if_system(); stage_ = 5;
    } else if (stage_ == 5 && three_ticks()) {
        record("forward",system);
        drag(pixel(main,13,row_of(main,"HARD 中文 e\xcc\x81")),pixel(main,0,row_of(main,"SOFT_",true)));
        copy_if_system(); stage_ = 6;
    } else if (stage_ == 6 && three_ticks()) {
        record("reverse",system);
        const int hard = row_of(main,"HARD 中文 e\xcc\x81");
        drag(pixel(main,0,hard),pixel(main,13,hard)); copy_if_system(); stage_ = 7;
    } else if (stage_ == 7 && three_ticks()) {
        record("dpi-96",system); transition(144); stage_ = 8;
    } else if (stage_ == 8 && three_ticks() && settled()) {
        copy_if_system(); stage_ = 9;
    } else if (stage_ == 9 && three_ticks()) {
        record("dpi-144",system); transition(192); stage_ = 10;
    } else if (stage_ == 10 && three_ticks() && settled()) {
        copy_if_system(); stage_ = 11;
    } else if (stage_ == 11 && three_ticks()) {
        record("dpi-192",system); transition(96); stage_ = 12;
    } else if (stage_ == 12 && three_ticks() && settled()) {
        copy_if_system(); stage_ = 13;
    } else if (stage_ == 13 && three_ticks()) {
        record("dpi-96-restored",system); queued_key(VK_ESCAPE);
        begin_phase(); stage_ = 14;
    } else if (stage_ == 14 && three_ticks()) {
        record("cleared",false);
        if (system) sentinel();
        queued_key('C',true,true); begin_phase(); stage_ = 15;
    } else if (stage_ == 15 && three_ticks()) {
        record("empty-copy",false,true); queued_key('X');
        begin_phase(); stage_ = 16;
    } else if (stage_ == 16 && three_ticks() && !capture(L"main").empty()) {
        record("input-after",false);
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for (const auto group : {Group::bottom,Group::main}) {
            const RECT rect = physical(group == Group::main ? main_terminal_ : terminal_);
            selected(group)->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save(image_);
        std::ostringstream out;
        out << "{\"schema\":\"termforge-window-selection-bytes-v2\",\"fixture_style\":\"styled-tail-v1\",\"system_clipboard\":" << (system ? "true" : "false")
            << ",\"producer\":\"real-ConPTY-console-writefile\",\"input_source\":\"physical-pixel-drag-and-queued-workspace-keys\""
            << ",\"controlled_dpi_only\":true,\"transport_observation_limit\":8192,\"snapshots\":[";
        for (size_t index = 0; index < samples.size(); ++index) { if (index) out << ','; out << samples[index]; }
        out << "]}\n"; save(directory / "selection-bytes.json",out.str());
        stage_ = 100; close_all();
    }
}
