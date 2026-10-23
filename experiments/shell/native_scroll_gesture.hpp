// Test-only queued mouse gesture through the OS non-client scrollbar tracker.
// No cursor movement, SendInput, input-desktop switch or fabricated WM_VSCROLL.
#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace tf_shell {
struct NativeScrollEvidence {
    int initial = 0, requested = 0, final = 0, minimum = 0, maximum = 0;
    unsigned tracks = 0, positions = 0, ends = 0, moves = 0, system_command = 0;
    bool nonclient_down = false, native_command = false;
    std::vector<int> tracked;
    std::string json() const {
        std::ostringstream out;
        out << "{\"source\":\"native-nonclient-tracker\",\"non_input_desktop\":true,\"queued_mouse_messages\":true,"
               "\"physical_mouse\":false,\"initial\":" << initial << ",\"requested\":" << requested << ",\"final\":" << final
            << ",\"minimum\":" << minimum << ",\"maximum\":" << maximum << ",\"tracks\":" << tracks
            << ",\"positions\":" << positions << ",\"ends\":" << ends << ",\"moves\":" << moves
            << ",\"system_command\":" << system_command
            << ",\"nonclient_down\":" << (nonclient_down ? "true" : "false")
            << ",\"native_command\":" << (native_command ? "true" : "false") << ",\"tracked\":[";
        for (size_t i = 0; i < tracked.size(); ++i) { if (i) out << ','; out << tracked[i]; }
        out << "]}"; return out.str();
    }
};
namespace scroll_gesture_detail {
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline std::wstring name(HANDLE object) {
    wchar_t value[256]{}; DWORD needed = 0;
    check(GetUserObjectInformationW(object, UOI_NAME, value, sizeof(value), &needed) != FALSE, "scroll gesture desktop identity unavailable");
    return value;
}
inline void require_owned_non_input(HWND window) {
    DWORD process = 0;
    check(GetWindowThreadProcessId(window, &process) == GetCurrentThreadId() && process == GetCurrentProcessId(),
          "scroll gesture requires its own UI-thread window");
    const auto desktop = name(GetThreadDesktop(GetCurrentThreadId()));
    check(desktop.rfind(L"TermForgeShell-", 0) == 0, "scroll gesture requires a runner-owned desktop");
    USEROBJECTFLAGS flags{}; DWORD needed = 0;
    check(GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &needed) != FALSE,
          "scroll gesture station flags unavailable");
    if (flags.dwFlags & WSF_VISIBLE) {
        const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        check(input != nullptr, "scroll gesture cannot verify the input desktop");
        std::wstring input_name;
        try { input_name = name(input); } catch (...) { CloseDesktop(input); throw; }
        check(CloseDesktop(input) != FALSE, "scroll gesture input identity handle cleanup failed");
        check(input_name != desktop, "scroll gesture must never run on the input desktop");
    }
}
struct Tracker {
    static constexpr const wchar_t* property = L"TermForgeNativeScrollGesture";
    HWND window;
    WNDPROC previous = nullptr;
    NativeScrollEvidence evidence;
    std::atomic<unsigned> count{0}, moves{0};
    std::atomic<int> last{0};
    std::atomic<bool> done{false}, post_failed{false};
    bool overflow = false;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = static_cast<Tracker*>(GetPropW(window, property));
        if (!self) return DefWindowProcW(window, message, wp, lp);
        if (message == WM_NCLBUTTONDOWN && wp == HTVSCROLL) self->evidence.nonclient_down = true;
        if (message == WM_SYSCOMMAND && ((wp & 0xfff0) == SC_VSCROLL || (wp & 0xfff0) == SC_HSCROLL)) {
            // Preserve the actual native command: Windows 11 may dispatch
            // 0xf087 here for HTVSCROLL. The subsequent WM_VSCROLL notifications
            // and independently queried vertical position establish the axis.
            self->evidence.native_command = true; self->evidence.system_command = static_cast<unsigned>(wp);
        }
        if (message == WM_VSCROLL) {
            const auto command = LOWORD(wp);
            if (command == SB_THUMBTRACK || command == SB_THUMBPOSITION) {
                SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
                if (GetScrollInfo(window, SB_VERT, &info)) {
                    if (self->evidence.tracked.size() < 64) self->evidence.tracked.push_back(info.nTrackPos);
                    else self->overflow = true;
                    self->last.store(info.nTrackPos);
                } else self->post_failed.store(true);
                if (command == SB_THUMBTRACK) { ++self->evidence.tracks; ++self->count; }
                else ++self->evidence.positions;
            } else if (command == SB_ENDSCROLL) ++self->evidence.ends;
        }
        return CallWindowProcW(self->previous, window, message, wp, lp);
    }
    explicit Tracker(HWND value) : window(value) {
        evidence.tracked.reserve(64);
        check(!GetPropW(window, property), "nested native scroll gesture");
        check(SetPropW(window, property, this) != FALSE, "scroll gesture property failed");
        previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure)));
        if (!previous) { RemovePropW(window, property); throw std::runtime_error("scroll gesture subclass failed"); }
    }
    ~Tracker() {
        if (IsWindow(window)) {
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
            RemovePropW(window, property);
        }
    }
    void post(UINT message, WPARAM wp, LPARAM lp) {
        if (!PostMessageW(window, message, wp, lp)) post_failed.store(true);
    }
};
} // namespace scroll_gesture_detail

