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
    require(tasks.selection.range && tasks.search.editor_present && tasks.search.result_count >= 1 &&
        tasks.search.find_query_bytes == 19 && tasks.pending_prepared_paste && tasks.paste_prompt_present &&
        tasks.pending_prepared_bytes == 22 && tasks.pending_source_units == size_t{23} && !tasks.paste &&
        tasks.pending_prepared_capacity_bytes >= tasks.pending_prepared_bytes,
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
    window.close();
    runtime_wait(window, [&] { return IsWindow(window.handle()) == FALSE; }, "unobserved real shell closes deadline");
    const auto result = window.collect();
    require(result.complete && !result.runtime_before_close && !result.runtime_final &&
        !window.runtime_before_close() && !window.runtime_final(), "normal lifecycle creates no runtime samples");
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--owned-desktop-child") {
            require(tf_shell::is_ime_desktop_child(), "runtime smoke requires owned non-input desktop");
            BOOL owned = FALSE;
            require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) != FALSE && owned, "runtime smoke requires owned Job");
            require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
                    "runtime smoke DPI setup failed");
            const auto result = runtime_window_case(); runtime_default_off_case(); return result;
        }
        require(argc == 1, "unexpected runtime smoke arguments");
        require(tf_shell::run_ime_desktop_child(15000) == 0, "owned runtime window assertions failed");
        std::cout << "owned runtime process Job0 and desktop deletion verified\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
