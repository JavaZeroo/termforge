#include "text_actions.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace tf_shell;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main() {
    try {
        TextActionContext target{true,false,false,false,true,true};
        const auto paste=text_action_state(TextAction::paste,target);
        check(!paste.enabled && paste.reason==TextActionBlock::paste_busy,
            "busy Paste must be unavailable in both Session and terminal menus, before execution");
        target.composing=true;
        for (const auto action : {TextAction::copy,TextAction::paste,TextAction::select_all,TextAction::find,
                TextAction::history_top,TextAction::history_bottom,TextAction::cancel_paste,TextAction::cancel_selection}) {
            const auto state=text_action_state(action,target);
            check(!state.enabled && state.reason==TextActionBlock::composing,
                "terminal menu actions must respect the same composition gate as terminal shortcuts");
        }
        target={true,false,false,false,false,false};
        const auto cancel=text_action_state(TextAction::cancel_selection,target);
        check(!cancel.enabled && cancel.reason==TextActionBlock::no_selection,
            "Escape only consumes a terminal selection cancellation when a selection exists");
        for (const auto action : {TextAction::copy,TextAction::paste,TextAction::select_all,TextAction::find,
                TextAction::history_top,TextAction::history_bottom,TextAction::cancel_paste,TextAction::cancel_selection}) {
            const auto no_session=text_action_state(action,{});
            check(!no_session.enabled && no_session.reason==TextActionBlock::no_session,"no-session actions cannot acquire a terminal target");
            const auto closing=text_action_state(action,{true,true,false,false,true,true});
            check(!closing.enabled && closing.reason==TextActionBlock::closing,"closing target cannot accept new text actions");
            const auto editor=text_action_state(action,{true,false,true,false,true,true});
            check(!editor.enabled && editor.reason==TextActionBlock::native_editor,"rename editor retains native text input");
        }
        check(text_action_state(TextAction::paste,target).enabled && text_action_state(TextAction::find,target).enabled &&
            text_action_state(TextAction::select_all,target).enabled,"ordinary target supports paste/find/all without a terminal selection");
        check(text_action_state(TextAction::copy,target).reason==TextActionBlock::no_selection &&
            text_action_state(TextAction::cancel_paste,target).reason==TextActionBlock::no_paste,"empty copy and idle paste cancellation are distinguished");
        TextActionContext background{true,false,false,false,true,true};
        check(!text_action_state(TextAction::paste,background).enabled && text_action_state(TextAction::paste,target).enabled &&
            text_action_state(TextAction::copy,background).enabled && text_action_state(TextAction::cancel_paste,background).enabled,
            "one target's paste does not change another target and does not disable unrelated copy or cancellation");
        check(std::wstring(text_action_feedback(TextActionBlock::paste_busy))==L"Paste already in progress" &&
            std::wstring(text_action_feedback(TextActionBlock::no_selection))==L"No selected text","unavailable shortcut reports a concrete reason");
        std::cout << "text action policy passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
