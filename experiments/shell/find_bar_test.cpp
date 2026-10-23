// Actual native EDIT/buttons and modeless routing on an owned non-input desktop.
// This checks HWND/focus/query lifecycle, not physical keys, IME or pixel parity.
#include "find_bar.hpp"
#include "ime_desktop_process.hpp"
#include <iostream>
#include <memory>
#include <utility>
static void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
static unsigned control_a_characters=0;
static unsigned control_f_characters=0;
static void pump(tf_shell::FindBar* bar = nullptr) {
    MSG message{};
    for (int count = 0; count < 1000 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
        if(message.message==WM_CHAR && message.wParam==1) ++control_a_characters;
        if(message.message==WM_CHAR && message.wParam==6) ++control_f_characters;
        if (bar && bar->route(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
static int run() {
    WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TermForgeFindBarTest";
    require(RegisterClassW(&cls) != 0, "find test terminal class failed");
    HWND terminal = CreateWindowExW(0, cls.lpszClassName, L"owned synthetic terminal", WS_OVERLAPPEDWINDOW,
        40, 40, 700, 400, nullptr, nullptr, cls.hInstance, nullptr);
    require(terminal != nullptr, "find test terminal failed");
    ShowWindow(terminal, SW_SHOWNOACTIVATE); SetActiveWindow(terminal); SetFocus(terminal); pump();
    for (int round = 0; round < 6; ++round) {
        auto first = std::make_unique<tf_shell::FindBar>(terminal, 96);
        auto second = std::make_unique<tf_shell::FindBar>(terminal, 144);
        const auto first_window = first->handle(), second_window = second->handle();
        require(first_window && second_window && first_window != second_window, "independent native find windows");
        LOGFONTW owner_font{};
        const auto second_font=reinterpret_cast<HFONT>(SendMessageW(second->edit_handle(),WM_GETFONT,0,0));
        require(second_font && GetObjectW(second_font,sizeof(owner_font),&owner_font)==sizeof(owner_font) && owner_font.lfHeight==-18,
                "Find EDIT must retain the explicit owner display DPI rather than overwrite it with OS DPI");
        second->set_display_dpi(192); second->reposition();
        const auto scaled_font=reinterpret_cast<HFONT>(SendMessageW(second->edit_handle(),WM_GETFONT,0,0));
        require(scaled_font && GetObjectW(scaled_font,sizeof(owner_font),&owner_font)==sizeof(owner_font) && owner_font.lfHeight==-24,
                "owner DPI update and reposition preserve the independently requested native font size");
        second->set_display_dpi(144);
        require(GetWindow(first_window, GW_OWNER) == terminal, "find popup is owned by terminal root");
        first->focus_query(); pump(first.get());
        require(GetFocus() == first->edit_handle(), "find opens native edit focus");
        require(SetWindowTextW(first->edit_handle(), L"中文 e\x0301 needle") != FALSE, "native query edit write failed");
        require(first->query_changed() && !first->query_changed() && first->query() == L"中文 e\x0301 needle",
                "Unicode query roundtrip and consumed EN_CHANGE signal");
        const auto original_query=first->query();
        const auto before_control_a=control_a_characters;
        SendMessageW(first->edit_handle(),EM_SETSEL,static_cast<WPARAM>(original_query.size()),
            static_cast<LPARAM>(original_query.size()));
        BYTE edit_saved[256]{}, edit_keys[256]{};
        require(GetKeyboardState(edit_saved)!=FALSE,"find edit modifiers read failed");
        CopyMemory(edit_keys,edit_saved,sizeof(edit_keys)); edit_keys[VK_CONTROL]=0x80; edit_keys[VK_MENU]=edit_keys[VK_SHIFT]=0;
        require(SetKeyboardState(edit_keys)!=FALSE,"find edit Ctrl state failed");
        require(PostMessageW(first->edit_handle(),WM_KEYDOWN,'A',1)!=FALSE,"find Ctrl+A queue failed");
        pump(first.get());
        const bool edit_restored=SetKeyboardState(edit_saved)!=FALSE;
        DWORD selected_begin=0,selected_end=0;
        SendMessageW(first->edit_handle(),EM_GETSEL,reinterpret_cast<WPARAM>(&selected_begin),reinterpret_cast<LPARAM>(&selected_end));
        require(edit_restored && selected_begin==0 && selected_end==original_query.size(),
                "Ctrl+A must select the actual focused Find EDIT before replacement");
        require(PostMessageW(first->edit_handle(),WM_CHAR,'z',1)!=FALSE,"find replacement character queue failed"); pump(first.get());
        require(first->query()==L"z" && control_a_characters==before_control_a && first->take_action()==tf_shell::FindAction::none,
                "native Find Ctrl+A replacement has no control character or terminal navigation action");
        require(SetWindowTextW(first->edit_handle(),original_query.c_str())!=FALSE,"find original query restore failed");
        first->query_changed();
        require(SetWindowTextW(first->edit_handle(),L"中文")!=FALSE,"find refocus literal setup failed");
        SendMessageW(first->edit_handle(),EM_SETSEL,2,2);
        const auto before_control_f=control_f_characters;
        require(GetKeyboardState(edit_saved)!=FALSE,"find refocus modifiers read failed");
        CopyMemory(edit_keys,edit_saved,sizeof(edit_keys));
        for(const int modifier:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU}) edit_keys[modifier]=0;
        edit_keys[VK_CONTROL]=edit_keys[VK_SHIFT]=0x80;
        require(SetKeyboardState(edit_keys)!=FALSE,"find refocus Ctrl+Shift state failed");
        require(PostMessageW(first->edit_handle(),WM_KEYDOWN,'F',1)!=FALSE &&
            PostMessageW(first->edit_handle(),WM_KEYUP,'F',LPARAM(3)<<30)!=FALSE,"find refocus shortcut queue failed");
        pump(first.get());
        const bool refocus_restored=SetKeyboardState(edit_saved)!=FALSE;
        SendMessageW(first->edit_handle(),EM_GETSEL,reinterpret_cast<WPARAM>(&selected_begin),reinterpret_cast<LPARAM>(&selected_end));
        require(refocus_restored && GetFocus()==first->edit_handle() && selected_begin==0 && selected_end==2,
                "Ctrl+Shift+F inside Find EDIT must reselect the complete literal query");
        require(first->query()==L"中文" && first->take_action()==tf_shell::FindAction::none && control_f_characters==before_control_f,
                "Find refocus does not change the query, navigate or manufacture WM_CHAR 6");
        require(PostMessageW(first->edit_handle(),WM_CHAR,'q',1)!=FALSE,"find refocus replacement queue failed"); pump(first.get());
        require(first->query()==L"q","ordinary input replaces the query selected by the refocus shortcut");
        require(SetWindowTextW(first->edit_handle(),original_query.c_str())!=FALSE,"find query restore after refocus failed");
        first->query_changed();
        require(first->case_sensitive(), "match case is the explicit default");
        SendMessageW(GetDlgItem(first_window, 4105), BM_CLICK, 0, 0);
        require(!first->case_sensitive() && first->query_changed() && !first->query_changed(),
                "case toggle requests one query refresh");
        SendMessageW(GetDlgItem(first_window, 4105), BM_CLICK, 0, 0);
        require(first->case_sensitive() && first->query_changed(), "case toggle restores exact matching");
        MSG key{}; key.hwnd = first->edit_handle(); key.message = WM_KEYDOWN; key.wParam = VK_RETURN;
        require(first->route(key) && first->take_action() == tf_shell::FindAction::previous,
                "Enter records previous action without terminal callback");
        key.wParam = VK_F3;
        require(first->route(key) && first->take_action() == tf_shell::FindAction::next,
                "F3 records next action");
        BYTE saved_keys[256]{}, shifted_keys[256]{};
        require(GetKeyboardState(saved_keys) != FALSE, "owned thread keyboard state read failed");
        CopyMemory(shifted_keys, saved_keys, sizeof(saved_keys)); shifted_keys[VK_SHIFT] |= 0x80;
        require(SetKeyboardState(shifted_keys) != FALSE, "owned thread Shift state failed");
        const auto routed_shift = first->route(key), restored = SetKeyboardState(saved_keys) != FALSE;
        require(routed_shift && restored && first->take_action() == tf_shell::FindAction::previous,
                "Shift-F3 records previous action with only thread-local keyboard state");
        key.wParam = VK_RETURN; first->route(key); key.wParam = VK_F3; first->route(key);
        require(first->take_action() == tf_shell::FindAction::previous &&
                first->take_action() == tf_shell::FindAction::next &&
                first->take_action() == tf_shell::FindAction::none, "two navigation requests retain ordering until owner poll");
        MSG unrelated = key; unrelated.hwnd = terminal;
        require(!first->route(unrelated), "find routing never consumes another terminal's messages");
        key.message = WM_CHAR; key.wParam = VK_RETURN;
        require(first->route(key) && first->query() == L"中文 e\x0301 needle" &&
                first->take_action() == tf_shell::FindAction::none, "queued CR is swallowed without new action");
        for (const auto pair : {std::pair<int, tf_shell::FindAction>{4102, tf_shell::FindAction::previous},
                               {4103, tf_shell::FindAction::next}}) {
            require(GetDlgItem(first_window, pair.first) != nullptr, "find action button missing");
            SendMessageW(GetDlgItem(first_window, pair.first), BM_CLICK, 0, 0);
            require(first->take_action() == pair.second, "actual native button not polled");
        }
        first->set_status(L"1 of 2"); first->reposition();
        SendMessageW(first->edit_handle(), EM_SETSEL, 0, -1);
        const std::wstring oversized(4100, L'x');
        SendMessageW(first->edit_handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(oversized.c_str()));
        require(first->query().size() == tf_shell::FindBar::query_utf16_limit,
                "native user-edit path respects 4096 UTF-16 limit");
        key.message = WM_KEYDOWN; key.wParam = VK_ESCAPE;
        require(first->route(key) && first->take_action() == tf_shell::FindAction::close &&
                !IsWindowVisible(first_window) && IsWindow(first_window), "Escape hides and records close without self destruction");
        require(GetFocus() == terminal, "Escape restores terminal focus");
        first->show(true); SetFocus(first_window);
        require(GetFocus() == first->edit_handle(), "popup activation restores its edit focus");
        second->focus_query(); SendMessageW(second_window, WM_CLOSE, 0, 0);
        require(second->take_action() == tf_shell::FindAction::close && IsWindow(second_window) &&
                !IsWindowVisible(second_window), "native WM_CLOSE defers object/window destruction");
        first.reset(); require(!IsWindow(first_window) && IsWindow(second_window), "one session release preserves other class user");
        second.reset(); require(!IsWindow(second_window), "last find window destroyed");
        WNDCLASSW missing{};
        require(!GetClassInfoW(cls.hInstance, L"TermForgeFindBar", &missing), "last find instance unregisters class");
        pump();
    }
    bool rejected = false;
    try { tf_shell::FindBar invalid(nullptr, 96); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid terminal rejected before class allocation");
    auto owner_closing = std::make_unique<tf_shell::FindBar>(terminal, 96);
    const auto owned_window = owner_closing->handle();
    require(DestroyWindow(terminal) != FALSE && !IsWindow(owned_window) && !owner_closing->handle(),
            "owner destruction releases popup HWND before deferred object cleanup");
    owner_closing.reset(); WNDCLASSW missing{};
    require(!GetClassInfoW(cls.hInstance, L"TermForgeFindBar", &missing), "owner-close object cleanup unregisters class");
    require(UnregisterClassW(cls.lpszClassName, cls.hInstance) != FALSE,
            "test terminal cleanup failed");
    std::cout << "native find query/buttons/focus/routing/class lifecycle passed\n"; return 0;
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--owned-desktop-child") {
            require(tf_shell::is_ime_desktop_child(), "find test requires owned non-input desktop"); return run();
        }
        require(argc == 1, "unexpected find test arguments");
        const auto result = tf_shell::run_ime_desktop_child(30000);
        require(result == 0, "owned native find assertions failed");
        std::cout << "owned find process Job drained and desktop deletion verified\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
