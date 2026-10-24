// Local shell with physical-row resize. See README for the limited contract.
#include "../presentation/render.hpp"
#include "queues.hpp"
#include "resize.hpp"
#include "keyboard.hpp"
#include "ime.hpp"
#include "hosted_ime_input.hpp"
#include "clipboard_worker.hpp"
#include "clipboard.hpp"
#include "paste_prompt.hpp"
#include "../core/text_selection.hpp"
#include "../core/text_search.hpp"
#include "find_bar.hpp"
#include "text_actions.hpp"
#include "runtime_observation.hpp"
#include "operation_observation.hpp"
#include <atomic>
#include <optional>
#include <sstream>

static std::wstring system_cmd() {
    wchar_t folder[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(folder, MAX_PATH);
    require(n && n < MAX_PATH, "system directory failed");
    return std::wstring(folder) + L"\\cmd.exe";
}
struct Result {
    int resize_verified = 0, rows = 24, columns = 80;
    size_t consumed = 0, output_high_water = 0, input_high_water = 0, input_rejected = 0, max_consumed_per_tick = 0;
    int frames = 0, ticks = 0;
    DWORD exit_code = STILL_ACTIVE, active_children = MAXDWORD;
    bool complete = false, normal = false, unicode = false, edited = false, arrows = false, interrupted = false;
    double input_to_state_ms = -1, input_to_submit_ms = -1, max_tick_ms = 0;
    std::string screen;
    std::optional<tf_shell::RuntimeSnapshot> runtime_before_close, runtime_final;
};
class ShellWindow {
    // Only the UI thread owns Parser, Snapshot, Renderer and HWND state.
    const DWORD ui_thread_id_ = GetCurrentThreadId();
    OutputQueue output_;
    tf_conpty::Session session_;
    InputQueue input_{session_};
    ResizeWorker resize_{session_};
    termforge::Parser parser_{24, 80, 100, termforge::ParseMode::recovery};
    termforge::HistoryViewport viewport_;
    std::unique_ptr<Renderer> renderer_ = std::make_unique<Renderer>("dwrite", 96);
    std::shared_ptr<const Snapshot> snapshot_;
    std::unique_ptr<Bitmap> surface_ = std::make_unique<Bitmap>(960, 624);
    HWND hwnd_{};
    HWND parent_{};
    bool registered_ = false;
    inline static unsigned class_users_ = 0; // All instances belong to one UI thread.
    inline static DWORD creation_thread_id_ = 0;
    inline static uint64_t successful_window_creations_ = 0;
    uint64_t window_creation_serial_ = 0;
    std::thread closer_;
    std::atomic<bool> closed_{false};
    bool closing_ = false, started_ = false, metrics_dirty_ = false;
    bool closer_joined_ = false, session_closed_on_closer_ = false;
    std::string failure_, close_failure_, mode_, image_path_;
    int revision_ = 0, stage_ = 0;
    int dirty_first_ = 0, dirty_last_ = 24;
    tf_shell::KeyboardEncoder keyboard_;
    tf_shell::ImeContext ime_;
    tf_shell_test::HostedImeObservation ime_observation_;
    inline static uint64_t next_identity_ = 0;
    const uint64_t identity_ = ++next_identity_;
    std::unique_ptr<tf_shell::ClipboardReadJob> clipboard_read_;
    std::optional<tf_shell::PreparedPaste> pending_paste_;
    std::unique_ptr<tf_shell::PastePrompt> paste_prompt_;
    std::shared_ptr<tf_shell::PasteTransfer> paste_transfer_;
    std::wstring input_status_;
    int wheel_remainder_ = 0;
    termforge::TextSelection selection_;
    std::optional<termforge::TextPosition> selection_anchor_;
    POINT selection_pointer_{}, last_click_point_{};
    DWORD last_click_time_ = 0;
    unsigned click_count_ = 0;
    bool selecting_ = false;
    termforge::TextSearch search_;
    std::unique_ptr<tf_shell::FindBar> find_;
    std::optional<termforge::SearchNavigation> find_current_;
    termforge::TextSelection find_resize_anchor_;
    bool find_resize_pending_ = false, find_resize_wrapped_ = false;
    std::deque<termforge::SearchDirection> find_pending_directions_;
    size_t search_work_budget_ = 4096;
    std::string find_query_;
    bool find_initial_ = false, find_terminal_visible_ = true;
    bool observe_output_ = false;
    std::string observed_output_; // Opt-in synthetic probe, capped independently of queue size.
    Clock::time_point start_ = Clock::now(), input_time_, flood_start_;
    bool frame_after_ = false;
    Result result_;
    bool runtime_enabled_ = false;
    Clock::time_point runtime_origin_;
    std::optional<tf_shell::RuntimeSnapshot> runtime_before_close_, runtime_final_;
    std::optional<tf_shell::RuntimeSnapshot::FinalSession> runtime_final_session_;
    std::optional<tf_shell::RuntimeSnapshot::Paste> runtime_last_terminal_paste_;
    std::optional<tf_shell::RuntimeSnapshot::Clipboard> runtime_last_terminal_clipboard_;
    std::optional<size_t> runtime_paste_source_units_;
    std::optional<termforge::ViewportState> runtime_published_viewport_;
    tf_shell::ScrollOperationObservations operation_observations_;

    static uint64_t operation_qpc() {
        LARGE_INTEGER value{};
        require(QueryPerformanceCounter(&value)!=FALSE && value.QuadPart>0,"operation QPC query failed");
        return static_cast<uint64_t>(value.QuadPart);
    }

    void require_ui_thread() const {
        require(GetCurrentThreadId() == ui_thread_id_, "runtime observation requires the owning UI thread");
    }
    tf_shell::RuntimeSnapshot::Paste runtime_paste_value(const tf_shell::PasteProgress& p) const {
        return {p.target,p.state,p.total,p.accepted,p.sent,p.in_flight,p.retained_capacity,
                runtime_paste_source_units_,!p.error.empty()};
    }
    static tf_shell::RuntimeSnapshot::Clipboard runtime_clipboard_value(const tf_shell::ClipboardReadProgress& p) {
        return {p.state,p.bytes_received,p.input_units,p.win32_error,p.cleanup_complete,!p.error.empty(),std::nullopt};
    }

    void selection_down(int x,int y,unsigned forced_clicks=0);
    void selection_move(int x,int y);
    void selection_end();
    void selection_autoscroll();
    std::optional<termforge::TextPosition> selection_position(int x,int y,bool nearest) const;
    void selection_overlay(termforge::ViewSnapshot& view);
    void selection_menu(int x,int y);
    void poll_find();
    void navigate_find(termforge::SearchDirection direction);
    void search_overlay(termforge::ViewSnapshot& view);
    void search_changed();
    void restore_resized_find_current();
    void clear_find_resize_anchor() { find_resize_anchor_.clear(); find_resize_pending_=false; }

    void resize_window(int columns, int rows) {
        RECT area{0, 0, renderer_->x(columns), renderer_->y(rows)};
        area.right+=GetSystemMetricsForDpi(SM_CXVSCROLL,renderer_->dpi());
        require(AdjustWindowRectExForDpi(&area, WS_OVERLAPPEDWINDOW|WS_VSCROLL, FALSE, 0, renderer_->dpi()) != FALSE, "resize frame calculation failed");
        // Deliberately clip most of this owned test window off the desktop.
        // Retained drawing must stay correct even when rcPaint is clipped.
        require(SetWindowPos(hwnd_, nullptr, GetSystemMetrics(SM_CXSCREEN)-120, GetSystemMetrics(SM_CYSCREEN)-120,
                            area.right-area.left, area.bottom-area.top,
                            SWP_NOZORDER|SWP_NOACTIVATE) != FALSE, "scripted resize failed");
    }
    void size_changed(int width, int height, bool force = false) {
        if (!started_ || closing_ || width <= 0 || height <= 0) return;
        const int columns = std::clamp(width/renderer_->cell_width(), 1, 500);
        const int rows = std::clamp(height/renderer_->cell_height(), 1, 200);
        if (!force && !metrics_dirty_ && columns == parser_.screen.width() && rows == parser_.screen.height()) return;
        auto surface = std::make_unique<Bitmap>(renderer_->x(columns), renderer_->y(rows));
        selection_end();
        // Preserve a current hit only across exact unchanged row projections.
        // Screen::resize can change row versions when only empty columns grow.
        if(const auto current=current_find_range()) {
            find_resize_pending_=find_resize_anchor_.set(termforge::TextDocument(parser_.screen),*current);
            find_resize_wrapped_=find_current_->wrapped;
        }
        parser_.screen.resize(rows, columns);
        search_changed();
        dirty_first_ = 0; dirty_last_ = rows;
        surface_ = std::move(surface); metrics_dirty_ = false; publish();
        resize_.request(rows, columns);
        require(InvalidateRect(hwnd_, nullptr, FALSE) != FALSE, "resize invalidate failed");
    }
    void change_metrics(int dpi, int zoom, bool defer_resize = false) {
        if (closing_ || (dpi == renderer_->dpi() && zoom == renderer_->zoom_percent())) return;
        auto next = std::make_unique<Renderer>("dwrite", dpi, zoom);
        renderer_ = std::move(next);
        if(find_) find_->set_display_dpi(dpi);
        metrics_dirty_ = true;
        // A DPI message supplies a new pixel rectangle too. Resize the grid
        // once, after that rectangle is applied, so an interim narrow grid
        // cannot irreversibly crop physical rows.
        if (defer_resize) return;
        if (started_) {
            RECT client{}; require(GetClientRect(hwnd_, &client) != FALSE, "font resize client query failed");
            size_changed(client.right, client.bottom, true);
        } else {
            surface_ = std::make_unique<Bitmap>(renderer_->x(parser_.screen.width()), renderer_->y(parser_.screen.height()));
        }
    }
    void query_size(bool fixture_output_only = false) {
        wchar_t path[32768]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        require(n && n < 32768, "size client path failed");
        std::wstring file(path, n);
        file = file.substr(0, file.find_last_of(L'\\')+1) + L"shell_size_client.exe";
        // Absolute synthetic helper; it independently queries the child console.
        const auto command=L"\"" + file + L"\"\r";
        if(fixture_output_only) fixture_command(command); else type(command);
    }

    bool line_is(const std::string& expected) const {
        for (const auto& row : snapshot_->lines) {
            std::string text; for (const auto& cell : row) text += cell.text;
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (text == expected) return true;
        }
        return false;
    }
    bool has(const std::string& value) const {
        for (const auto& row : snapshot_->lines) {
            std::string text; for (const auto& cell : row) text += cell.text;
            if (text.find(value) != std::string::npos) return true;
        }
        return false;
    }
    void send_input(std::string_view bytes) {
        if (closing_ || bytes.empty()) return;
        if (!input_.send(std::string(bytes))) {
            input_status(L"Input busy; key rejected"); MessageBeep(MB_ICONWARNING);
        } else if(!viewport_.following()) {
            viewport_.bottom(parser_.screen); publish();
        }
    }
    void input_status(std::wstring value) {
        if(value==input_status_) return;
        input_status_=std::move(value);
        SetWindowTextW(hwnd_,(L"TermForge — "+input_status_).c_str());
        if(parent_) InvalidateRect(parent_,nullptr,FALSE);
    }
    bool allow_text_action(tf_shell::TextAction action) {
        const auto state=text_action_state(action);
        if(!state.enabled && !closing_) {
            // Invalidating a selection removes its range but retains its copy
            // diagnostic. Keep menus disabled without reducing that state to
            // an ordinary empty Copy when a shortcut/WM_COPY is attempted.
            if(action==tf_shell::TextAction::copy && state.reason==tf_shell::TextActionBlock::no_selection)
                report_copy_unavailable(selected_text().status);
            else input_status(tf_shell::text_action_feedback(state.reason));
        }
        return state.enabled;
    }
    void report_copy_unavailable(termforge::SelectionCopyStatus status) {
        input_status(status==termforge::SelectionCopyStatus::empty?L"No selected text":
            status==termforge::SelectionCopyStatus::limit_exceeded?L"Selected text exceeds the copy limit":
            L"Selected text changed; select it again");
        if(status!=termforge::SelectionCopyStatus::empty) publish();
    }
    std::optional<tf_shell::TextAction> text_key_action(WPARAM key,tf_shell::KeyModifiers mods) const {
        using Action=tf_shell::TextAction;
        if(key==VK_ESCAPE && paste_busy()) return Action::cancel_paste;
        if(key==VK_ESCAPE && has_selection()) return Action::cancel_selection;
        if(!mods.alt && mods.control && mods.shift) {
            if(key=='C') return Action::copy;
            if(key=='V') return Action::paste;
            if(key=='A') return Action::select_all;
            if(key=='F') return Action::find;
            if(key==VK_HOME) return Action::history_top;
            if(key==VK_END) return Action::history_bottom;
        }
        if(!mods.alt && mods.shift && !mods.control && key==VK_INSERT) return Action::paste;
        return std::nullopt;
    }
    void start_prepared_paste(tf_shell::PreparedPaste value) {
        const auto observed_source=runtime_enabled_?std::optional<size_t>(value.source_units):std::nullopt;
        auto transfer=std::make_shared<tf_shell::PasteTransfer>(identity_,std::move(value),true);
        if(!input_.begin_paste(transfer)) { input_status(L"Input busy; paste not started"); return; }
        if(runtime_enabled_) runtime_paste_source_units_=observed_source;
        viewport_.bottom(parser_.screen); publish();
        paste_transfer_=std::move(transfer); input_status(L"Pasting — Escape cancels");
    }
    void prepare_paste_text(std::u16string text) {
        auto prepared=tf_shell::prepare_paste(text);
        if(prepared.bytes.empty()) { input_status(L"Clipboard text is empty"); return; }
        if(prepared.confirmation_required) {
            paste_prompt_=std::make_unique<tf_shell::PastePrompt>(hwnd_,identity_,prepared.newline_count,prepared.bytes.size());
            pending_paste_=std::move(prepared); input_status(L"Paste is waiting for confirmation");
        } else start_prepared_paste(std::move(prepared));
    }
    void poll_paste() {
        try {
            if(clipboard_read_) {
                const auto progress=clipboard_read_->poll();
                if(tf_shell::clipboard_read_terminal(progress.state)) {
                    if(runtime_enabled_) runtime_last_terminal_clipboard_=runtime_clipboard_value(progress);
                    if(progress.state==tf_shell::ClipboardReadState::completed && !closing_) {
                        auto text=clipboard_read_->take_text(); clipboard_read_.reset();
                        prepare_paste_text(std::move(text));
                    } else {
                        if(!closing_) input_status(progress.error.empty()?L"Clipboard read cancelled":utf16(progress.error.c_str()));
                        clipboard_read_.reset();
                    }
                }
            }
            if(paste_prompt_ && paste_prompt_->decision()) {
                const bool accepted=paste_prompt_->decision()>0;
                paste_prompt_.reset();
                restore_input_focus();
                auto prepared=std::move(*pending_paste_); pending_paste_.reset();
                if(accepted && !closing_) start_prepared_paste(std::move(prepared));
                else input_status(L"Paste cancelled before sending");
            }
            if(paste_transfer_) {
                const auto progress=paste_transfer_->progress();
                using State=tf_shell::PasteState;
                std::wstring state=progress.state==State::completed?L"Paste sent":progress.state==State::cancelled?L"Paste cancelled":
                    progress.state==State::failed?L"Paste failed":progress.state==State::cancelling?L"Cancelling paste":L"Pasting";
                input_status(state+L": "+std::to_wstring(progress.sent)+L" / "+std::to_wstring(progress.total)+L" bytes"+
                    (progress.error.empty()?L"":L" — "+utf16(progress.error.c_str())));
                if(tf_shell::paste_terminal(progress.state)) {
                    if(runtime_enabled_) runtime_last_terminal_paste_=runtime_paste_value(progress);
                    paste_transfer_.reset();
                }
            }
        } catch(const std::exception& error) {
            if(clipboard_read_) clipboard_read_->cancel();
            paste_prompt_.reset(); pending_paste_.reset();
            input_status(L"Paste failed: "+utf16(error.what()));
        }
    }
    void character(wchar_t value, bool alt = false) {
        if (closing_) return;
        if (value == 3 && stage_ == 5) input_time_ = Clock::now();
        send_input(keyboard_.character(static_cast<char16_t>(value), alt));
    }
    void type(const std::wstring& command) {
        // Exercise the same WM_CHAR handler used by translated keyboard input.
        // This is scripted message injection, not physical keyboard latency.
        for (wchar_t c : command) require(PostMessageW(hwnd_, WM_CHAR, c, 1) != FALSE, "scripted input post failed");
    }
    void begin_close() {
        require_ui_thread();
        if (closing_) return;
        if(runtime_enabled_) runtime_before_close_=runtime_snapshot();
        closing_ = true;
        operation_observations_.close();
        selection_end();
        find_.reset(); search_.close(); find_current_.reset(); clear_find_resize_anchor();
        keyboard_.reset();
        ime_.focus_lost();
        cancel_paste();
        SetWindowTextW(hwnd_, L"TermForge — closing session");
        closer_ = std::thread([this] {
            try {
                input_.stop();
                resize_.stop();
                // Kill only the session job, including descendants after shell exit.
                session_.cancel(); session_.close(); session_closed_on_closer_=true;
            } catch (const std::exception& e) { close_failure_ = e.what(); }
            closed_.store(true);
        });
    }
    void publish(std::optional<size_t> scroll_operation=std::nullopt,bool scroll_changed=false) {
        selection_.reconcile(termforge::TextDocument(parser_.screen));
        auto view = viewport_.snapshot(parser_.screen);
        if(runtime_enabled_) runtime_published_viewport_=view.viewport;
        search_overlay(view);
        selection_overlay(view);
        if (hwnd_) {
            // Keep the disabled bar allocated too: output must not resize and
            // crop the terminal merely because the first history row arrived.
            SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
            info.nMin=0; info.nMax=static_cast<int>(view.viewport.total)-1;
            info.nPage=static_cast<UINT>(view.viewport.page); info.nPos=static_cast<int>(view.viewport.top);
            SetScrollInfo(hwnd_,SB_VERT,&info,TRUE);
        }
        auto next = std::make_shared<const Snapshot>(std::move(view), ++revision_);
        auto dirty = changed_rows(*snapshot_, *next); snapshot_ = std::move(next);
        if(operation_observations_.enabled()) operation_observations_.published(snapshot_->revision);
        if(scroll_operation) operation_observations_.state(*scroll_operation,snapshot_->revision,operation_qpc(),
            scroll_changed,hwnd_ && IsWindowVisible(hwnd_)!=FALSE);
        update_ime_caret();
        if (!dirty.empty()) {
            dirty_first_ = (std::min)(dirty_first_, dirty.front());
            dirty_last_ = (std::max)(dirty_last_, dirty.back()+1);
            RECT area{0, renderer_->y(dirty.front()), surface_->width, renderer_->y(dirty.back()+1)};
            require(InvalidateRect(hwnd_, &area, FALSE) != FALSE, "invalidate failed");
        }
        if (stage_ >= 5 && result_.input_to_state_ms < 0 && line_is("TF_AFTER"))
            result_.input_to_state_ms = ms(input_time_);
    }
    void history_scroll(UINT command,std::optional<size_t> operation=std::nullopt) {
        if (closing_) return;
        const auto state=viewport_.reconcile(parser_.screen);
        const auto page=static_cast<int64_t>((std::max)(size_t{1},state.page-1));
        switch(command) {
        case SB_LINEUP: viewport_.scroll_by(parser_.screen,-1); break;
        case SB_LINEDOWN: viewport_.scroll_by(parser_.screen,1); break;
        case SB_PAGEUP: viewport_.scroll_by(parser_.screen,-page); break;
        case SB_PAGEDOWN: viewport_.scroll_by(parser_.screen,page); break;
        case SB_TOP: viewport_.scroll_to(parser_.screen,0); break;
        case SB_BOTTOM: viewport_.bottom(parser_.screen); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO info{sizeof(info),SIF_TRACKPOS};
            require(GetScrollInfo(hwnd_,SB_VERT,&info)!=FALSE,"history thumb position unavailable");
            viewport_.scroll_to(parser_.screen,static_cast<size_t>((std::max)(0,info.nTrackPos))); break;
        }
        default: return;
        }
        if(operation) {
            const auto after=viewport_.reconcile(parser_.screen);
            publish(operation,state.top!=after.top || state.following!=after.following);
        } else publish();
    }
    void update_ime_caret() {
        if(!hwnd_ || GetFocus()!=hwnd_ || snapshot_->cursor_row<0) return;
        const int x=renderer_->x(snapshot_->cursor_column), y=renderer_->y(snapshot_->cursor_row);
        const RECT caret{x,y,x+renderer_->cell_width(),y+renderer_->cell_height()};
        const bool positioned=ime_.set_caret(caret);
        ime_observation_.caret(hwnd_,caret,positioned);
    }
    void history_wheel(short delta) {
        if (closing_) return;
        wheel_remainder_+=delta;
        const int detents=wheel_remainder_/WHEEL_DELTA;
        wheel_remainder_%=WHEEL_DELTA;
        if (!detents) return;
        UINT lines=3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        const auto page=static_cast<int64_t>((std::max)(1,parser_.screen.height()-1));
        const auto step=lines==WHEEL_PAGESCROLL?page:static_cast<int64_t>(lines);
        viewport_.scroll_by(parser_.screen,-static_cast<int64_t>(detents)*step); publish();
    }
    void scenario() {
        if (mode_.empty() || closing_) return;
        require(ms(start_) < 25000, "shell scenario timeout");
        if (mode_ == "watchdog") {
            if (!stage_ && result_.frames) { std::cout << "{\"watchdog_ready\":true}\n" << std::flush; stage_ = 1; }
            return;
        }
        if (stage_ == 0 && has("TF>")) { type(L"echo TF_READY\r"); stage_ = 1; }
        else if (stage_ == 1 && line_is("TF_READY")) {
            result_.normal = true; type(L"echo \x4e2d\x6587\x00e9\r"); stage_ = 2;
        } else if (stage_ == 2 && line_is("\xe4\xb8\xad\xe6\x96\x87\xc3\xa9")) {
            result_.unicode = true; type(L"echo TF_EDITX\b\r"); stage_ = 3;
        } else if (stage_ == 3 && line_is("TF_EDIT")) {
            result_.edited = true;
            type(L"echo TF_ARROX"); PostMessageW(hwnd_, WM_KEYDOWN, VK_LEFT, 1);
            type(L"W"); PostMessageW(hwnd_, WM_KEYDOWN, VK_RIGHT, 1); type(L"\b\r"); stage_ = 30;
        } else if (stage_ == 30 && line_is("TF_ARROW")) {
            result_.arrows = true;
            if (mode_ == "resize" || mode_ == "resize-close") {
                resize_window(60, 12); stage_ = 40; return;
            }
            type(L"for /L %i in (1,1,1000000) do @echo TF_BUSY_%i\r"); stage_ = 4; flood_start_ = Clock::now();
        } else if (stage_ == 40 && resize_.settled()) {
            query_size(); stage_ = 41;
        } else if (stage_ == 41 && line_is("TF_SIZE_60x12")) {
            require(parser_.screen.width() == 60 && parser_.screen.height() == 12, "core size mismatch");
            ++result_.resize_verified;
            // Coalesce rapid requests without a queue of allocated surfaces.
            resize_window(90, 30); resize_window(50, 10); resize_window(100, 28); stage_ = 42;
        } else if (stage_ == 42 && resize_.settled()) {
            query_size(); stage_ = 43;
        } else if (stage_ == 43 && line_is("TF_SIZE_100x28")) {
            require(parser_.screen.width() == 100 && parser_.screen.height() == 28, "grown core size mismatch");
            ++result_.resize_verified;
            type(L"for /L %i in (1,1,1000000) do @echo TF_BUSY_%i\r"); stage_ = 4; flood_start_ = Clock::now();
        } else if (stage_ == 4 && result_.consumed > 65536 && ms(flood_start_) >= 1000 && has("TF_BUSY_")) {
            if (mode_ == "resize" || mode_ == "resize-close") resize_window(72, 18);
            if (mode_ == "close" || mode_ == "resize-close") { begin_close(); return; }
            type(std::wstring(1, 3));
            type(L"echo TF_AFTER\r"); stage_ = 5;
        } else if (stage_ == 5 && line_is("TF_AFTER") && frame_after_) {
            result_.interrupted = true;
            if (mode_ == "resize") { if (!resize_.settled()) return; query_size(); stage_ = 44; }
            else { type(L"exit 37\r"); stage_ = 6; }
        } else if (stage_ == 44 && line_is("TF_SIZE_72x18")) {
            require(parser_.screen.width() == 72 && parser_.screen.height() == 18, "flood resize mismatch");
            ++result_.resize_verified; type(L"exit 37\r"); stage_ = 6;
        }
    }
    void tick() {
        const auto began = Clock::now(); ++result_.ticks;
        poll_paste();
        selection_autoscroll();
        size_t batch = 0;
        bool drained = false;
        // Check time between small feeds; also retain a hard byte budget.
        do {
            const auto bytes = output_.take(8192);
            if (bytes.empty()) { drained = true; break; }
            if(observe_output_) {
                observed_output_.append(bytes);
                if(observed_output_.size()>8192) observed_output_.erase(0,observed_output_.size()-8192);
            }
            parser_.feed(bytes); batch += bytes.size();
        } while (batch < 65536 && ms(began) < 4);
        result_.max_consumed_per_tick = (std::max)(result_.max_consumed_per_tick, batch);
        if (batch) { result_.consumed += batch; search_changed(); publish(); }
        poll_find();
        session_.check_reader();
        resize_.settled(); // Propagate worker failures even without test scenarios.
        if (closing_ && closed_.load()) {
            require(close_failure_.empty(), close_failure_.c_str());
            if (drained && !clipboard_read_) {
                parser_.finish(); search_changed(); publish(); UpdateWindow(hwnd_);
                if (!image_path_.empty()) surface_->save(image_path_);
                DestroyWindow(hwnd_); hwnd_ = nullptr;
            }
        } else if (!closing_) {
            scenario();
            if (!closing_ && session_.wait_exit(0)) begin_close();
        }
        result_.max_tick_ms = (std::max)(result_.max_tick_ms, ms(began));
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<ShellWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        try {
            if(message==WM_IME_STARTCOMPOSITION && !self->closing_) {
                self->viewport_.bottom(self->parser_.screen); self->publish();
            }
            const auto ime=self->ime_.handle(message,wp,lp);
            self->ime_observation_.message(message,wp,lp,ime);
            if(!ime.committed.empty()) self->send_input(ime.committed);
            if(!ime.error.empty()) self->input_status(L"Input method: "+utf16(ime.error.c_str()));
            if(ime.handled) return ime.result;
            if (message == WM_CLOSE) { self->begin_close(); return 0; }
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
                self->selection_down(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp)),
                                     message==WM_LBUTTONDBLCLK?2:0); return 0;
            }
            if (message == WM_MOUSEMOVE && self->selecting_) {
                self->selection_move(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))); return 0;
            }
            if (message == WM_LBUTTONUP && self->selecting_) {
                self->selection_move(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp)));
                self->selection_end(); return 0;
            }
            if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) self->selection_end();
            if (message == WM_CONTEXTMENU) {
                self->selection_menu(static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))); return 0;
            }
            if (message == WM_VSCROLL) {
                if(!self->operation_observations_.enabled()) { self->history_scroll(LOWORD(wp)); return 0; }
                const auto token=self->operation_observations_.receive(LOWORD(wp),operation_qpc());
                self->history_scroll(LOWORD(wp),token);
                return 0;
            }
            if (message == WM_MOUSEWHEEL) { self->history_wheel(static_cast<short>(HIWORD(wp))); return 0; }
            if (message == WM_KILLFOCUS) { self->keyboard_.reset(); self->selection_end(); }
            if (message == WM_SETFOCUS && self->parent_) {
                self->update_ime_caret();
                SendMessageW(self->parent_,WM_APP+2,0,reinterpret_cast<LPARAM>(self)); return 0;
            }
            if (message == WM_DESTROY) {
                self->ime_.detach();
                if (self->parent_) PostMessageW(self->parent_, WM_APP+1, 0, reinterpret_cast<LPARAM>(self));
                else PostQuitMessage(0);
                return 0;
            }
            if (message == WM_SIZE && wp != SIZE_MINIMIZED) {
                self->size_changed(LOWORD(lp), HIWORD(lp)); return 0;
            }
            if (message == WM_DPICHANGED && !self->parent_) {
                require(lp != 0, "missing suggested DPI rectangle");
                self->set_dpi(LOWORD(wp), true);
                const auto& rect = *reinterpret_cast<const RECT*>(lp);
                require(SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top,
                                     SWP_NOZORDER|SWP_NOACTIVATE) != FALSE, "shell DPI resize failed");
                self->sync_client_size();
                return 0;
            }
            if (message == WM_GETMINMAXINFO && !self->parent_) {
                auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
                RECT area{0,0,self->renderer_->cell_width(),self->renderer_->cell_height()};
                area.right+=GetSystemMetricsForDpi(SM_CXVSCROLL,self->renderer_->dpi());
                AdjustWindowRectExForDpi(&area, WS_OVERLAPPEDWINDOW|WS_VSCROLL, FALSE, 0, self->renderer_->dpi());
                limits->ptMinTrackSize = {area.right-area.left, area.bottom-area.top};
                // The default maximum tracks monitor size, which silently
                // clamps scripted/off-screen windows on small CI desktops.
                area = {0,0,6000,5200}; AdjustWindowRectExForDpi(&area, WS_OVERLAPPEDWINDOW|WS_VSCROLL, FALSE, 0, self->renderer_->dpi());
                limits->ptMaxTrackSize = {area.right-area.left, area.bottom-area.top};
                return 0;
            }
            if (message == WM_CHAR) { self->character(static_cast<wchar_t>(wp)); return 0; }
            if (message == WM_PASTE) { self->perform_text_action(tf_shell::TextAction::paste); return 0; }
            if (message == WM_COPY) { self->perform_text_action(tf_shell::TextAction::copy); return 0; }
            if (message == WM_MOVE && self->find_) self->find_->reposition();
            if (message == WM_SHOWWINDOW && !wp) {
                self->operation_observations_.hide();
                if(self->find_) self->find_->show(false);
            }
            if (message == WM_SYSCHAR) {
                const auto mods=key_modifiers();
                self->character(static_cast<wchar_t>(wp),mods.alt && !mods.alt_graph); return 0;
            }
            if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !self->closing_) {
                if(self->ime_.composing()) return DefWindowProcW(hwnd,message,wp,lp);
                const auto mods=key_modifiers();
                if(const auto action=self->text_key_action(wp,mods)) { self->perform_text_action(*action); return 0; }
                if(!mods.control && !mods.alt && wp==VK_F3) {
                    self->navigate_find(mods.shift?termforge::SearchDirection::previous:termforge::SearchDirection::next); return 0;
                }
                const auto key=tf_shell::KeyboardEncoder::key_down(static_cast<uint32_t>(wp),mods);
                if(key.handled) { self->send_input(key.bytes); return 0; }
            }
            if (message == WM_TIMER && self->started_) { self->tick(); return 0; }
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
                const int painted_revision=self->snapshot_->revision;
                const bool observed=self->operation_observations_.paint_candidate(painted_revision);
                try {
                    // rcPaint is clipped by visible desktop/window regions.
                    // Retained pixels must also incorporate off-screen dirty
                    // rows, otherwise exposing them later reveals stale data.
                    const int cell_height = self->renderer_->cell_height();
                    const int first = (std::min)(self->dirty_first_, (std::max)(0, int(paint.rcPaint.top)/cell_height));
                    const int last = (std::min)(self->snapshot_->rows(),
                        (std::max)(self->dirty_last_, (int(paint.rcPaint.bottom)+cell_height-1)/cell_height));
                    FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                    self->renderer_->draw(*self->surface_, *self->snapshot_, first, last);
                    if (self->mode_ == "resize" || self->mode_ == "resize-close") {
                        Bitmap expected(self->surface_->width, self->surface_->height);
                        self->renderer_->draw(expected, *self->snapshot_, 0, self->snapshot_->rows());
                        require(self->surface_->equals(expected), "resized dirty frame differs from full redraw");
                    }
                    require(BitBlt(dc,0,0,self->surface_->width,self->surface_->height,self->surface_->dc,0,0,SRCCOPY) != FALSE, "shell frame blit failed");
                    require(GdiFlush() != FALSE, "shell frame flush failed"); ++self->result_.frames;
                    if(observed) self->operation_observations_.paint(painted_revision,operation_qpc(),
                        IsWindowVisible(hwnd)!=FALSE,!IsRectEmpty(&paint.rcPaint),true);
                    self->dirty_first_ = self->snapshot_->rows(); self->dirty_last_ = 0;
                    if (self->stage_ >= 5 && !self->frame_after_ && self->line_is("TF_AFTER")) {
                        self->result_.input_to_submit_ms = ms(self->input_time_); self->frame_after_ = true;
                    }
                } catch (...) {
                    if(observed) self->operation_observations_.paint(painted_revision,0,
                        IsWindowVisible(hwnd)!=FALSE,!IsRectEmpty(&paint.rcPaint),false);
                    EndPaint(hwnd, &paint); throw;
                }
                EndPaint(hwnd, &paint); return 0;
            }
        } catch (const std::exception& e) {
            self->failure_ = e.what(); self->output_.stop();
            if (!self->closing_) self->begin_close();
            else if (self->closed_.load()) { DestroyWindow(hwnd); self->hwnd_ = nullptr; }
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
public:
    static tf_shell::KeyModifiers key_modifiers() {
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0, alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        return {(GetKeyState(VK_SHIFT)&0x8000)!=0,ctrl,alt,ctrl && alt};
    }
    static void dispatch_message(const MSG& message) {
        bool translate=true;
        if(message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) {
            wchar_t name[32]{}; GetClassNameW(message.hwnd,name,32);
            if(std::wstring_view(name)==L"TermForgeShell") {
                const auto mods=key_modifiers();
                auto* self=reinterpret_cast<ShellWindow*>(GetWindowLongPtrW(message.hwnd,GWLP_USERDATA));
                const bool text_key=self && self->text_key_action(message.wParam,mods).has_value();
                translate=(self && self->ime_.composing()) || (!text_key &&
                    !tf_shell::KeyboardEncoder::key_down(static_cast<uint32_t>(message.wParam),mods).skip_translate);
            }
        }
        if(translate) TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ShellWindow(std::string mode, std::string image) : mode_(std::move(mode)), image_path_(std::move(image)) {
        snapshot_ = std::make_shared<const Snapshot>(viewport_.snapshot(parser_.screen), 0);
    }
    ~ShellWindow() {
        ime_.detach();
        find_.reset();
        paste_prompt_.reset();
        if(clipboard_read_) clipboard_read_->cancel();
        output_.stop();
        if (closer_.joinable()) closer_.join();
        input_.stop();
        if (hwnd_) DestroyWindow(hwnd_);
        if (registered_ && --class_users_ == 0) UnregisterClassW(L"TermForgeShell", GetModuleHandleW(nullptr));
    }
    void create(HWND parent = nullptr) {
        require_ui_thread();
        require(!creation_thread_id_ || creation_thread_id_ == GetCurrentThreadId(), "shell windows require one UI thread");
        parent_ = parent;
        WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"TermForgeShell"; cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        if (!class_users_) require(RegisterClassW(&cls) != 0, "shell window class failed");
        ++class_users_; registered_ = true;
        const DWORD style = (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW)|WS_VSCROLL;
        RECT size{0,0,renderer_->x(80),renderer_->y(24)};
        size.right+=GetSystemMetricsForDpi(SM_CXVSCROLL,renderer_->dpi());
        AdjustWindowRectExForDpi(&size, style, FALSE, 0, renderer_->dpi());
        hwnd_ = CreateWindowExW(0, cls.lpszClassName, L"TermForge — local shell prototype", style,
            parent ? 0 : CW_USEDEFAULT,parent ? 0 : CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,parent,nullptr,cls.hInstance,this);
        require(hwnd_ != nullptr, "shell window creation failed");
        creation_thread_id_=GetCurrentThreadId(); window_creation_serial_=++successful_window_creations_;
        ime_.attach(hwnd_);
        publish();
        const auto cmd = system_cmd();
        const auto chcp = cmd.substr(0, cmd.find_last_of(L'\\') + 1) + L"chcp.com";
        session_.start(cmd, L"/d /s /q /k \"\"" + chcp + L"\" 65001>nul&prompt TF$G&cls\"",
            [this](std::string_view bytes) { output_.push(bytes); });
        input_.start(); resize_.start(); started_ = true;
        if (!parent_) set_dpi(static_cast<int>(GetDpiForWindow(hwnd_)));
        RECT client{}; GetClientRect(hwnd_, &client); size_changed(client.right, client.bottom);
        require(SetTimer(hwnd_,1,16,nullptr) != 0, "shell timer failed");
    }
    HWND handle() const { return hwnd_; }
    void enable_operation_observation() {
        require_ui_thread(); require(!closing_,"operation observation must be enabled before close");
        if(operation_observations_.enabled()) return;
        LARGE_INTEGER frequency{};
        require(QueryPerformanceFrequency(&frequency)!=FALSE && frequency.QuadPart>0,"operation QPC frequency failed");
        operation_observations_.enable(static_cast<uint64_t>(frequency.QuadPart));
    }
    bool operation_observation_enabled() const { require_ui_thread(); return operation_observations_.enabled(); }
    void arm_scroll_observation(uint64_t id,uint64_t generation,UINT command) {
        require_ui_thread(); require(!closing_ && hwnd_ && IsWindow(hwnd_),"operation target must be live");
        require(command==SB_LINEUP || command==SB_LINEDOWN || command==SB_PAGEUP || command==SB_PAGEDOWN ||
            command==SB_TOP || command==SB_BOTTOM,"unsupported observed scroll command");
        operation_observations_.arm(id,identity_,window_creation_serial_,generation,command);
    }
    std::vector<tf_shell::OperationObservation> operation_observations() const {
        require_ui_thread(); return operation_observations_.values();
    }
    static uint64_t successful_window_creations() {
        require(!creation_thread_id_ || creation_thread_id_ == GetCurrentThreadId(), "shell creation count requires the UI thread");
        return successful_window_creations_;
    }
    void enable_runtime_observation() {
        require_ui_thread();
        require(!closing_, "runtime observation must be enabled before closing");
        if(runtime_enabled_) return;
        runtime_origin_=Clock::now(); runtime_enabled_=true;
    }
    bool runtime_observation_enabled() const { require_ui_thread(); return runtime_enabled_; }
    std::optional<tf_shell::RuntimeSnapshot> runtime_before_close() const { require_ui_thread(); return runtime_before_close_; }
    std::optional<tf_shell::RuntimeSnapshot> runtime_final() const { require_ui_thread(); return runtime_final_; }
    tf_shell::RuntimeSnapshot runtime_snapshot() const {
        require_ui_thread(); require(runtime_enabled_, "runtime observation is not enabled");
        const auto timestamp=[&] {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-runtime_origin_).count());
        };
        tf_shell::RuntimeSnapshot value;
        value.capture_begin_ns=timestamp();
        value.session_id=identity_; value.window_creation_serial=window_creation_serial_;
        value.published_revision=snapshot_->revision; value.rows=parser_.screen.height(); value.columns=parser_.screen.width();
        value.started=started_; value.closing=closing_; value.final_after_join=closer_joined_;
        if(closer_joined_) value.close_error_present=!close_failure_.empty();
        value.window_exists=hwnd_ && IsWindow(hwnd_) &&
            GetWindowLongPtrW(hwnd_,GWLP_USERDATA)==reinterpret_cast<LONG_PTR>(this);
        value.visible=value.window_exists && IsWindowVisible(hwnd_)!=FALSE;
        // Each worker takes and releases its own mutex. This is an interval
        // observation, not an atomic snapshot across queues or paste progress.
        value.output=output_.snapshot(); value.input=input_.snapshot(); value.resize=resize_.snapshot();
        value.history_rows=parser_.screen.history.size(); value.history_limit=parser_.screen.history_limit;
        value.retained_rows=parser_.screen.retained_size(); value.discarded_history_rows=parser_.screen.discarded;
        value.viewport=runtime_published_viewport_;
        value.consumed_bytes=result_.consumed; value.max_consumed_per_tick=result_.max_consumed_per_tick;
        value.ticks=result_.ticks; value.frames=result_.frames; value.max_tick_ns=static_cast<uint64_t>(result_.max_tick_ms*1000000.0);
        value.search.state=search_.status(); value.search.generation=search_.generation();
        value.search.find_query_bytes=find_query_.size(); value.search.find_query_capacity=find_query_.capacity();
        value.search.work_budget_units=search_work_budget_; value.search.result_count=search_.matches().size();
        value.search.result_capacity=search_.matches().capacity(); value.search.retained_metadata_rows=search_.retained_rows();
        value.search.pending_navigation_count=find_pending_directions_.size(); value.search.editor_present=bool(find_);
        value.selection.range=selection_.range(); value.find_resize_anchor.range=find_resize_anchor_.range();
        value.paste_busy=paste_busy(); value.pending_prepared_paste=pending_paste_.has_value(); value.paste_prompt_present=bool(paste_prompt_);
        if(pending_paste_) {
            value.pending_prepared_bytes=pending_paste_->bytes.size(); value.pending_prepared_capacity_bytes=pending_paste_->bytes.capacity();
            value.pending_source_units=pending_paste_->source_units;
        }
        if(paste_transfer_) value.paste=runtime_paste_value(paste_transfer_->progress());
        value.last_terminal_paste=runtime_last_terminal_paste_;
        if(clipboard_read_) value.clipboard=runtime_clipboard_value(clipboard_read_->progress());
        value.last_terminal_clipboard=runtime_last_terminal_clipboard_;
        value.renderer_layout_count=renderer_->layout_count(); value.dpi=renderer_->dpi(); value.zoom_percent=renderer_->zoom_percent();
        value.cell_width=renderer_->cell_width(); value.cell_height=renderer_->cell_height();
        // Session's public fields are populated only into this UI-owned cache
        // after actual closer join AND Session::close has returned.
        value.final_session=runtime_final_session_;
        value.capture_end_ns=timestamp(); return value;
    }
    bool paste_busy() const { return clipboard_read_ || paste_prompt_ || paste_transfer_; }
    tf_shell::TextActionState text_action_state(tf_shell::TextAction action) const {
        return tf_shell::text_action_state(action,{true,closing_,false,ime_.composing(),paste_busy(),has_selection()});
    }
    bool perform_text_action(tf_shell::TextAction action) {
        // Admission is not successful clipboard delivery; status and the
        // operation's actual result remain the source of execution feedback.
        if(!allow_text_action(action)) return false;
        using Action=tf_shell::TextAction;
        switch(action) {
        case Action::copy: request_copy(); break;
        case Action::paste: request_paste(); break;
        case Action::select_all: select_all_text(); break;
        case Action::find: request_find(); break;
        case Action::history_top: history_scroll(SB_TOP); break;
        case Action::history_bottom: history_scroll(SB_BOTTOM); break;
        case Action::cancel_paste: cancel_paste(); break;
        case Action::cancel_selection: selection_end(); selection_.clear(); publish(); break;
        }
        return true;
    }
    std::optional<tf_shell::ClipboardReadProgress> observed_clipboard_read() const {
        return clipboard_read_?std::optional<tf_shell::ClipboardReadProgress>(clipboard_read_->progress()):std::nullopt;
    }
    std::shared_ptr<const tf_shell::PasteTransfer> observed_paste() const { return paste_transfer_; }
    bool composing() const { return ime_.composing(); }
    void begin_ime_observation(unsigned generation) {
        ime_observation_.begin(generation); update_ime_caret();
    }
    tf_shell_test::HostedImeObservation ime_observation() const {
        return ime_observation_.snapshot(ime_,hwnd_);
    }
    std::vector<std::string> ime_retained_lines() const {
        require(ime_observation_.enabled,"IME observation is not enabled");
        const termforge::TextDocument document(parser_.screen);
        require(document.size()<=300,"IME retained line bound exceeded");
        std::vector<std::string> lines;
        size_t bytes=0;
        for(size_t i=0;i<document.size();++i) {
            auto text=document.line(i).projection.text;
            bytes+=text.size(); require(bytes<=32768,"IME retained text bound exceeded");
            lines.push_back(std::move(text));
        }
        return lines;
    }
    bool has_selection() const { return !selection_.empty(); }
    void request_copy();
    void select_all_text();
    void request_find();
    bool route_find_message(MSG& message) { return find_ && find_->route(message); }
    HWND find_handle() const { return find_?find_->handle():nullptr; }
    HWND find_edit_handle() const { return find_?find_->edit_handle():nullptr; }
    std::optional<termforge::TextRange> current_find_range() const {
        if(!find_current_) return std::nullopt;
        const auto value=search_.match(termforge::TextDocument(parser_.screen),find_current_->index);
        return value?std::optional<termforge::TextRange>(value->range):std::nullopt;
    }
    termforge::SearchStatus search_status() const { return search_.status(); }
    uint64_t search_generation() const { return search_.generation(); }
    std::vector<termforge::TextRange> valid_find_ranges() const {
        const termforge::TextDocument document(parser_.screen);
        std::vector<termforge::TextRange> ranges;
        ranges.reserve(search_.matches().size());
        for(size_t index=0;index<search_.matches().size();++index)
            if(const auto match=search_.match(document,index)) ranges.push_back(match->range);
        return ranges;
    }
    std::vector<termforge::DocumentLine> observed_document_lines() const {
        const termforge::TextDocument document(parser_.screen);
        require(document.size()<=300,"observed document exceeded retained row bound");
        std::vector<termforge::DocumentLine> lines; lines.reserve(document.size());
        for(size_t index=0;index<document.size();++index) lines.push_back(document.line(index));
        return lines;
    }
    int observed_tick_count() const { return result_.ticks; }
    void set_search_work_budget(size_t units) {
        require(units>=1 && units<=4096,"search work budget outside testable bound"); search_work_budget_=units;
    }
    termforge::SelectionCopy selected_text() {
        return selection_.copy(termforge::TextDocument(parser_.screen),4*1024*1024);
    }
    const std::wstring& status_text() const { return input_status_; }
    HWND paste_prompt_handle() const { return paste_prompt_?paste_prompt_->handle():nullptr; }
    bool route_paste_message(MSG& message) { return paste_prompt_ && paste_prompt_->route(message); }
    void restore_input_focus() {
        if(closing_) return;
        if(parent_) SendMessageW(parent_,WM_APP+4,0,0);
        else if(IsWindowVisible(hwnd_)) SetFocus(hwnd_);
    }
    void request_paste() {
        if(!allow_text_action(tf_shell::TextAction::paste)) return;
        try { clipboard_read_=std::make_unique<tf_shell::ClipboardReadJob>(); input_status(L"Reading clipboard — Escape cancels"); }
        catch(const std::exception& error) { input_status(L"Clipboard unavailable: "+utf16(error.what())); }
    }
    void cancel_paste() {
        const bool had_prompt=bool(paste_prompt_);
        if(had_prompt) input_status(L"Paste cancelled before sending");
        if(clipboard_read_) clipboard_read_->cancel();
        paste_prompt_.reset(); pending_paste_.reset();
        if(had_prompt) restore_input_focus();
        if(paste_transfer_) paste_transfer_->cancel();
    }
    void synthetic_paste(std::u16string text) {
        require(!closing_ && !paste_busy(),"synthetic paste cannot replace a running operation");
        prepare_paste_text(std::move(text));
    }
    void start_input_receiver(const std::filesystem::path& capture) {
        wchar_t path[32768]{}; const auto count=GetModuleFileNameW(nullptr,path,32768);
        require(count && count<32768,"input receiver location failed");
        const auto client=std::filesystem::path(path).parent_path()/L"shell_input_client.exe";
        command(L"\""+client.wstring()+L"\" \""+capture.wstring()+L"\"\r");
    }
    void start_text_receiver(const std::filesystem::path& capture) {
        wchar_t path[32768]{}; const auto count=GetModuleFileNameW(nullptr,path,32768);
        require(count && count<32768,"text receiver location failed");
        const auto client=std::filesystem::path(path).parent_path()/L"shell_text_client.exe";
        command(L"\""+client.wstring()+L"\" \""+capture.wstring()+L"\"\r");
    }
    bool contains_line(const std::string& text) const { return line_is(text); }
    bool live_contains_line(const std::string& expected) const {
        for(const auto& line:parser_.screen.lines) {
            std::string text; for(const auto& cell:line) text+=cell.text;
            while(!text.empty() && text.back()==' ') text.pop_back();
            if(text==expected) return true;
        }
        return false;
    }
    termforge::ViewportState history_state() { return viewport_.reconcile(parser_.screen); }
    const Snapshot& visible_snapshot() const { return *snapshot_; }
    void begin_output_observation() { observed_output_.clear(); observe_output_=true; }
    const std::string& observed_output() const { return observed_output_; }
    bool ready() const { return has("TF>"); }
    bool settled() { return resize_.settled(); }
    bool closing() const { return closing_; }
    void close() { begin_close(); }
    void command(const std::wstring& text) { type(text); }
    void check_size() { query_size(); }
    void fixture_command(const std::wstring& text) {
        // Producer control for the owned history fixture, not a UI input
        // gesture: background output must be tested without typing in a view.
        require(!closing_ && input_.send(utf8(text.c_str())),"history fixture control rejected");
    }
    void fixture_check_size() { query_size(true); }
    void set_dpi(int dpi, bool defer_resize = false) { change_metrics(dpi, renderer_->zoom_percent(), defer_resize); }
    void sync_client_size() {
        RECT client{}; require(GetClientRect(hwnd_, &client) != FALSE, "shell layout client query failed");
        size_changed(client.right, client.bottom);
        // Workspace places children without synchronous paint while DPI and
        // geometry change together. Even a sub-cell move/resize must expose
        // fresh pixels when the terminal grid itself stays unchanged.
        require(InvalidateRect(hwnd_, nullptr, FALSE) != FALSE, "shell layout invalidate failed");
    }
    void change_zoom(int delta) { change_metrics(renderer_->dpi(), std::clamp(renderer_->zoom_percent()+std::clamp(delta,-150,150),50,200)); }
    void reset_zoom() { change_metrics(renderer_->dpi(),100); }
    int zoom_percent() const { return renderer_->zoom_percent(); }
    int display_dpi() const { return renderer_->dpi(); }
    std::pair<int,int> grid_dimensions() const { return {parser_.screen.width(),parser_.screen.height()}; }
    std::pair<int,int> pixel_cell_size() const { return {renderer_->cell_width(),renderer_->cell_height()}; }
    bool frame_matches_full() {
        // Retained dirty drawing must agree with an independent full redraw,
        // including hidden tabs whose WM_PAINT may not be dispatched.
        if (surface_->width != renderer_->x(snapshot_->columns()) ||
            surface_->height != renderer_->y(snapshot_->rows())) return false;
        renderer_->draw(*surface_, *snapshot_, dirty_first_, dirty_last_);
        dirty_first_=snapshot_->rows(); dirty_last_=0;
        Bitmap expected(surface_->width,surface_->height);
        renderer_->draw(expected,*snapshot_,0,snapshot_->rows());
        return surface_->equals(expected);
    }
    void render_to(HDC dc, int x, int y) {
        renderer_->draw(*surface_, *snapshot_, 0, snapshot_->rows());
        require(BitBlt(dc,x,y,surface_->width,surface_->height,surface_->dc,0,0,SRCCOPY) != FALSE, "workspace terminal capture failed");
    }
    Result run() {
        create();
        ShowWindow(hwnd_, mode_.empty() ? SW_SHOW : SW_SHOWNOACTIVATE); UpdateWindow(hwnd_);
        MSG message{}; BOOL status;
        while ((status = GetMessageW(&message,nullptr,0,0)) > 0) {
            if(!route_find_message(message) && !route_paste_message(message)) dispatch_message(message);
        }
        require(status == 0, "message loop failed");
        return collect();
    }
    Result collect() {
        require_ui_thread();
        if (closer_.joinable()) { closer_.join(); closer_joined_=true; }
        if(runtime_enabled_ && closer_joined_) {
            if(session_closed_on_closer_) {
                tf_shell::RuntimeSnapshot::FinalSession final;
                if(session_.child_signaled) final.exit_code=session_.exit_code;
                if(session_.active_after!=MAXDWORD) final.active_children=session_.active_after;
                final.active_children_query_failed=session_.active_after==MAXDWORD;
                final.read_error=session_.read_error; final.output_bytes=session_.output_bytes;
                final.child_signaled=session_.child_signaled; final.cleanup_complete=session_.cleanup_complete;
                final.capture_overflow=session_.capture_overflow; final.forced_reader_cancel=session_.forced_reader_cancel;
                final.forced_process_cancel=session_.forced_process_cancel; runtime_final_session_=final;
            }
            runtime_final_=runtime_snapshot();
            result_.runtime_before_close=runtime_before_close_; result_.runtime_final=runtime_final_;
        }
        if (!failure_.empty() && !mode_.empty()) {
            std::cerr << "stage=" << stage_ << " bytes=" << result_.consumed << " screen=";
            parser_.screen.json(std::cerr); std::cerr << '\n';
        }
        require(failure_.empty(), failure_.c_str()); require(close_failure_.empty(), close_failure_.c_str());
        result_.exit_code = session_.exit_code; result_.active_children = session_.active_after;
        result_.complete = session_.cleanup_complete;
        result_.output_high_water = output_.high_water; result_.input_high_water = input_.high_water;
        result_.input_rejected = input_.rejected;
        result_.rows = parser_.screen.height(); result_.columns = parser_.screen.width();
        std::ostringstream state; parser_.screen.json(state); result_.screen = state.str();
        return result_;
    }
};
#include "text_interactions.hpp"
#include "search_interactions.hpp"
#include "workspace.hpp"
static int application(int argc, char** argv) {
    try {
        std::string mode, image;
        if(argc==4 && std::string_view(argv[1])=="--test-owned-hosted-ime") {
            tf_shell_test::require_hosted_ime_child_gate();
            mode=argv[2]; image=argv[3];
            require((tf_shell_test::is_product_ime_mode(utf16(mode.c_str())) || tf_shell_test::is_hosted_workflow_mode(utf16(mode.c_str()))) && !image.empty(),"invalid explicit hosted IME case");
            require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=FALSE,"DPI awareness failed");
            Workspace window(image,mode); return window.run();
        }
        require(argc == 1 || (argc == 4 && (std::string(argv[1]) == "--test" || std::string(argv[1]) == "--test-owned")), "termforge_shell [--test mode frame.bmp]");
        if (argc == 4) {
            mode = argv[2]; image = argv[3];
            require(mode == "normal" || mode == "close" || mode == "watchdog" || mode == "resize" || mode == "resize-close" || mode == "ui" || mode == "ui-edit" || mode == "ui-restore" || mode == "ui-main" || mode == "ui-dpi" || mode == "ui-history" || mode == "ui-keyboard" || mode == "ui-paste" || mode == "ui-paste-system" || mode == "ui-clipboard-system" || mode == "ui-selection" || mode == "ui-copy-system" || mode == "ui-find" || mode == "ui-find-stream" || mode == "ui-selection-bytes" || mode == "ui-copy-bytes-system" || mode == "ui-selection-context" || mode == "ui-copy-context-system" || mode == "ui-actions" || mode == "ui-actions-system" || mode == "ui-workflow-sessions", "invalid shell test mode");
            if(mode=="ui-clipboard-system" || mode=="ui-copy-system" || mode=="ui-copy-bytes-system" || mode=="ui-copy-context-system" || mode=="ui-actions-system") tf_shell_test::require_hosted_clipboard();
            if(std::string(argv[1])=="--test") {
                std::string gate; std::getline(std::cin, gate); require(gate == "GO", "missing owned-job gate");
            }
            BOOL in_job=FALSE;
            require(IsProcessInJob(GetCurrentProcess(),nullptr,&in_job)!=FALSE && in_job,"shell test requires an owned job");
        }
        require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE, "DPI awareness failed");
        if (mode.empty()) { Workspace window; return window.run(); }
        wchar_t desktop_name[256]{}; DWORD desktop_size=0;
        require(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop_name,sizeof(desktop_name),&desktop_size)!=FALSE &&
            std::wstring_view(desktop_name).find(L"TermForgeShell-")==0,"shell test requires the runner-owned desktop");
        if (mode == "ui" || mode == "ui-edit" || mode == "ui-restore" || mode == "ui-main" || mode == "ui-dpi" || mode == "ui-history" || mode == "ui-keyboard" || mode == "ui-paste" || mode == "ui-paste-system" || mode == "ui-clipboard-system" || mode == "ui-selection" || mode == "ui-copy-system" || mode == "ui-find" || mode == "ui-find-stream" || mode == "ui-selection-bytes" || mode == "ui-copy-bytes-system" || mode == "ui-selection-context" || mode == "ui-copy-context-system" || mode == "ui-actions" || mode == "ui-actions-system" || mode == "ui-workflow-sessions") {
            Workspace window(image,mode); return window.run();
        }
        Result r;
        {
            ShellWindow window(mode,image); r = window.run();
        }
        const bool valid = r.complete && r.active_children == 0 && r.frames > 0 && r.input_rejected == 0 &&
            (r.normal && r.unicode && r.edited && r.arrows &&
             (mode == "close" || mode == "resize-close" || (r.interrupted && r.exit_code == 37))) &&
            (mode != "resize" || r.resize_verified == 3) && (mode != "resize-close" || r.resize_verified == 2);
        std::cout << "{\"valid\":" << (valid ? "true" : "false") << ",\"mode\":\"" << mode << "\",\"consumed_bytes\":" << r.consumed
            << ",\"frames\":" << r.frames << ",\"ticks\":" << r.ticks << ",\"output_queue_high_water\":" << r.output_high_water
            << ",\"input_queue_high_water\":" << r.input_high_water << ",\"input_rejected\":" << r.input_rejected
            << ",\"exit_code\":" << r.exit_code << ",\"active_children_after\":" << r.active_children
            << ",\"cleanup_complete\":" << (r.complete?"true":"false") << ",\"command_verified\":" << (r.normal?"true":"false")
            << ",\"unicode_verified\":" << (r.unicode?"true":"false") << ",\"backspace_verified\":" << (r.edited?"true":"false")
            << ",\"arrows_verified\":" << (r.arrows?"true":"false")
            << ",\"ctrl_c_verified\":" << (r.interrupted?"true":"false") << ",\"scripted_input_to_state_ms\":";
        if (r.input_to_state_ms < 0) std::cout << "null"; else std::cout << r.input_to_state_ms;
        std::cout << ",\"scripted_input_to_submit_ms\":";
        if (r.input_to_submit_ms < 0) std::cout << "null"; else std::cout << r.input_to_submit_ms;
        std::cout << ",\"max_tick_ms\":" << r.max_tick_ms
            << ",\"resize_verified\":" << r.resize_verified << ",\"rows\":" << r.rows << ",\"columns\":" << r.columns
            << ",\"max_consumed_per_tick\":" << r.max_consumed_per_tick
            << ",\"presented_done\":null,\"screen\":" << r.screen << "}\n";
        return valid ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (argc == 1) MessageBoxW(nullptr, utf16(e.what()).c_str(), L"TermForge", MB_OK | MB_ICONERROR);
        return 2;
    }
}
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return application(__argc, __argv); }
