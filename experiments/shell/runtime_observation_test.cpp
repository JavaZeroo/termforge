// Public ShellWindow lifecycle on the existing owned non-input desktop fixture.
// Only the executable entry is renamed; no private state is made accessible.
#define WinMain included_shell_entry
#include "shell.cpp"
#undef WinMain
#include "ime_desktop_process.hpp"

template<class Predicate> static void runtime_wait(ShellWindow& window, Predicate predicate, const char* detail) {
    const auto deadline = GetTickCount64() + 5000;
    while (!predicate()) {
        require(GetTickCount64() < deadline, detail);
        MSG message{};
        for (int count = 0; count < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            if (!window.route_find_message(message) && !window.route_paste_message(message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
}
static int runtime_window_case() {
    ShellWindow window("", "");
    require(!window.operation_observation_enabled(), "operation observation defaults off");
    window.enable_operation_observation();
    require(window.operation_observations().empty(), "enabling is not receiving an operation");
    require(!window.runtime_observation_enabled(), "runtime observation defaults off");
    bool rejected = false;
    try { window.runtime_snapshot(); } catch (const std::exception&) { rejected = true; }
    require(rejected && !window.runtime_before_close() && !window.runtime_final(), "disabled observation has no samples");
    window.enable_runtime_observation();
    bool wrong_thread_snapshot = false, wrong_thread_enable = false;
    std::thread other([&] {
        try { window.runtime_snapshot(); } catch (const std::exception&) { wrong_thread_snapshot = true; }
        try { window.enable_runtime_observation(); } catch (const std::exception&) { wrong_thread_enable = true; }
    });
    other.join();
    require(wrong_thread_snapshot && wrong_thread_enable, "runtime access requires owning UI thread");
    const auto initial = window.runtime_snapshot();
    require(initial.rows == 24 && initial.columns == 80 && initial.history_limit == 100 && !initial.history_rows &&
        !initial.started && !initial.window_exists && !initial.final_session && !initial.input.started && !initial.resize.started,
        "pre-create observation reflects real unstarted state");
    const auto creations = ShellWindow::successful_window_creations();
    window.create(); ShowWindow(window.handle(), SW_SHOWNOACTIVATE);
    runtime_wait(window, [&] { return window.ready() && window.settled(); }, "runtime actual shell ready deadline");
    const auto live = window.runtime_snapshot();
    require(live.session_id != 0 && live.window_creation_serial == creations + 1 &&
        ShellWindow::successful_window_creations() == creations + 1 && live.started && live.visible && live.window_exists &&
        live.input.started && live.resize.started && !live.input.joined && !live.resize.joined && !live.closing &&
        !live.final_after_join && !live.final_session && live.published_revision > 0 &&
        live.capture_begin_ns <= live.capture_end_ns && !live.selection.copied_bytes &&
        !live.selection.retained_capacity_bytes && !live.history_capacity_bytes && !live.search.actual_work_units,
        "live snapshot is bounded and keeps unobserved fields unknown");
    window.command(L"echo TF_RUNTIME_OBSERVED\r");
    runtime_wait(window, [&] { return window.contains_line("TF_RUNTIME_OBSERVED"); }, "runtime literal command output deadline");
    const auto after = window.runtime_snapshot();
    require(after.published_revision > live.published_revision && after.consumed_bytes > live.consumed_bytes &&
        after.capture_begin_ns >= live.capture_end_ns && after.session_id == live.session_id &&
        live.published_revision > initial.published_revision && !initial.window_exists,
        "independent live values survive subsequent UI output");
    window.synthetic_paste(u"RUNTIME_SENT");
    runtime_wait(window, [&] { return !window.paste_busy(); }, "runtime synthetic transfer completes deadline");
    const auto sent = window.runtime_snapshot();
    require(!sent.paste && sent.last_terminal_paste && sent.last_terminal_paste->state == tf_shell::PasteState::completed &&
        sent.last_terminal_paste->source_units == size_t{12} && sent.last_terminal_paste->total_bytes == 12 &&
        sent.last_terminal_paste->sent_bytes == 12 && !sent.last_terminal_paste->in_flight_bytes &&
        sent.last_terminal_paste->retained_capacity_bytes <= 32,
        "terminal transfer counters survive product shared_ptr reset");
    window.select_all_text(); window.request_find();
    require(SetWindowTextW(window.find_edit_handle(), L"TF_RUNTIME_OBSERVED") != FALSE, "runtime find literal query setup");
    runtime_wait(window, [&] { return window.search_status() == termforge::SearchStatus::complete &&
        window.current_find_range().has_value(); }, "runtime actual find finishes deadline");
    window.synthetic_paste(u"TF_RUNTIME_PROMPT\r\nNEXT");
    const auto tasks = window.runtime_snapshot();
    const bool tasks_valid = tasks.selection.range && tasks.search.editor_present && tasks.search.result_count >= 1 &&
        tasks.search.find_query_bytes == 19 && tasks.pending_prepared_paste && tasks.paste_prompt_present &&
        tasks.pending_prepared_bytes == 22 && tasks.pending_source_units == size_t{23} && !tasks.paste &&
        tasks.pending_prepared_capacity_bytes >= tasks.pending_prepared_bytes;
    if (!tasks_valid) std::cerr << "runtime_tasks=" << tf_shell::runtime_snapshot_json(tasks) << '\n';
    require(tasks_valid,
        "actual search selection and unsent confirmation payload are observed without copying text");
    window.close();
    const auto before_close = window.runtime_before_close();
    require(before_close && !before_close->closing && before_close->session_id == live.session_id &&
        !before_close->final_session && before_close->selection.range && before_close->search.editor_present &&
        before_close->pending_prepared_paste && before_close->pending_prepared_bytes == 22 &&
        before_close->pending_source_units == size_t{23}, "pre-close is captured before cancellation and cleanup");
    runtime_wait(window, [&] { return IsWindow(window.handle()) == FALSE; }, "runtime real window closes deadline");
    require(!window.runtime_snapshot().final_session, "closed window alone does not establish joined session evidence");
    const auto result = window.collect();
    const auto final = window.runtime_final();
    require(result.runtime_before_close && result.runtime_final && final && final->closing && final->final_after_join &&
        final->final_session && final->final_session->cleanup_complete && final->final_session->active_children == 0u &&
        final->final_session->child_signaled && final->input.joined && final->resize.joined &&
        !final->input.queued_bytes && !final->input.in_flight_bytes && !final->window_exists && !final->visible &&
        final->search.state == termforge::SearchStatus::closed && !final->search.editor_present &&
        !final->pending_prepared_paste && !final->paste_prompt_present && !final->pending_prepared_capacity_bytes &&
        final->last_terminal_paste && final->last_terminal_paste->sent_bytes == 12 &&
        window.runtime_before_close()->capture_begin_ns == before_close->capture_begin_ns,
        "collect returns retained pre-close and actual post-join observations");
    for(const auto& checkpoint : {std::pair<const char*,tf_shell::RuntimeSnapshot>{"live",live},
        {"tasks",tasks},{"before-close",*before_close},{"final",*final}})
        std::cout << "{\"checkpoint\":\"" << checkpoint.first << "\",\"snapshot\":"
                  << tf_shell::runtime_snapshot_json(checkpoint.second) << "}\n";
    std::cout << "{\"runtime_snapshot\":\"public-window-lifecycle\",\"default_off\":true,\"thread_guard\":true,"
                 "\"window_creations\":1,\"preclose_preserved\":true,\"session_job_active_after\":0,\"final_after_join\":true}\n";
    return 0;
}
static void runtime_default_off_case() {
    ShellWindow window("", ""); window.create();
    runtime_wait(window, [&] { return window.ready(); }, "unobserved real shell ready deadline");
    require(!window.runtime_observation_enabled(), "normal live window keeps observation disabled");
    require(!window.operation_observation_enabled() && window.operation_observations().empty(),
        "normal live window keeps operation observation disabled");
    window.close();
    runtime_wait(window, [&] { return IsWindow(window.handle()) == FALSE; }, "unobserved real shell closes deadline");
    const auto result = window.collect();
    require(result.complete && !result.runtime_before_close && !result.runtime_final &&
        !window.runtime_before_close() && !window.runtime_final(), "normal lifecycle creates no runtime samples");
}
static void operation_state_case() {
    using R=tf_shell::OperationReason;
    tf_shell::ScrollOperationObservations values;
    values.enable(1000);
    values.arm(1,11,21,31,SB_LINEUP);
    require(!values.values()[0].received_qpc,"arming alone has no received endpoint");
    require(!values.receive(SB_LINEDOWN,9),"unrelated native command does not receive armed operation");
    const auto token=values.receive(SB_LINEUP,10);
    require(token.has_value(),"expected native command receives operation");
    values.state(*token,7,20,true,true);
    values.paint(8,30,true,true,true);
    values.paint(7,30,true,false,true);
    require(!values.values()[0].submitted_qpc && values.values()[0].reason==R::awaiting_paint,
        "wrong revision or empty paint cannot submit");
    values.paint(7,30,true,true,false);
    values.paint(7,40,true,true,true);
    require(values.values()[0].reason==R::paint_failed && !values.values()[0].submitted_qpc,
        "failed GDI submission stays null and later unrelated repaint cannot repair it");
    values.arm(2,11,21,32,SB_TOP);
    values.state(*values.receive(SB_TOP,50),8,60,false,true);
    require(values.values()[1].reason==R::no_state_change && !values.values()[1].state_qpc &&
        !values.values()[1].published_revision,"no changed viewport has no state endpoint");
    values.arm(3,11,21,33,SB_BOTTOM);
    values.state(*values.receive(SB_BOTTOM,70),9,80,true,true); values.published(10);
    values.paint(10,90,true,true,true);
    require(values.values()[2].reason==R::superseded && !values.values()[2].submitted_qpc,
        "newer published revision cannot be counted as operation submission");
    for(uint64_t id=4;id<=8;++id) values.arm(id,11,21,30+id,SB_TOP);
    bool capacity=false,duplicate=false;
    try { values.arm(9,11,21,39,SB_TOP); } catch(const std::length_error&) { capacity=true; }
    tf_shell::ScrollOperationObservations unique; unique.enable(1000); unique.arm(1,11,21,31,SB_TOP);
    try { unique.arm(1,11,21,32,SB_TOP); } catch(const std::invalid_argument&) { duplicate=true; }
    require(capacity && duplicate && values.values().size()==8,"bounded capacity and duplicate IDs reject explicitly");
}
static std::string operation_line(const termforge::Line& line) {
    std::string text; for(const auto& cell:line) text+=cell.text;
    while(!text.empty() && text.back()==' ') text.pop_back();
    return text;
}
static void operation_window_case() {
    using R=tf_shell::OperationReason;
    ShellWindow window("",""); window.enable_runtime_observation(); window.enable_operation_observation();
    window.create(); ShowWindow(window.handle(),SW_SHOWNOACTIVATE);
    runtime_wait(window,[&] { return window.ready() && window.settled(); },"operation shell ready deadline");
    window.command(L"cls&for /L %i in (100,1,160) do @echo TF_OP_%i\r");
    runtime_wait(window,[&] { return window.live_contains_line("TF_OP_160"); },"operation literal output deadline");
    // Allow the final prompt to arrive; require three real stable UI ticks.
    auto bytes=window.runtime_snapshot().consumed_bytes; int quiet=0, tick=window.observed_tick_count();
    runtime_wait(window,[&] {
        if(window.observed_tick_count()==tick) return false;
        tick=window.observed_tick_count(); const auto next=window.runtime_snapshot().consumed_bytes;
        quiet=next==bytes ? quiet+1 : 0; bytes=next; return quiet>=3 && window.settled();
    },"operation output quiet deadline");
    UpdateWindow(window.handle());
    const auto identity=window.runtime_snapshot();
    bool wrong_arm=false,wrong_read=false;
    std::thread wrong([&] {
        try { window.arm_scroll_observation(99,1,SB_LINEUP); } catch(const std::exception&) { wrong_arm=true; }
        try { window.operation_observations(); } catch(const std::exception&) { wrong_read=true; }
    }); wrong.join(); require(wrong_arm && wrong_read,"operation interfaces require UI owner thread");
    window.arm_scroll_observation(1,101,SB_TOP);
    require(!window.operation_observations()[0].received_qpc,"real arm is not native receipt");
    require(PostMessageW(window.handle(),WM_VSCROLL,SB_TOP,0)!=FALSE,"owned scroll post failed");
    runtime_wait(window,[&] { return window.operation_observations()[0].submitted_qpc.has_value(); },
        "actual WM_PAINT scroll submission deadline");
    const auto success=window.operation_observations()[0]; const auto view=window.visible_snapshot();
    require(success.reason==R::submitted && success.session_id==identity.session_id &&
        success.window_creation_serial==identity.window_creation_serial && success.generation==101 &&
        success.received_qpc && success.state_qpc && *success.received_qpc<=*success.state_qpc &&
        *success.state_qpc<=*success.submitted_qpc && success.qpc_frequency>0 && !success.presented_done &&
        success.published_revision==view.revision && window.history_state().top==0 && identity.viewport->top>0,
        "actual handler state and exact revision submit bind target identity and ordered QPC");
    for(size_t row=0;row<view.lines.size();++row) {
        const auto expected="TF_OP_"+std::to_string(100+row);
        if(operation_line(view.lines[row])!=expected)
            std::cerr<<"operation-row="<<row<<" expected="<<expected<<" actual="<<operation_line(view.lines[row])
                     <<" top="<<window.history_state().top<<" page="<<view.lines.size()<<'\n';
        require(operation_line(view.lines[row])==expected,"scroll visible numbered rows differ from independent literal fixture");
    }
    require(window.frame_matches_full(),"actual scroll retained pixels match independent full redraw");
    SendMessageW(window.handle(),WM_VSCROLL,SB_BOTTOM,0); UpdateWindow(window.handle());
    window.arm_scroll_observation(2,102,SB_BOTTOM); SendMessageW(window.handle(),WM_VSCROLL,SB_BOTTOM,0);
    require(window.operation_observations()[1].reason==R::no_state_change &&
        !window.operation_observations()[1].state_qpc,"actual no-op scroll has no state endpoint");
    window.arm_scroll_observation(3,103,SB_LINEUP); SendMessageW(window.handle(),WM_VSCROLL,SB_LINEUP,0);
    require(window.operation_observations()[2].state_qpc && !window.operation_observations()[2].submitted_qpc,
        "actual state before paint retains null submitted endpoint");
    Bitmap offscreen(1600,1000); window.render_to(offscreen.dc,0,0);
    SendMessageW(window.handle(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(offscreen.dc),PRF_CLIENT);
    SendMessageW(window.handle(),WM_PRINT,reinterpret_cast<WPARAM>(offscreen.dc),PRF_CLIENT|PRF_CHILDREN);
    require(!window.operation_observations()[2].submitted_qpc,"render_to and print cannot submit operation");
    ShowWindow(window.handle(),SW_HIDE); ShowWindow(window.handle(),SW_SHOWNOACTIVATE); UpdateWindow(window.handle());
    require(window.operation_observations()[2].reason==R::hidden && !window.operation_observations()[2].submitted_qpc,
        "hidden operation cannot claim later show repaint");
    window.arm_scroll_observation(4,104,SB_LINEUP); SendMessageW(window.handle(),WM_VSCROLL,SB_LINEUP,0);
    SendMessageW(window.handle(),WM_VSCROLL,SB_BOTTOM,0); UpdateWindow(window.handle());
    require(window.operation_observations()[3].reason==R::superseded && !window.operation_observations()[3].submitted_qpc,
        "actual newer viewport publication supersedes before paint");
    window.arm_scroll_observation(5,105,SB_LINEUP); window.arm_scroll_observation(6,106,SB_BOTTOM);
    SendMessageW(window.handle(),WM_VSCROLL,SB_BOTTOM,0);
    require(window.operation_observations()[4].reason==R::superseded && !window.operation_observations()[4].received_qpc,
        "overlap explicitly supersedes the old armed operation");
    ShowWindow(window.handle(),SW_HIDE); window.arm_scroll_observation(7,107,SB_LINEUP);
    SendMessageW(window.handle(),WM_VSCROLL,SB_LINEUP,0);
    ShowWindow(window.handle(),SW_SHOWNOACTIVATE); UpdateWindow(window.handle());
    require(window.operation_observations()[6].reason==R::hidden && window.operation_observations()[6].state_qpc &&
        !window.operation_observations()[6].submitted_qpc,"hidden target publishes state without false submission");
    window.arm_scroll_observation(8,108,SB_LINEUP); SendMessageW(window.handle(),WM_VSCROLL,SB_LINEUP,0);
    window.close();
    require(window.operation_observations()[7].reason==R::closed && !window.operation_observations()[7].submitted_qpc,
        "closing target preserves null submission");
    runtime_wait(window,[&] { return !IsWindow(window.handle()); },"operation window close deadline");
    const auto result=window.collect();
    require(result.complete && !result.active_children && window.operation_observations().size()==8,
        "operation fixture session Job drained with bounded retained value records");
    for(const auto& record:window.operation_observations()) std::cout<<tf_shell::operation_observation_json(record)<<'\n';
    std::cout<<"{\"operation_window\":\"passed\",\"session_job_active_after\":0,\"records\":8,\"literal_visible_rows\":true}\n";
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--owned-desktop-child") {
            require(tf_shell::is_ime_desktop_child(), "runtime smoke requires owned non-input desktop");
            BOOL owned = FALSE;
            require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) != FALSE && owned, "runtime smoke requires owned Job");
            require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
                    "runtime smoke DPI setup failed");
            operation_state_case();
            const auto result = runtime_window_case(); runtime_default_off_case(); operation_window_case(); return result;
        }
        require(argc == 1, "unexpected runtime smoke arguments");
        for(int round=0;round<2;++round)
            require(tf_shell::run_ime_desktop_child(15000) == 0, "owned runtime window assertions failed");
        std::cout << "owned runtime process Job0 and desktop deletion verified\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
