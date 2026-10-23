// Real numbered cmd output and HWND history controls on an owned test desktop.
#pragma once
#include "native_scroll_gesture.hpp"
inline void Workspace::scenario_history() {
    if(image_.empty() || closing_) return;
    // DefWindowProc's native scrollbar tracker dispatches nested timers.
    if(stage_==-1000) return;
    if(ms(started_)>=25000) throw std::runtime_error("history scenario timeout at stage "+std::to_string(stage_));
    auto* first=tabs_.front().shell.get();
    auto record=[&](const char* phase) {
        const auto state=first->history_state(); const auto& view=first->visible_snapshot();
        SCROLLINFO bar{sizeof(bar),SIF_RANGE|SIF_PAGE|SIF_POS};
        require(GetScrollInfo(first->handle(),SB_VERT,&bar)!=FALSE,"history scrollbar query failed");
        require(bar.nMin==0 && bar.nMax==static_cast<int>(state.total)-1 && bar.nPage==state.page &&
                bar.nPos==static_cast<int>(state.top),"scrollbar and viewport disagree");
        require(first->frame_matches_full(),"history retained frame differs from full redraw");
        std::ostringstream out;
        out<<"{\"phase\":\""<<phase<<"\",\"top\":"<<state.top<<",\"total\":"<<state.total
            <<",\"page\":"<<state.page<<",\"maximum\":"<<state.maximum_top<<",\"anchor\":"<<state.anchor
            <<",\"following\":"<<(state.following?"true":"false")<<",\"cursor_row\":"<<view.cursor_row
            <<",\"hidden\":"<<(IsWindowVisible(first->handle())?"false":"true")<<",\"lines\":[";
        for(size_t i=0;i<view.lines.size();++i) {
            if(i) out<<',';
            std::string text; for(const auto& cell:view.lines[i]) text+=cell.text;
            while(!text.empty() && text.back()==' ') text.pop_back();
            termforge::Screen::quoted(out,text);
        }
        out<<"],\"raw_output_tail_hex\":\"";
        if(std::string(phase)=="live-160" || std::string(phase)=="evicted-500") {
            static constexpr char hex[]="0123456789abcdef";
            for(unsigned char byte:first->observed_output()) out<<hex[byte>>4]<<hex[byte&15];
        }
        out<<"\"}"; geometry_samples_.push_back(out.str());
    };
    auto scroll=[&](UINT command) { SendMessageW(first->handle(),WM_VSCROLL,command,0); };
    auto position=[&](int index) {
        const int saved_stage=stage_; stage_=-1000;
        tf_shell::NativeScrollEvidence evidence;
        try { evidence=tf_shell::native_scroll_to(first->handle(),index); }
        catch(...) { stage_=saved_stage; throw; }
        stage_=saved_stage;
        require(first->history_state().top==static_cast<size_t>(index),"native thumb gesture did not move the text viewport");
        std::ofstream gestures(std::filesystem::path(image_).parent_path()/"native-scroll.jsonl",std::ios::app);
        gestures<<evidence.json()<<'\n'; require(bool(gestures),"native scrollbar evidence write failed");
    };
    if(stage_==0 && first->ready()) {
        first->begin_output_observation();
        first->command(L"for /L %i in (1,1,160) do @echo TF_H_%i\r"); stage_=1;
    } else if(stage_==1 && first->live_contains_line("TF_H_160") && first->live_contains_line("TF>")) {
        record("live-160"); scroll(SB_TOP); record("oldest-160");
        scroll(SB_PAGEDOWN); record("page-down");
        SendMessageW(first->handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),0); record("wheel-half");
        SendMessageW(first->handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),0); record("wheel-up");
        position(50); record("anchored-160"); history_anchor_=first->history_state().anchor;
        first->fixture_command(L"for /L %i in (161,1,170) do @echo TF_H_%i\r");
        add(); stage_=2;
    } else if(stage_==2 && active()->ready() && first->live_contains_line("TF_H_170") && first->live_contains_line("TF>")) {
        require(!IsWindowVisible(first->handle()),"history test did not hide the original tab");
        require(first->history_state().anchor==history_anchor_,"new output moved a retained reading anchor");
        record("hidden-after-append");
        active()->command(L"echo TF_H_OTHER\r"); add(Group::main); stage_=3;
    } else if(stage_==3 && active()->ready() && tabs_[1].shell->live_contains_line("TF_H_OTHER")) {
        active()->command(L"echo TF_H_MAIN\r"); stage_=4;
    } else if(stage_==4 && active()->live_contains_line("TF_H_MAIN")) {
        require(!first->live_contains_line("TF_H_OTHER") && !first->live_contains_line("TF_H_MAIN"),"history sessions leaked content");
        select(0); record("restored-tab"); scroll(SB_TOP);
        first->fixture_command(L"for /L %i in (171,1,500) do @echo TF_H_%i\r"); stage_=5;
    } else if(stage_==5 && first->live_contains_line("TF_H_500") && first->live_contains_line("TF>")) {
        require(!first->history_state().following && first->history_state().top==0,"evicted history did not clamp to oldest row");
        record("evicted-500"); scroll(SB_BOTTOM); record("bottom-500");
        position(50); history_anchor_=first->history_state().anchor; record("before-zoom");
        first->change_zoom(25); stage_=6;
    } else if(stage_==6 && first->settled()) {
        first->fixture_check_size(); stage_=7;
    } else if(stage_==7 && first->settled()) {
        const auto [columns,rows]=first->grid_dimensions();
        if(!first->live_contains_line("TF_SIZE_"+std::to_string(columns)+"x"+std::to_string(rows))) return;
        require(first->history_state().anchor==history_anchor_,"zoom moved a retained historical anchor");
        record("after-zoom"); scroll(SB_BOTTOM); record("bottom-after-zoom");
        scroll(SB_TOP); SendMessageW(first->handle(),WM_CHAR,'x',1); stage_=8;
    } else if(stage_==8 && first->live_contains_line("TF>x")) {
        record("typed-return-bottom");
        Bitmap frame(physical_width_,physical_height_); paint(frame.dc);
        for(const auto group:{Group::bottom,Group::main}) if(auto* shell=selected(group)) {
            const RECT rect=physical(group==Group::main?main_terminal_:terminal_);
            shell->render_to(frame.dc,rect.left,rect.top);
        }
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path()/"history.json");
        UINT wheel_lines=0; require(SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&wheel_lines,0)!=FALSE,"wheel setting query failed");
        out<<"{\"schema\":\"termforge-history-v2\",\"wheel_scroll_lines\":"<<wheel_lines<<",\"snapshots\":[";
        for(size_t i=0;i<geometry_samples_.size();++i) { if(i) out<<','; out<<geometry_samples_[i]; }
        out<<"]}\n"; require(bool(out),"history observations write failed");
        stage_=100; close_all();
    }
}
