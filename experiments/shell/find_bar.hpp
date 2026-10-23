// Original modeless native find controls. The session owner polls changes and
// actions; no callback destroys this object or re-enters the terminal workflow.
#pragma once
#include <windows.h>
#include <imm.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>
#include <initializer_list>

namespace tf_shell {
enum class FindAction { none, previous, next, close };
class FindBar {
    static constexpr wchar_t class_name_[] = L"TermForgeFindBar";
    static constexpr int edit_id_ = 4101, previous_id_ = 4102, next_id_ = 4103, close_id_ = 4104, case_id_ = 4105;
    inline static std::mutex class_mutex_;
    inline static size_t class_users_ = 0;
    HWND terminal_ = nullptr, owner_ = nullptr, window_ = nullptr;
    HWND edit_ = nullptr, previous_ = nullptr, next_ = nullptr, close_ = nullptr, status_ = nullptr;
    HWND case_ = nullptr;
    HFONT font_ = nullptr;
    int dpi_ = 96;
    bool registered_ = false, query_changed_ = false;
    // Normal key repeat is retained without an unbounded message-to-action
    // buffer. Overflow coalesces repeat requests; close always takes priority.
    std::array<FindAction, 64> actions_{};
    size_t action_head_ = 0, action_count_ = 0;

    static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    int px(int value) const { return MulDiv(value, dpi_, 96); }
    void acquire_class() {
        std::lock_guard<std::mutex> lock(class_mutex_);
        if (!class_users_) {
            WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = class_name_; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
            require(RegisterClassW(&cls) != 0, "find bar class registration failed");
        }
        ++class_users_; registered_ = true;
    }
    void release() noexcept {
        if (window_) DestroyWindow(window_);
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        if (registered_) {
            std::lock_guard<std::mutex> lock(class_mutex_);
            if (--class_users_ == 0) UnregisterClassW(class_name_, GetModuleHandleW(nullptr));
            registered_ = false;
        }
    }
    bool set_dpi(int dpi) noexcept {
        if (dpi < 48 || dpi > 768) return false;
        if (font_ && dpi == dpi_) return true;
        HFONT next_font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!next_font) return false;
        for (const auto control : {edit_, previous_, next_, close_, status_, case_})
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        const auto old = font_; font_ = next_font; dpi_ = dpi;
        if (old) DeleteObject(old);
        return true;
    }
    void layout() noexcept {
        if (!edit_) return;
        RECT area{}; if (!GetClientRect(window_, &area)) return;
        const int margin = px(8), gap = px(6), button = px(68), height = px(25);
        const int available = area.right - margin * 2;
        const int edit_width = (std::max)(px(40), available - (button + gap) * 3);
        MoveWindow(edit_, margin, margin, edit_width, height, TRUE);
        int x = margin + edit_width + gap;
        for (const auto control : {previous_, next_, close_}) {
            MoveWindow(control, x, margin, button, height, TRUE); x += button + gap;
        }
        MoveWindow(status_, margin, margin + height + px(6), available - px(125), px(20), TRUE);
        MoveWindow(case_, area.right - margin - px(115), margin + height + px(6), px(115), px(20), TRUE);
    }
    bool contains(HWND window) const noexcept { return window_ && (window == window_ || IsChild(window_, window)); }
    void request(FindAction action) noexcept {
        if (action == FindAction::close) {
            action_head_ = 0; action_count_ = 1; actions_[0] = action;
            const bool had_focus = contains(GetFocus());
            if (window_) ShowWindow(window_, SW_HIDE);
            if (had_focus && IsWindow(terminal_)) { SetActiveWindow(owner_); SetFocus(terminal_); }
        } else if (action_count_ < actions_.size()) {
            // A pending close cannot be replaced by a late navigation message.
            if (action_count_ && actions_[action_head_] == FindAction::close) return;
            actions_[(action_head_ + action_count_) % actions_.size()] = action; ++action_count_;
        }
    }
    bool native_composition() const noexcept {
        if (GetFocus() != edit_) return false;
        const auto context = ImmGetContext(edit_); if (!context) return false;
        const auto bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
        ImmReleaseContext(edit_, context); return bytes > 0;
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<FindBar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<FindBar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        if (message == WM_COMMAND) {
            const int id = LOWORD(wp), notification = HIWORD(wp);
            if (id == edit_id_ && notification == EN_CHANGE) { self->query_changed_ = true; return 0; }
            if (notification == BN_CLICKED) {
                if (id == previous_id_) { self->request(FindAction::previous); return 0; }
                if (id == next_id_) { self->request(FindAction::next); return 0; }
                if (id == close_id_) { self->request(FindAction::close); return 0; }
                if (id == case_id_) { self->query_changed_ = true; return 0; }
            }
        }
        if (message == WM_CLOSE) { self->request(FindAction::close); return 0; }
        if (message == WM_SETFOCUS && self->edit_) { SetFocus(self->edit_); return 0; }
        if (message == WM_SIZE) { self->layout(); return 0; }
        if (message == WM_DPICHANGED) {
            if (self->set_dpi(HIWORD(wp))) {
                const auto suggested = reinterpret_cast<const RECT*>(lp);
                if (suggested) SetWindowPos(window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                self->layout();
            }
            return 0;
        }
        if (message == WM_NCDESTROY) {
            self->window_ = self->edit_ = self->previous_ = self->next_ = self->close_ = self->status_ = nullptr;
            self->case_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(window, message, wp, lp);
    }
public:
    static constexpr size_t query_utf16_limit = 4096;
    FindBar(HWND terminal, int dpi) : terminal_(terminal) {
        DWORD process = 0;
        require(IsWindow(terminal_) && GetWindowThreadProcessId(terminal_, &process) == GetCurrentThreadId() &&
            process == GetCurrentProcessId(), "find bar requires its same-thread terminal window");
        require(dpi >= 48 && dpi <= 768, "find bar DPI outside supported range");
        owner_ = GetAncestor(terminal_, GA_ROOT);
        require(owner_ != nullptr, "find bar owner window unavailable");
        const auto actual_dpi = GetDpiForWindow(terminal_);
        dpi_ = actual_dpi ? static_cast<int>(actual_dpi) : dpi;
        acquire_class();
        try {
            const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
            require(CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, class_name_, L"Find", style,
                0, 0, 1, 1, owner_, nullptr, GetModuleHandleW(nullptr), this) != nullptr,
                "find bar window creation failed");
            const auto instance = GetModuleHandleW(nullptr);
            edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(edit_id_)), instance, nullptr);
            previous_ = CreateWindowExW(0, L"BUTTON", L"Previous", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(previous_id_)), instance, nullptr);
            next_ = CreateWindowExW(0, L"BUTTON", L"Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id_)), instance, nullptr);
            close_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(close_id_)), instance, nullptr);
            status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 1, 1, window_, nullptr, instance, nullptr);
            case_ = CreateWindowExW(0, L"BUTTON", L"Match case", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(case_id_)), instance, nullptr);
            require(edit_ && previous_ && next_ && close_ && status_ && case_, "find bar controls creation failed");
            SendMessageW(edit_, EM_SETLIMITTEXT, query_utf16_limit, 0);
            SendMessageW(case_, BM_SETCHECK, BST_CHECKED, 0);
            require(set_dpi(dpi_), "find bar font creation failed");
            reposition();
        } catch (...) { release(); throw; }
    }
    ~FindBar() { release(); }
    FindBar(const FindBar&) = delete;
    FindBar& operator=(const FindBar&) = delete;
    HWND handle() const noexcept { return window_; }
    HWND edit_handle() const noexcept { return edit_; }
    bool case_sensitive() const noexcept { return !case_ || SendMessageW(case_, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    std::wstring query() const {
        if (!edit_) return {};
        const auto length = GetWindowTextLengthW(edit_);
        // WM_SETTEXT can bypass EDIT's user-input limit. Fail without copying
        // unbounded text if a programmatic caller violates the native contract.
        require(length >= 0 && static_cast<size_t>(length) <= query_utf16_limit, "find query exceeds UTF-16 limit");
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        const auto copied = GetWindowTextW(edit_, value.data(), static_cast<int>(value.size()));
        require(copied == length, "find query text retrieval failed"); value.resize(copied); return value;
    }
    bool query_changed() noexcept { const bool changed = query_changed_; query_changed_ = false; return changed; }
    FindAction take_action() noexcept {
        if (!action_count_) return FindAction::none;
        const auto action = actions_[action_head_]; action_head_ = (action_head_ + 1) % actions_.size();
        --action_count_; return action;
    }
    bool route(MSG& message) {
        if (!contains(message.hwnd)) return false;
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            const auto key = message.wParam;
            const bool composing = (key == VK_RETURN || key == VK_ESCAPE) && native_composition();
            if (composing) {
                // IsDialogMessage would consume Return/Escape as dialog keys;
                // native EDIT/IME must receive them to commit/cancel composition.
                TranslateMessage(&message); DispatchMessageW(&message); return true;
            }
            if (!composing && (key == VK_RETURN || key == VK_F3 || key == VK_ESCAPE)) {
                request(key == VK_ESCAPE ? FindAction::close : key == VK_RETURN || (GetKeyState(VK_SHIFT) & 0x8000)
                    ? FindAction::previous : FindAction::next);
                return true; // No TranslateMessage: never manufacture a CR/Escape WM_CHAR.
            }
        }
        if ((message.message == WM_CHAR || message.message == WM_SYSCHAR) &&
            (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) && !native_composition()) return true;
        if (!IsDialogMessageW(window_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        return true;
    }
    void focus_query() {
        if (!window_) return;
        show(true); SetActiveWindow(window_); SetFocus(edit_); SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
    void set_status(const std::wstring& text) { if (status_) SetWindowTextW(status_, text.c_str()); }
    void reposition() {
        if (!window_) return;
        RECT terminal{}; require(GetWindowRect(terminal_, &terminal) != FALSE, "find terminal rectangle unavailable");
        const auto actual = GetDpiForWindow(terminal_);
        if (actual) require(set_dpi(static_cast<int>(actual)), "find bar DPI font update failed");
        RECT bounds{0, 0, px(540), px(68)};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
        require(AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, dpi_) != FALSE,
                "find bar frame dimensions failed");
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        LONG x = terminal.right - width - px(8), y = terminal.top + px(8);
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromRect(&terminal, MONITOR_DEFAULTTONEAREST), &monitor)) {
            x = (std::max)(monitor.rcWork.left, (std::min)(x, monitor.rcWork.right - width));
            y = (std::max)(monitor.rcWork.top, (std::min)(y, monitor.rcWork.bottom - height));
        }
        require(SetWindowPos(window_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "find bar positioning failed"); layout();
    }
    void show(bool visible) {
        if (!window_) return;
        if (visible) { reposition(); ShowWindow(window_, SW_SHOWNOACTIVATE); }
        else {
            const bool had_focus = contains(GetFocus()); ShowWindow(window_, SW_HIDE);
            if (had_focus && IsWindow(terminal_)) { SetActiveWindow(owner_); SetFocus(terminal_); }
        }
    }
};
} // namespace tf_shell
