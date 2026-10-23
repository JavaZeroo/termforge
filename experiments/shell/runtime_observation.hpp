// Opt-in, bounded value observations. No terminal text, HWND or live object references.
#pragma once
#include "queues.hpp"
#include "resize.hpp"
#include "clipboard_worker.hpp"
#include "../core/history_view.hpp"
#include "../core/text_search.hpp"
#include <locale>
#include <optional>
#include <sstream>

namespace tf_shell {
struct RuntimeSnapshot {
    struct Search {
        termforge::SearchStatus state = termforge::SearchStatus::idle;
        uint64_t generation = 0;
        size_t find_query_bytes = 0, find_query_capacity = 0, work_budget_units = 0;
        size_t result_count = 0, result_capacity = 0, retained_metadata_rows = 0, pending_navigation_count = 0;
        std::optional<size_t> actual_work_units;
        bool editor_present = false;
    };
    struct Selection {
        std::optional<termforge::TextRange> range;
        // Current core exposes no cheap const getters. Unknown is not zero;
        // sampling must not reconcile/copy a selection to manufacture these.
        std::optional<size_t> copied_bytes, retained_capacity_bytes;
    };
    struct Paste {
        uint64_t target = 0;
        PasteState state = PasteState::ready;
        size_t total_bytes = 0, accepted_bytes = 0, sent_bytes = 0, in_flight_bytes = 0, retained_capacity_bytes = 0;
        std::optional<size_t> source_units;
        bool error_present = false;
    };
    struct Clipboard {
        ClipboardReadState state = ClipboardReadState::running;
        size_t bytes_received = 0, input_units = 0;
        uint32_t native_error = 0;
        bool cleanup_complete = false, error_present = false;
        std::optional<size_t> retained_capacity_bytes;
    };
    struct FinalSession {
        std::optional<uint32_t> exit_code, active_children;
        uint32_t read_error = 0;
        uint64_t output_bytes = 0;
        bool child_signaled = false, cleanup_complete = false, capture_overflow = false;
        bool forced_reader_cancel = false, forced_process_cancel = false;
        bool active_children_query_failed = false;
    };
    uint64_t session_id = 0, window_creation_serial = 0, capture_begin_ns = 0, capture_end_ns = 0;
    int published_revision = 0, rows = 0, columns = 0;
    bool started = false, closing = false, window_exists = false, visible = false, final_after_join = false;
    std::optional<bool> close_error_present; // Closer-owned error is unreadable before join.
    OutputQueue::Snapshot output;
    InputQueue::Snapshot input;
    ResizeWorker::Snapshot resize;
    size_t history_rows = 0, history_limit = 0, retained_rows = 0, discarded_history_rows = 0;
    std::optional<size_t> history_capacity_bytes;
    std::optional<termforge::ViewportState> viewport; // Published after observation was enabled.
    size_t consumed_bytes = 0, max_consumed_per_tick = 0;
    int ticks = 0, frames = 0;
    uint64_t max_tick_ns = 0; // Converted from the existing UI tick duration statistic.
    Search search;
    Selection selection, find_resize_anchor;
    bool paste_busy = false, pending_prepared_paste = false, paste_prompt_present = false;
    size_t pending_prepared_bytes = 0, pending_prepared_capacity_bytes = 0;
    std::optional<size_t> pending_source_units;
    std::optional<Paste> paste, last_terminal_paste;
    std::optional<Clipboard> clipboard, last_terminal_clipboard;
    size_t renderer_layout_count = 0;
    int dpi = 0, zoom_percent = 0, cell_width = 0, cell_height = 0;
    std::optional<FinalSession> final_session;
};

namespace runtime_detail {
inline const char* state_name(termforge::SearchStatus state) {
    switch (state) {
    case termforge::SearchStatus::idle: return "idle";
    case termforge::SearchStatus::running: return "running";
    case termforge::SearchStatus::complete: return "complete";
    case termforge::SearchStatus::capped: return "capped";
    case termforge::SearchStatus::cancelled: return "cancelled";
    case termforge::SearchStatus::invalidated: return "invalidated";
    case termforge::SearchStatus::closed: return "closed";
    }
    throw std::invalid_argument("invalid runtime search state");
}
inline const char* state_name(PasteState state) {
    switch (state) {
    case PasteState::ready: return "ready";
    case PasteState::sending: return "sending";
    case PasteState::cancelling: return "cancelling";
    case PasteState::completed: return "completed";
    case PasteState::cancelled: return "cancelled";
    case PasteState::failed: return "failed";
    }
    throw std::invalid_argument("invalid runtime paste state");
}
inline const char* state_name(ClipboardReadState state) {
    switch (state) {
    case ClipboardReadState::running: return "running";
    case ClipboardReadState::cancelling: return "cancelling";
    case ClipboardReadState::completed: return "completed";
    case ClipboardReadState::cancelled: return "cancelled";
    case ClipboardReadState::timed_out: return "timed_out";
    case ClipboardReadState::failed: return "failed";
    }
    throw std::invalid_argument("invalid runtime clipboard state");
}
template<class T> inline void optional_scalar(std::ostream& out, const std::optional<T>& value) {
    if (value) out << *value; else out << "null";
}
inline void selection(std::ostream& out, const RuntimeSnapshot::Selection& value) {
    out << "{\"range\":";
    if (value.range) {
        const auto position = [&](const termforge::TextPosition& p) {
            out << "{\"line_id\":" << p.line_id << ",\"version\":" << p.version << ",\"column\":" << p.column << '}';
        };
        out << "{\"begin\":"; position(value.range->begin); out << ",\"end\":"; position(value.range->end); out << '}';
    } else out << "null";
    out << ",\"copied_bytes\":"; optional_scalar(out, value.copied_bytes);
    out << ",\"retained_capacity_bytes\":"; optional_scalar(out, value.retained_capacity_bytes); out << '}';
}
inline void paste(std::ostream& out, const std::optional<RuntimeSnapshot::Paste>& value) {
    if (!value) { out << "null"; return; }
    const auto& p = *value;
    out << "{\"target\":" << p.target << ",\"state\":\"" << state_name(p.state)
        << "\",\"total_bytes\":" << p.total_bytes << ",\"accepted_bytes\":" << p.accepted_bytes
        << ",\"sent_bytes\":" << p.sent_bytes << ",\"in_flight_bytes\":" << p.in_flight_bytes
        << ",\"retained_capacity_bytes\":" << p.retained_capacity_bytes << ",\"source_units\":";
    optional_scalar(out, p.source_units); out << ",\"error_present\":" << p.error_present << '}';
}
inline void clipboard(std::ostream& out, const std::optional<RuntimeSnapshot::Clipboard>& value) {
    if (!value) { out << "null"; return; }
    const auto& p = *value;
    out << "{\"state\":\"" << state_name(p.state) << "\",\"bytes_received\":" << p.bytes_received
        << ",\"input_units\":" << p.input_units << ",\"native_error\":" << p.native_error
        << ",\"cleanup_complete\":" << p.cleanup_complete << ",\"error_present\":" << p.error_present
        << ",\"retained_capacity_bytes\":"; optional_scalar(out, p.retained_capacity_bytes); out << '}';
}
} // namespace runtime_detail

inline std::string runtime_snapshot_json(const RuntimeSnapshot& s) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::boolalpha;
    out << "{\"schema\":\"termforge-runtime-snapshot-v1\",\"clock\":\"steady-relative-to-enable-ns\""
        << ",\"session_id\":" << s.session_id << ",\"window_creation_serial\":" << s.window_creation_serial
        << ",\"capture_begin_ns\":" << s.capture_begin_ns << ",\"capture_end_ns\":" << s.capture_end_ns
        << ",\"published_revision\":" << s.published_revision << ",\"rows\":" << s.rows << ",\"columns\":" << s.columns
        << ",\"started\":" << s.started << ",\"closing\":" << s.closing << ",\"window_exists\":" << s.window_exists
        << ",\"visible\":" << s.visible << ",\"final_after_join\":" << s.final_after_join
        << ",\"close_error_present\":";
    runtime_detail::optional_scalar(out, s.close_error_present);
    out << ",\"output\":{\"queued_bytes\":" << s.output.queued_bytes << ",\"high_water_bytes\":" << s.output.high_water_bytes
        << ",\"stopped\":" << s.output.stopped << '}'
        << ",\"input\":{\"queued_bytes\":" << s.input.queued_bytes << ",\"in_flight_bytes\":" << s.input.in_flight_bytes
        << ",\"high_water_bytes\":" << s.input.high_water_bytes << ",\"rejected\":" << s.input.rejected
        << ",\"stopping\":" << s.input.stopping << ",\"started\":" << s.input.started << ",\"joined\":" << s.input.joined
        << ",\"failed\":" << s.input.failed << '}'
        << ",\"resize\":{\"requested_generation\":" << s.resize.requested_generation
        << ",\"completed_generation\":" << s.resize.completed_generation << ",\"working_generation\":" << s.resize.working_generation
        << ",\"pending_generation\":" << s.resize.pending_generation << ",\"working\":" << s.resize.working
        << ",\"pending\":" << s.resize.pending << ",\"stopping\":" << s.resize.stopping
        << ",\"started\":" << s.resize.started << ",\"joined\":" << s.resize.joined << ",\"failed\":" << s.resize.failed << '}'
        << ",\"history_rows\":" << s.history_rows << ",\"history_limit\":" << s.history_limit
        << ",\"retained_rows\":" << s.retained_rows << ",\"discarded_history_rows\":" << s.discarded_history_rows
        << ",\"history_capacity_bytes\":"; runtime_detail::optional_scalar(out, s.history_capacity_bytes);
    out << ",\"viewport\":";
    if(s.viewport) out << "{\"top\":" << s.viewport->top << ",\"total\":" << s.viewport->total
        << ",\"page\":" << s.viewport->page << ",\"maximum_top\":" << s.viewport->maximum_top
        << ",\"anchor\":" << s.viewport->anchor << ",\"following\":" << s.viewport->following << '}';
    else out << "null";
    out << ",\"consumed_bytes\":" << s.consumed_bytes << ",\"max_consumed_per_tick\":" << s.max_consumed_per_tick
        << ",\"ticks\":" << s.ticks << ",\"frames\":" << s.frames << ",\"max_tick_ns\":" << s.max_tick_ns
        << ",\"search\":{\"state\":\"" << runtime_detail::state_name(s.search.state)
        << "\",\"generation\":" << s.search.generation << ",\"find_query_bytes\":" << s.search.find_query_bytes
        << ",\"find_query_capacity\":" << s.search.find_query_capacity << ",\"work_budget_units\":" << s.search.work_budget_units
        << ",\"result_count\":" << s.search.result_count << ",\"result_capacity\":" << s.search.result_capacity
        << ",\"retained_metadata_rows\":" << s.search.retained_metadata_rows
        << ",\"pending_navigation_count\":" << s.search.pending_navigation_count << ",\"editor_present\":" << s.search.editor_present
        << ",\"actual_work_units\":"; runtime_detail::optional_scalar(out, s.search.actual_work_units); out << '}';
    out << ",\"selection\":"; runtime_detail::selection(out, s.selection);
    out << ",\"find_resize_anchor\":"; runtime_detail::selection(out, s.find_resize_anchor);
    out << ",\"paste_busy\":" << s.paste_busy << ",\"pending_prepared_paste\":" << s.pending_prepared_paste
        << ",\"paste_prompt_present\":" << s.paste_prompt_present << ",\"pending_prepared_bytes\":" << s.pending_prepared_bytes
        << ",\"pending_prepared_capacity_bytes\":" << s.pending_prepared_capacity_bytes << ",\"pending_source_units\":";
    runtime_detail::optional_scalar(out, s.pending_source_units);
    out << ",\"paste\":"; runtime_detail::paste(out, s.paste);
    out << ",\"last_terminal_paste\":"; runtime_detail::paste(out, s.last_terminal_paste);
    out << ",\"clipboard\":"; runtime_detail::clipboard(out, s.clipboard);
    out << ",\"last_terminal_clipboard\":"; runtime_detail::clipboard(out, s.last_terminal_clipboard);
    out << ",\"renderer_layout_count\":" << s.renderer_layout_count << ",\"dpi\":" << s.dpi
        << ",\"zoom_percent\":" << s.zoom_percent << ",\"cell_width\":" << s.cell_width << ",\"cell_height\":" << s.cell_height
        << ",\"final_session\":";
    if (s.final_session) {
        const auto& f = *s.final_session;
        out << "{\"exit_code\":"; runtime_detail::optional_scalar(out, f.exit_code);
        out << ",\"active_children\":"; runtime_detail::optional_scalar(out, f.active_children);
        out << ",\"active_children_query_failed\":" << f.active_children_query_failed << ",\"read_error\":" << f.read_error
            << ",\"output_bytes\":" << f.output_bytes << ",\"child_signaled\":" << f.child_signaled
            << ",\"cleanup_complete\":" << f.cleanup_complete << ",\"capture_overflow\":" << f.capture_overflow
            << ",\"forced_reader_cancel\":" << f.forced_reader_cancel << ",\"forced_process_cancel\":" << f.forced_process_cancel << '}';
    } else out << "null";
    out << '}'; return out.str();
}
} // namespace tf_shell
