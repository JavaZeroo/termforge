// Local shell with physical-row resize. See README for the limited contract.
#include "../presentation/render.hpp"
#include "queues.hpp"
#include "resize.hpp"
#include "../presentation/desktop.hpp"
#include <atomic>
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
};
class ShellWindow {
    // Only the UI thread owns Parser, Snapshot, Renderer and HWND state.
    OutputQueue output_;
    tf_conpty::Session session_;
    InputQueue input_{session_};
    ResizeWorker resize_{session_};
    termforge::Parser parser_{24, 80, 100, termforge::ParseMode::recovery};
    Renderer renderer_{"dwrite", 96};
    std::shared_ptr<const Snapshot> snapshot_;
    std::unique_ptr<Bitmap> surface_ = std::make_unique<Bitmap>(960, 624);
    HWND hwnd_{};
    HWND parent_{};
    bool registered_ = false;
    inline static unsigned class_users_ = 0; // All instances belong to one UI thread.
    std::thread closer_;
    std::atomic<bool> closed_{false};
    bool closing_ = false, started_ = false;
    std::string failure_, close_failure_, mode_, image_path_;
    int revision_ = 0, stage_ = 0;
    int dirty_first_ = 0, dirty_last_ = 24;
    wchar_t high_surrogate_ = 0;
    Clock::time_point start_ = Clock::now(), input_time_, flood_start_;
    bool frame_after_ = false;
    Result result_;

