// Real ConPTY fixture, native find EDIT and queued keys through the product loop.
#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
inline void Workspace::scenario_find() {
    if (image_.empty() || closing_) return;
    require(ms(started_) < 30000, ("find scenario timeout at stage " + std::to_string(stage_)).c_str());
    static std::array<BYTE, 256> saved_keys{};
    static bool restore_keys = false;
    static unsigned initial_running_checks = 0;
    if (restore_keys) { require(SetKeyboardState(saved_keys.data()) != FALSE, "find test keyboard state restore failed"); restore_keys = false; }
    auto* bottom = tabs_.front().shell.get(); auto* main = selected(Group::main);
    const auto directory = std::filesystem::path(image_).parent_path();
    const auto line_text = [](const auto& row) {
        std::string text; for (const auto& cell : row) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back(); return text;
    };
    const auto fixture_line = [](unsigned number) {
        std::ostringstream out; out << "TF_FIND_" << std::setw(3) << std::setfill('0') << number << " needle 中文 e\xcc\x81";
        return out.str();
    };
    const auto capture = [&](const char* name) {
        const auto path = directory / name;
        if (!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path) <= 4096, "find capture exceeded expected bounded diagnostic size");
        std::ifstream stream(path, std::ios::binary);
        require(bool(stream), "find capture read failed"); return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    const auto current_line = [&](ShellWindow* shell) {
        if (!shell) return std::string{};
        const auto range = shell->current_find_range(); if (!range) return std::string{};
        for (const auto& row : shell->visible_snapshot().lines)
            if (row.id == range->begin.line_id) return line_text(row);
        return std::string{};
    };
    const auto complete = [](ShellWindow* shell) { return shell && shell->search_status() == termforge::SearchStatus::complete; };
    const auto query = [](ShellWindow* shell, const wchar_t* value) {
        require(shell && shell->find_edit_handle() && SetWindowTextW(shell->find_edit_handle(), value),
                "find scenario native edit update failed");
    };
    const auto key = [](ShellWindow* shell, WPARAM value) {
        require(shell && shell->find_edit_handle(), "find key requires an actual edit HWND");
        require(PostMessageW(shell->find_edit_handle(), WM_KEYDOWN, value,
            1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(value), MAPVK_VK_TO_VSC)) << 16)),
            "find key post failed");
        require(PostMessageW(shell->find_edit_handle(), WM_KEYUP, value,
            1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(value), MAPVK_VK_TO_VSC)) << 16) |
            (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31)), "find key release post failed");
    };
    const auto record = [&](const char* phase) {
        std::ostringstream out; out << "{\"phase\":\"" << phase << "\",\"captures\":{\"bottom\":";
        termforge::Screen::quoted(out, capture("find-bottom.bin")); out << ",\"main\":";
        termforge::Screen::quoted(out, capture("find-main.bin")); out << "},\"sessions\":[";
        bool first = true;
        for (const auto group : {Group::bottom, Group::main}) {
            auto* shell = selected(group); require(shell != nullptr, "find scenario requires both regions");
            const auto range = shell->current_find_range(); const auto state = shell->history_state();
            const auto& view = shell->visible_snapshot(); size_t yellow = 0, blue = 0;
            for (const auto& row : view.lines) for (const auto& cell : row) {
                if ((cell.style.bg == 3 || cell.style.bg == 11) && cell.style.fg == 0) ++yellow;
                if (cell.style.bg == 4 && cell.style.fg == 15) ++blue;
            }
            require(shell->frame_matches_full(), "find dirty drawing differs from complete redraw");
            if (!first) out << ','; first = false;
            out << "{\"group\":\"" << (group == Group::bottom ? "bottom" : "main")
                << "\",\"workspace_active\":" << (active() == shell ? "true" : "false")
                << ",\"terminal_focused\":" << (GetFocus() == shell->handle() ? "true" : "false")
                << ",\"edit_focused\":" << (shell->find_edit_handle() && GetFocus() == shell->find_edit_handle() ? "true" : "false")
                << ",\"find_focused\":" << (shell->find_handle() &&
                    (GetFocus() == shell->find_handle() || IsChild(shell->find_handle(), GetFocus())) ? "true" : "false")
                << ",\"focus_control\":" << (shell->find_handle() && IsChild(shell->find_handle(), GetFocus()) ? GetDlgCtrlID(GetFocus()) : 0)
                << ",\"find_exists\":" << (shell->find_handle() ? "true" : "false")
                << ",\"find_visible\":" << (shell->find_handle() && IsWindowVisible(shell->find_handle()) ? "true" : "false")
                << ",\"case_sensitive\":" << (!shell->find_handle() || SendMessageW(GetDlgItem(shell->find_handle(), 4105), BM_GETCHECK, 0, 0) == BST_CHECKED ? "true" : "false")
                << ",\"query\":";
            if (shell->find_edit_handle()) {
                wchar_t value[4097]{}; GetWindowTextW(shell->find_edit_handle(), value, 4097); termforge::Screen::quoted(out, utf8(value));
            } else out << "null";
            out << ",\"status\":\"";
            switch (shell->search_status()) {
                case termforge::SearchStatus::idle: out << "idle"; break;
                case termforge::SearchStatus::running: out << "running"; break;
                case termforge::SearchStatus::complete: out << "complete"; break;
                case termforge::SearchStatus::capped: out << "capped"; break;
                case termforge::SearchStatus::cancelled: out << "cancelled"; break;
                case termforge::SearchStatus::invalidated: out << "invalidated"; break;
                case termforge::SearchStatus::closed: out << "closed"; break;
            }
            const auto selected = shell->selected_text();
            out << "\",\"selected_text\":"; termforge::Screen::quoted(out, selected.text);
            out << ",\"yellow_cells\":" << yellow << ",\"blue_cells\":" << blue
                << ",\"frame_matches_full\":true,\"top\":" << state.top << ",\"total\":" << state.total
                << ",\"page\":" << state.page << ",\"maximum\":" << state.maximum_top << ",\"anchor\":" << state.anchor
                << ",\"append_ready\":" << (shell->live_contains_line("TF_FIND_APPEND_READY 100") ? "true" : "false")
                << ",\"evict_ready\":" << (shell->live_contains_line("TF_FIND_EVICT_READY 500") ? "true" : "false")
                << ",\"current\":";
            if (range) {
                out << "{\"line_id\":" << range->begin.line_id << ",\"version\":" << range->begin.version
                    << ",\"begin\":" << range->begin.column << ",\"end_line_id\":" << range->end.line_id
                    << ",\"end_version\":" << range->end.version << ",\"end\":" << range->end.column << ",\"line\":";
                termforge::Screen::quoted(out, current_line(shell)); out << '}';
            } else out << "null";
            out << ",\"current_cells\":[";
            if (range) for (const auto& row : view.lines) if (row.id == range->begin.line_id) {
                for (size_t column = 0; column < row.size(); ++column) {
                    if (column) out << ',';
                    out << "{\"column\":" << column << ",\"bg\":" << row[column].style.bg << ",\"fg\":" << row[column].style.fg << '}';
                }
                break;
            }
            out << "],\"visible_lines\":[";
            for (size_t row = 0; row < view.lines.size(); ++row) {
                if (row) out << ','; out << "{\"id\":" << view.lines[row].id << ",\"version\":" << view.lines[row].version << ",\"text\":";
                termforge::Screen::quoted(out, line_text(view.lines[row])); out << '}';
            }
            out << "]}";
        }
        out << "]}"; geometry_samples_.push_back(out.str());
        std::ofstream stages(directory / "find-stages.jsonl", std::ios::app);
        stages << out.str() << '\n'; stages.close(); require(stages.good(), "find stage observations write failed");
    };
    if (stage_ == 3 && bottom->search_status() == termforge::SearchStatus::running) {
        ++initial_running_checks;
        require(!bottom->current_find_range(), "partial search results were incorrectly published as the final current match");
    }
    if (stage_ == 0 && bottom->ready()) { add(Group::main); stage_ = 1; }
    else if (stage_ == 1 && main && main->ready() && bottom->settled() && main->settled()) {
        bottom->start_text_receiver(directory / L"find-bottom.bin"); bottom->set_search_work_budget(64);
        main->start_text_receiver(directory / L"find-main.bin"); stage_ = 2;
    } else if (stage_ == 2 && bottom->live_contains_line("TF_FIND_READY 080") && main->live_contains_line("TF_FIND_READY 080")) {
        select(0, Group::bottom); SendMessageW(window_, WM_COMMAND, 16, 0); query(bottom, L"needle"); stage_ = 3;
    } else if (stage_ == 3 && complete(bottom) && current_line(bottom) == fixture_line(79)) {
        require(initial_running_checks > 0, "initial query did not exercise an observed multibatch search");
        bottom->set_search_work_budget(4096);
        record("initial-last");
        const auto& view = bottom->visible_snapshot(); int row_index = -1;
        for (size_t row = 0; row < view.lines.size(); ++row) if (line_text(view.lines[row]) == fixture_line(79)) row_index = static_cast<int>(row);
        require(row_index >= 0, "last fixture match is not actually visible");
        const auto [width, height] = bottom->pixel_cell_size();
        SendMessageW(bottom->handle(), WM_LBUTTONDOWN, 0, MAKELPARAM(width / 4, row_index * height + height / 2));
        SendMessageW(bottom->handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(11 * width + width / 4, row_index * height + height / 2));
        SendMessageW(bottom->handle(), WM_LBUTTONUP, 0, MAKELPARAM(11 * width + width / 4, row_index * height + height / 2));
        bottom->request_find(); record("selection-coexists"); key(bottom, VK_RETURN); stage_ = 4;
    } else if (stage_ == 4 && current_line(bottom) == fixture_line(78)) {
        record("previous-78"); key(bottom, VK_F3); stage_ = 5;
    } else if (stage_ == 5 && current_line(bottom) == fixture_line(79)) {
        record("next-79"); key(bottom, VK_F3); stage_ = 6;
    } else if (stage_ == 6 && current_line(bottom) == fixture_line(0)) {
        record("wrap-next-0"); require(GetKeyboardState(saved_keys.data()), "find Shift state read failed");
        auto shifted = saved_keys; shifted[VK_SHIFT] |= 0x80;
        require(SetKeyboardState(shifted.data()), "find Shift state setup failed"); restore_keys = true;
        key(bottom, VK_F3); stage_ = 7;
    } else if (stage_ == 7 && current_line(bottom) == fixture_line(79)) {
        record("wrap-previous-79"); query(bottom, L"NEEDLE"); stage_ = 8;
    } else if (stage_ == 8 && complete(bottom) && !bottom->current_find_range()) {
        record("case-sensitive-none"); SendMessageW(GetDlgItem(bottom->find_handle(), 4105), BM_CLICK, 0, 0); stage_ = 9;
    } else if (stage_ == 9 && complete(bottom) && current_line(bottom) == fixture_line(79)) {
        record("case-fold-last"); bottom->request_find(); query(bottom, L"needle"); query(bottom, L"中文"); stage_ = 10;
    } else if (stage_ == 10 && complete(bottom) && current_line(bottom) == fixture_line(79) && bottom->current_find_range()->begin.column == 19) {
        record("unicode-replacement"); query(bottom, L"TF_FIND_MISSING"); stage_ = 11;
    } else if (stage_ == 11 && complete(bottom) && !bottom->current_find_range()) {
        record("no-result"); query(bottom, L"needle"); stage_ = 12;
    } else if (stage_ == 12 && complete(bottom) && current_line(bottom) == fixture_line(79)) {
        select(0, Group::main); SendMessageW(window_, WM_COMMAND, 16, 0); query(main, L"TF_FIND_005"); stage_ = 13;
    } else if (stage_ == 13 && complete(main) && current_line(main) == fixture_line(5)) {
        // Finding the last row can legitimately leave the view following at
        // the bottom. Explicitly pause one row before the background-append
        // check; this tests retained reading rather than inventing a new rule.
        SendMessageW(bottom->handle(), WM_VSCROLL, SB_LINEUP, 0);
        require(!bottom->history_state().following && current_line(bottom) == fixture_line(79),
                "background append fixture did not establish a visible paused match");
        record("main-active"); bottom->fixture_command(L"+"); stage_ = 14;
    } else if (stage_ == 14 && complete(bottom) && bottom->live_contains_line("TF_FIND_APPEND_READY 100") && current_line(main) == fixture_line(5)) {
        record("background-append"); bottom->fixture_command(L"#"); stage_ = 15;
    } else if (stage_ == 15 && complete(bottom) && bottom->live_contains_line("TF_FIND_EVICT_READY 500") && !bottom->current_find_range()) {
        record("evicted"); key(main, VK_ESCAPE); stage_ = 16;
    } else if (stage_ == 16 && !main->find_handle() && GetFocus() == main->handle()) {
        record("escape-before-input"); require(PostMessageW(main->handle(), WM_CHAR, L'x', 1), "post-search terminal input failed"); stage_ = 17;
    } else if (stage_ == 17 && capture("find-main.bin") == "x") {
        record("input-after-escape");
        Bitmap frame(physical_width_, physical_height_); paint(frame.dc);
        for (const auto group : {Group::bottom, Group::main}) {
            const RECT rect = physical(group == Group::main ? main_terminal_ : terminal_);
            selected(group)->render_to(frame.dc, rect.left, rect.top);
        }
        frame.save(image_);
        std::ofstream out(directory / "find.json");
        out << "{\"schema\":\"termforge-window-find-v1\",\"input_path\":\"queued-WM_KEYDOWN-through-workspace-loop\","
               "\"fixture\":\"shell_text_client-v1-80-plus20-evict500\",\"initial_running_checks\":" << initial_running_checks << ",\"snapshots\":[";
        for (size_t i = 0; i < geometry_samples_.size(); ++i) { if (i) out << ','; out << geometry_samples_[i]; }
        out << "]}\n"; out.close(); require(out.good(), "find observations write failed"); stage_ = 100; close_all();
    }
}