inline NativeScrollEvidence native_scroll_to(HWND window, int target) {
    using namespace scroll_gesture_detail;
    require_owned_non_input(window);
    check(IsWindowVisible(window) != FALSE, "native scrollbar gesture target is hidden");
    SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE | SIF_POS};
    SCROLLBARINFO bar{sizeof(bar)};
    check(GetScrollInfo(window, SB_VERT, &range) && GetScrollBarInfo(window, OBJID_VSCROLL, &bar),
          "native scrollbar geometry unavailable");
    const int maximum = range.nMax - static_cast<int>(range.nPage ? range.nPage - 1 : 0);
    check(target >= range.nMin && target <= maximum && target != range.nPos, "native scrollbar gesture must move within range");
    const int thumb = bar.xyThumbBottom - bar.xyThumbTop;
    const int travel = bar.rcScrollBar.bottom - bar.rcScrollBar.top - 2 * bar.dxyLineButton - thumb;
    check(thumb > 0 && travel > 0 && maximum > range.nMin, "native scrollbar has no draggable thumb");
    const POINT start{(bar.rcScrollBar.left + bar.rcScrollBar.right) / 2,
                      bar.rcScrollBar.top + (bar.xyThumbTop + bar.xyThumbBottom) / 2};
    check(SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(start.x, start.y)) == HTVSCROLL,
          "native scrollbar hit test missed the thumb region");
    POINT end{start.x, bar.rcScrollBar.top + bar.dxyLineButton + thumb / 2 +
                         MulDiv(target - range.nMin, travel, maximum - range.nMin)};
    check(ScreenToClient(window, &end) != FALSE, "native scrollbar client conversion failed");
    const int initial_y = end.y;
    Tracker tracker(window);
    tracker.evidence.initial = range.nPos; tracker.evidence.requested = target;
    tracker.evidence.minimum = range.nMin; tracker.evidence.maximum = maximum;
    tracker.last.store(range.nPos);
    BYTE saved[256]{}, pressed[256]{};
    check(GetKeyboardState(saved) != FALSE, "scroll gesture key state unavailable");
    std::copy(std::begin(saved), std::end(saved), std::begin(pressed)); pressed[VK_LBUTTON] |= 0x80;
    // SetKeyboardState is local to this UI thread's queue, not physical input.
    check(SetKeyboardState(pressed) != FALSE, "scroll gesture thread-local button state failed");
    std::thread feeder;
    try {
        feeder = std::thread([&] {
            const ULONGLONG deadline = GetTickCount64() + 2000;
            while (!tracker.count.load() && !tracker.done.load() && GetTickCount64() < deadline) Sleep(1);
            // Pixel quantization is OS-owned. Adjust only mouse coordinates;
            // every resulting position must come back from the native tracker.
            for (unsigned attempt = 0; attempt < 20 && !tracker.done.load() && GetTickCount64() < deadline; ++attempt) {
                const auto before = tracker.count.load();
                tracker.post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y)); ++tracker.moves;
                const auto response_deadline = (std::min)(deadline, GetTickCount64() + 80);
                while (tracker.count.load() == before && !tracker.done.load() && GetTickCount64() < response_deadline) Sleep(1);
                const int actual = tracker.last.load();
                if (actual == target) break;
                const int correction = MulDiv(target - actual, travel, maximum - range.nMin);
                end.y += correction ? correction : (target > actual ? 1 : -1);
                if (end.y < initial_y - 24 || end.y > initial_y + 24) break;
            }
            tracker.post(WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
            while (!tracker.done.load() && GetTickCount64() < deadline) Sleep(1);
            if (!tracker.done.load()) tracker.post(WM_CANCELMODE, 0, 0);
        });
        SendMessageW(window, WM_NCLBUTTONDOWN, HTVSCROLL, MAKELPARAM(start.x, start.y));
        tracker.done.store(true); feeder.join();
    } catch (...) {
        tracker.done.store(true); if (feeder.joinable()) feeder.join(); SetKeyboardState(saved); throw;
    }
    check(SetKeyboardState(saved) != FALSE, "scroll gesture key state restore failed");
    check(!tracker.post_failed.load() && !tracker.overflow, "native scrollbar observation failed");
    range.fMask = SIF_POS;
    check(GetScrollInfo(window, SB_VERT, &range) != FALSE, "native scrollbar final position unavailable");
    tracker.evidence.final = range.nPos; tracker.evidence.moves = tracker.moves.load();
    if (!(tracker.evidence.nonclient_down && tracker.evidence.native_command && tracker.evidence.tracks >= 2 &&
          tracker.evidence.positions == 1 && tracker.evidence.ends == 1 && tracker.evidence.moves && range.nPos == target))
        throw std::runtime_error("queued mouse gesture did not complete the native scrollbar tracker: " + tracker.evidence.json());
    return tracker.evidence;
}
} // namespace tf_shell