    void resize_window(int columns, int rows) {
        RECT area{0, 0, columns*12, rows*26};
        require(AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE) != FALSE, "resize frame calculation failed");
        // Deliberately clip most of this owned test window off the desktop.
        // Retained drawing must stay correct even when rcPaint is clipped.
        require(SetWindowPos(hwnd_, nullptr, GetSystemMetrics(SM_CXSCREEN)-120, GetSystemMetrics(SM_CYSCREEN)-120,
                            area.right-area.left, area.bottom-area.top,
                            SWP_NOZORDER|SWP_NOACTIVATE) != FALSE, "scripted resize failed");
    }
    void size_changed(int width, int height) {
        if (!started_ || closing_ || width <= 0 || height <= 0) return;
        const int columns = std::clamp(width/12, 1, 500), rows = std::clamp(height/26, 1, 200);
        if (columns == parser_.screen.width() && rows == parser_.screen.height()) return;
        auto surface = std::make_unique<Bitmap>(columns*12, rows*26);
        parser_.screen.resize(rows, columns);
        dirty_first_ = 0; dirty_last_ = rows;
        surface_ = std::move(surface); publish();
        resize_.request(rows, columns);
        require(InvalidateRect(hwnd_, nullptr, FALSE) != FALSE, "resize invalidate failed");
    }
    void query_size() {
        wchar_t path[32768]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        require(n && n < 32768, "size client path failed");
        std::wstring file(path, n);
        file = file.substr(0, file.find_last_of(L'\\')+1) + L"shell_size_client.exe";
        // Absolute synthetic helper; it independently queries the child console.
        type(L"\"" + file + L"\"\r");
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
    void character(wchar_t value) {
        if (closing_) return;
        if (value == 3 && stage_ == 5) input_time_ = Clock::now();
        // WM_CHAR backspace is U+0008; the VT keyboard encoding is DEL.
        if (value == L'\b') value = 0x7f;
        std::wstring text;
        if (value >= 0xd800 && value <= 0xdbff) { high_surrogate_ = value; return; }
        if (high_surrogate_) {
            if (value >= 0xdc00 && value <= 0xdfff) text += high_surrogate_;
            else text += wchar_t(0xfffd);
            high_surrogate_ = 0;
        }
        if (value >= 0xdc00 && value <= 0xdfff && text.empty()) value = 0xfffd;
        text += value;
        const auto bytes = utf8(text.c_str());
        if (!bytes.empty() && !input_.send(bytes)) {
            SetWindowTextW(hwnd_, L"TermForge — input queue full; key rejected"); MessageBeep(MB_ICONWARNING);
        }
    }
    void type(const std::wstring& command) {
        // Exercise the same WM_CHAR handler used by translated keyboard input.
        // This is scripted message injection, not physical keyboard latency.
        for (wchar_t c : command) require(PostMessageW(hwnd_, WM_CHAR, c, 1) != FALSE, "scripted input post failed");
    }
    void begin_close() {
        if (closing_) return;
        closing_ = true;
        SetWindowTextW(hwnd_, L"TermForge — closing session");
        closer_ = std::thread([this] {
            try {
                input_.stop();
                resize_.stop();
                // Kill only the session job, including descendants after shell exit.
                session_.cancel(); session_.close();
            } catch (const std::exception& e) { close_failure_ = e.what(); }
            closed_.store(true);
        });
    }
    void publish() {
        auto next = std::make_shared<const Snapshot>(parser_.screen, ++revision_);
        auto dirty = changed_rows(*snapshot_, *next); snapshot_ = std::move(next);
        if (!dirty.empty()) {
            dirty_first_ = (std::min)(dirty_first_, dirty.front());
            dirty_last_ = (std::max)(dirty_last_, dirty.back()+1);
            RECT area{0, renderer_.y(dirty.front()), surface_->width, renderer_.y(dirty.back()+1)};
            require(InvalidateRect(hwnd_, &area, FALSE) != FALSE, "invalidate failed");
        }
        if (stage_ >= 5 && result_.input_to_state_ms < 0 && line_is("TF_AFTER"))
            result_.input_to_state_ms = ms(input_time_);
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
        size_t batch = 0;
        bool drained = false;
        // Check time between small feeds; also retain a hard byte budget.
        do {
            const auto bytes = output_.take(8192);
            if (bytes.empty()) { drained = true; break; }
            parser_.feed(bytes); batch += bytes.size();
        } while (batch < 65536 && ms(began) < 4);
        result_.max_consumed_per_tick = (std::max)(result_.max_consumed_per_tick, batch);
        if (batch) { result_.consumed += batch; publish(); }
        session_.check_reader();
        resize_.settled(); // Propagate worker failures even without test scenarios.
        if (closing_ && closed_.load()) {
            require(close_failure_.empty(), close_failure_.c_str());
            if (drained) {
                parser_.finish(); publish(); UpdateWindow(hwnd_);
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
            if (message == WM_CLOSE) { self->begin_close(); return 0; }
            if (message == WM_DESTROY) {
                if (self->parent_) PostMessageW(self->parent_, WM_APP+1, 0, reinterpret_cast<LPARAM>(self));
                else PostQuitMessage(0);
                return 0;
            }
            if (message == WM_SIZE && wp != SIZE_MINIMIZED) {
                self->size_changed(LOWORD(lp), HIWORD(lp)); return 0;
            }
            if (message == WM_GETMINMAXINFO && !self->parent_) {
                auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
                RECT area{0,0,12,26}; AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
                limits->ptMinTrackSize = {area.right-area.left, area.bottom-area.top};
                // The default maximum tracks monitor size, which silently
                // clamps scripted/off-screen windows on small CI desktops.
                area = {0,0,500*12,200*26}; AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
                limits->ptMaxTrackSize = {area.right-area.left, area.bottom-area.top};
                return 0;
            }
            if (message == WM_CHAR) { self->character(static_cast<wchar_t>(wp)); return 0; }
            if (message == WM_KEYDOWN && wp >= VK_LEFT && wp <= VK_DOWN && !self->closing_) {
                const char* sequence = wp == VK_LEFT ? "\x1b[D" : wp == VK_RIGHT ? "\x1b[C" : wp == VK_UP ? "\x1b[A" : "\x1b[B";
                if (!self->input_.send(sequence)) MessageBeep(MB_ICONWARNING);
                return 0;
            }
            if (message == WM_TIMER && self->started_) { self->tick(); return 0; }
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
                try {
                    // rcPaint is clipped by visible desktop/window regions.
                    // Retained pixels must also incorporate off-screen dirty
                    // rows, otherwise exposing them later reveals stale data.
                    const int first = (std::min)(self->dirty_first_, (std::max)(0, int(paint.rcPaint.top)/26));
                    const int last = (std::min)(self->snapshot_->rows(),
                        (std::max)(self->dirty_last_, (int(paint.rcPaint.bottom)+25)/26));
                    FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                    self->renderer_.draw(*self->surface_, *self->snapshot_, first, last);
                    if (self->mode_ == "resize" || self->mode_ == "resize-close") {
                        Bitmap expected(self->surface_->width, self->surface_->height);
                        self->renderer_.draw(expected, *self->snapshot_, 0, self->snapshot_->rows());
                        require(self->surface_->equals(expected), "resized dirty frame differs from full redraw");
                    }
                    require(BitBlt(dc,0,0,self->surface_->width,self->surface_->height,self->surface_->dc,0,0,SRCCOPY) != FALSE, "shell frame blit failed");
                    require(GdiFlush() != FALSE, "shell frame flush failed"); ++self->result_.frames;
                    self->dirty_first_ = self->snapshot_->rows(); self->dirty_last_ = 0;
                    if (self->stage_ >= 5 && !self->frame_after_ && self->line_is("TF_AFTER")) {
                        self->result_.input_to_submit_ms = ms(self->input_time_); self->frame_after_ = true;
                    }
                } catch (...) { EndPaint(hwnd, &paint); throw; }
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
    ShellWindow(std::string mode, std::string image) : mode_(std::move(mode)), image_path_(std::move(image)) {
        snapshot_ = std::make_shared<const Snapshot>(parser_.screen, 0);
    }
    ~ShellWindow() {
        output_.stop();
        if (closer_.joinable()) closer_.join();
        input_.stop();
        if (hwnd_) DestroyWindow(hwnd_);
        if (registered_ && --class_users_ == 0) UnregisterClassW(L"TermForgeShell", GetModuleHandleW(nullptr));
    }
    void create(HWND parent = nullptr) {
        parent_ = parent;
        WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"TermForgeShell"; cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        if (!class_users_) require(RegisterClassW(&cls) != 0, "shell window class failed");
        ++class_users_; registered_ = true;
        const DWORD style = parent ? WS_CHILD : WS_OVERLAPPEDWINDOW;
        RECT size{0,0,960,624}; AdjustWindowRect(&size, style, FALSE);
        hwnd_ = CreateWindowExW(0, cls.lpszClassName, L"TermForge — local shell prototype", style,
            parent ? 0 : CW_USEDEFAULT,parent ? 0 : CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,parent,nullptr,cls.hInstance,this);
        require(hwnd_ != nullptr, "shell window creation failed");
        const auto cmd = system_cmd();
        const auto chcp = cmd.substr(0, cmd.find_last_of(L'\\') + 1) + L"chcp.com";
        session_.start(cmd, L"/d /s /q /k \"\"" + chcp + L"\" 65001>nul&prompt TF$G&cls\"",
            [this](std::string_view bytes) { output_.push(bytes); });
        input_.start(); resize_.start(); started_ = true;
        RECT client{}; GetClientRect(hwnd_, &client); size_changed(client.right, client.bottom);
        require(SetTimer(hwnd_,1,16,nullptr) != 0, "shell timer failed");
    }
    HWND handle() const { return hwnd_; }
    bool contains_line(const std::string& text) const { return line_is(text); }
    bool ready() const { return has("TF>"); }
    bool settled() { return resize_.settled(); }
    bool closing() const { return closing_; }
    void close() { begin_close(); }
    void command(const std::wstring& text) { type(text); }
    void check_size() { query_size(); }
    void render_to(HDC dc, int x, int y) {
        renderer_.draw(*surface_, *snapshot_, 0, snapshot_->rows());
        require(BitBlt(dc,x,y,surface_->width,surface_->height,surface_->dc,0,0,SRCCOPY) != FALSE, "workspace terminal capture failed");
    }
    Result run() {
        create();
        ShowWindow(hwnd_, mode_.empty() ? SW_SHOW : SW_SHOWNOACTIVATE); UpdateWindow(hwnd_);
        MSG message{}; BOOL status;
        while ((status = GetMessageW(&message,nullptr,0,0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        require(status == 0, "message loop failed");
        return collect();
    }
    Result collect() {
        if (closer_.joinable()) closer_.join();
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
#include "workspace.hpp"
static int application(int argc, char** argv) {
    try {
        std::string mode, image;
        require(argc == 1 || (argc == 4 && std::string(argv[1]) == "--test"), "termforge_shell [--test normal|close|resize|resize-close|watchdog|ui frame.bmp]");
        if (argc == 4) {
            mode = argv[2]; image = argv[3];
            require(mode == "normal" || mode == "close" || mode == "watchdog" || mode == "resize" || mode == "resize-close" || mode == "ui", "invalid shell test mode");
            std::string gate; std::getline(std::cin, gate); require(gate == "GO", "missing owned-job gate");
        }
        require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE, "DPI awareness failed");
        if (mode.empty()) { Workspace window; return window.run(); }
        if (mode == "ui") {
            OwnedDesktop desktop(L"TermForgeShell-");
            return desktop.run([&] { Workspace window(image); return window.run(); });
        }
        Result r;
        {
            OwnedDesktop desktop(L"TermForgeShell-");
            r = desktop.run([&] { ShellWindow window(mode,image); return window.run(); });
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