// Separate lifecycle/continuous-output gate. The original 15-phase fixture and
// its exact captures remain unchanged. This explicitly exercises partial UI
// policy, not a claim about unobserved WindTerm flood-search semantics.
inline void Workspace::scenario_find_stream() {
    if(image_.empty() || closing_) return;
    require(ms(started_)<30000,("find stream timeout at stage "+std::to_string(stage_)).c_str());
    auto* bottom=tabs_.empty()?nullptr:tabs_.front().shell.get(); auto* main=selected(Group::main);
    const auto directory=std::filesystem::path(image_).parent_path();
    static std::array<BYTE,256> saved_keys{}; static bool restore_keys=false, cancel_baseline=false;
    static uint64_t cancelled_tail_id=0; static HWND closing_find=nullptr, closing_terminal=nullptr;
    if(restore_keys) { require(SetKeyboardState(saved_keys.data()),"stream keyboard state restore failed"); restore_keys=false; }
    const auto text=[](const auto& row) { std::string result; for(const auto& cell:row) result+=cell.text;
        while(!result.empty() && result.back()==' ') result.pop_back(); return result; };
    const auto capture=[&](const char* name) { const auto path=directory/name;
        if(!std::filesystem::exists(path)) return std::string{};
        require(std::filesystem::file_size(path)<=4096,"stream raw capture exceeded diagnostic bound");
        std::ifstream in(path,std::ios::binary); require(bool(in),"stream capture read failed");
        return std::string(std::istreambuf_iterator<char>(in),{}); };
    const auto current_line=[&](ShellWindow* shell) { if(!shell) return std::string{};
        const auto range=shell->current_find_range(); if(!range) return std::string{};
        for(const auto& row:shell->visible_snapshot().lines) if(row.id==range->begin.line_id) return text(row);
        return std::string{}; };
    const auto label=[](ShellWindow* shell) { if(!shell || !shell->find_handle()) return std::string{};
        for(HWND child=GetWindow(shell->find_handle(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            wchar_t name[32]{}; GetClassNameW(child,name,32);
            if(_wcsicmp(name,L"Static")==0) { wchar_t value[512]{}; GetWindowTextW(child,value,512); return utf8(value); }
        } return std::string{}; };
    const auto running=[](ShellWindow* shell) { return shell && shell->search_status()==termforge::SearchStatus::running; };
    const auto query=[](ShellWindow* shell,const wchar_t* value) {
        require(shell && shell->find_edit_handle() && SetWindowTextW(shell->find_edit_handle(),value),"stream query update failed"); };
    const auto key=[](ShellWindow* shell,WPARAM value) {
        require(shell && shell->find_edit_handle(),"stream key requires native find edit");
        require(PostMessageW(shell->find_edit_handle(),WM_KEYDOWN,value,1),"stream key post failed");
        require(PostMessageW(shell->find_edit_handle(),WM_KEYUP,value,static_cast<LPARAM>(0xc0000001u)),"stream key release failed"); };
    const auto record=[&](const char* phase) {
        std::ostringstream out; out<<"{\"phase\":\""<<phase<<"\",\"captures\":{\"bottom\":";
        termforge::Screen::quoted(out,capture("stream-bottom.bin")); out<<",\"main\":";
        termforge::Screen::quoted(out,capture("stream-main.bin"));
        out<<"},\"closed_find_hwnd_gone\":"<<(closing_find && !IsWindow(closing_find)?"true":"false")
           <<",\"closed_terminal_hwnd_gone\":"<<(closing_terminal && !IsWindow(closing_terminal)?"true":"false")<<",\"sessions\":[";
        bool first=true;
        for(const auto group:{Group::bottom,Group::main}) {
            auto* shell=group==Group::bottom?bottom:main;
            if(!first) out<<','; first=false;
            out<<"{\"group\":\""<<(group==Group::bottom?"bottom":"main")<<"\",\"exists\":"<<(shell?"true":"false");
            if(!shell) { out<<'}'; continue; }
            require(shell->frame_matches_full(),"stream retained frame differs from full redraw");
            const auto range=shell->current_find_range(); const auto& view=shell->visible_snapshot();
            size_t needle_cells=0,chinese_cells=0;
            for(const auto& row:view.lines) if(text(row).rfind("TF_FIND_",0)==0) for(size_t c=0;c<row.size();++c)
                if((row[c].style.bg==3 || row[c].style.bg==11) && row[c].style.fg==0) {
                    if(c>=12 && c<18) ++needle_cells;
                    if(c>=19 && c<23) ++chinese_cells;
                }
            out<<",\"find_exists\":"<<(shell->find_handle()?"true":"false")
               <<",\"edit_focused\":"<<(shell->find_edit_handle() && GetFocus()==shell->find_edit_handle()?"true":"false")
               <<",\"terminal_focused\":"<<(GetFocus()==shell->handle()?"true":"false")
               <<",\"workspace_active\":"<<(active()==shell?"true":"false")
               <<",\"frame_matches_full\":true,\"running\":"<<(running(shell)?"true":"false")
               <<",\"cancelled\":"<<(shell->search_status()==termforge::SearchStatus::cancelled?"true":"false")
               <<",\"needle_highlight_cells\":"<<needle_cells<<",\"chinese_highlight_cells\":"<<chinese_cells
               <<",\"source_tick_seen\":"<<(shell->observed_output().find("TF_STREAM_TICK_")!=std::string::npos?"true":"false")
               <<",\"status_label\":"; termforge::Screen::quoted(out,label(shell)); out<<",\"query\":";
            if(shell->find_edit_handle()) { wchar_t value[4097]{}; GetWindowTextW(shell->find_edit_handle(),value,4097); termforge::Screen::quoted(out,utf8(value)); }
            else out<<"null";
            out<<",\"current\":";
            if(range) { out<<"{\"line_id\":"<<range->begin.line_id<<",\"version\":"<<range->begin.version
                <<",\"begin\":"<<range->begin.column<<",\"end_line_id\":"<<range->end.line_id
                <<",\"end_version\":"<<range->end.version<<",\"end\":"<<range->end.column<<",\"line\":";
                termforge::Screen::quoted(out,current_line(shell)); out<<'}'; }
            else out<<"null";
            out<<",\"tail\":{\"id\":"<<view.lines.back().id<<",\"version\":"<<view.lines.back().version<<",\"text\":";
            termforge::Screen::quoted(out,text(view.lines.back())); out<<"},\"lines\":[";
            for(size_t i=0;i<view.lines.size();++i) { if(i) out<<','; out<<"{\"id\":"<<view.lines[i].id<<",\"version\":"<<view.lines[i].version<<",\"text\":";
                termforge::Screen::quoted(out,text(view.lines[i])); out<<'}'; }
            out<<"]}";
        }
        out<<"]}"; geometry_samples_.push_back(out.str());
        std::ofstream stages(directory/"find-stream-stages.jsonl",std::ios::app); stages<<out.str()<<'\n';
        stages.close(); require(stages.good(),"stream observations write failed");
    };
    if(stage_==0 && bottom && bottom->ready()) { add(Group::main); stage_=1; }
    else if(stage_==1 && main && main->ready() && bottom && bottom->settled() && main->settled()) {
        bottom->start_text_receiver(directory/L"stream-bottom.bin"); main->start_text_receiver(directory/L"stream-main.bin");
        bottom->begin_output_observation(); bottom->set_search_work_budget(64); stage_=2;
    } else if(stage_==2 && bottom->live_contains_line("TF_FIND_READY 080") && main->live_contains_line("TF_FIND_READY 080")) {
        select(0,Group::bottom); SendMessageW(window_,WM_COMMAND,16,0); query(bottom,L"needle"); bottom->fixture_command(L"~"); stage_=3;
    } else if(stage_==3 && running(bottom) && !bottom->current_find_range() && capture("stream-bottom.bin")=="~" &&
              bottom->observed_output().find("TF_STREAM_TICK_")!=std::string::npos) {
        record("running-before-navigation"); key(bottom,VK_F3); stage_=4;
    } else if(stage_==4 && running(bottom) && current_line(bottom)=="TF_FIND_000 needle 中文 e\xcc\x81") {
        require(label(bottom).find("partial")!=std::string::npos,"running navigation did not mark partial scope");
        record("partial-next-0"); require(GetKeyboardState(saved_keys.data()),"stream Shift state read failed");
        auto shifted=saved_keys; shifted[VK_SHIFT]|=0x80; require(SetKeyboardState(shifted.data()),"stream Shift setup failed");
        restore_keys=true; key(bottom,VK_F3); stage_=5;
    } else if(stage_==5 && running(bottom) && label(bottom).find("no known")!=std::string::npos &&
              current_line(bottom)=="TF_FIND_000 needle 中文 e\xcc\x81") {
        record("partial-no-wrap"); key(bottom,VK_F3); stage_=6;
    } else if(stage_==6 && running(bottom) && current_line(bottom)=="TF_FIND_001 needle 中文 e\xcc\x81") {
        record("partial-next-1"); query(bottom,L"中文"); stage_=7;
    } else if(stage_==7 && running(bottom) && !bottom->current_find_range()) {
        record("replaced-running-empty"); key(bottom,VK_F3); stage_=8;
    } else if(stage_==8 && running(bottom) && current_line(bottom)=="TF_FIND_000 needle 中文 e\xcc\x81" &&
              bottom->current_find_range()->begin.column==19) {
        record("partial-chinese-0"); key(bottom,VK_ESCAPE); stage_=9;
    } else if(stage_==9 && !bottom->find_handle() && GetFocus()==bottom->handle()) {
        require(!bottom->current_find_range(),"cancel left a current result");
        require(PostMessageW(bottom->handle(),WM_CHAR,L'y',1),"cancelled stream terminal input post failed"); stage_=10;
    } else if(stage_==10 && capture("stream-bottom.bin")=="~y" &&
              text(bottom->visible_snapshot().lines.back()).find("TF_STREAM_TICK_")==0) {
        const auto& tail=bottom->visible_snapshot().lines.back();
        if(!cancel_baseline) { cancelled_tail_id=tail.id; history_anchor_=tail.version; cancel_baseline=true; }
        else if(tail.id==cancelled_tail_id && tail.version>history_anchor_) {
            record("cancelled-output-continues"); bottom->fixture_command(L"-"); stage_=11;
        }
    } else if(stage_==11 && bottom->live_contains_line("TF_STREAM_STOPPED")) {
        record("explicitly-stopped"); bottom->request_find(); query(bottom,L"needle"); bottom->fixture_command(L"~"); stage_=12;
    } else if(stage_==12 && running(bottom) && !bottom->current_find_range() && capture("stream-bottom.bin")=="~y-~" &&
              bottom->visible_snapshot().lines.back().id>cancelled_tail_id &&
              text(bottom->visible_snapshot().lines.back()).find("TF_STREAM_TICK_")==0) {
        record("target-closing-running"); closing_find=bottom->find_handle(); closing_terminal=bottom->handle(); bottom->close(); stage_=13;
    } else if(stage_==13 && !bottom && main && !main->closing()) {
        require(!IsWindow(closing_find) && !IsWindow(closing_terminal),"query target HWND survived session closure");
        select(0,Group::main); require(PostMessageW(main->handle(),WM_CHAR,L'x',1),"other terminal input after close failed"); stage_=14;
    } else if(stage_==14 && capture("stream-main.bin")=="x") {
        record("target-closed-other-input"); Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        const auto rect=physical(main_terminal_); main->render_to(frame.dc,rect.left,rect.top); frame.save(image_);
        std::ofstream out(directory/"find-stream.json");
        out<<"{\"schema\":\"termforge-window-find-stream-v1\",\"input_path\":\"queued-WM_KEYDOWN-through-workspace-loop\","
             "\"search_work_budget\":64,\"worker_deadline_ms\":5000,\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"]}\n"; out.close(); require(out.good(),"stream manifest write failed"); stage_=100; close_all();
    }
}
