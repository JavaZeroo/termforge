// Shared availability for explicit terminal text actions. Native EDIT input is
// routed by its owner before terminal shortcuts reach this policy.
#pragma once

namespace tf_shell {
enum class TextAction { copy, paste, select_all, find, history_top, history_bottom, cancel_paste, cancel_selection };
enum class TextActionBlock { none, no_session, closing, native_editor, composing, no_selection, paste_busy, no_paste };
struct TextActionContext {
    bool session = false, closing = false, native_editor = false, composing = false;
    bool paste_busy = false, selection = false;
};
struct TextActionState { bool enabled; TextActionBlock reason; };

inline TextActionState text_action_state(TextAction action, const TextActionContext& context) {
    if (!context.session) return {false, TextActionBlock::no_session};
    if (context.closing) return {false, TextActionBlock::closing};
    if (context.native_editor) return {false, TextActionBlock::native_editor};
    if (context.composing) return {false, TextActionBlock::composing};
    if (action == TextAction::paste && context.paste_busy) return {false, TextActionBlock::paste_busy};
    if ((action == TextAction::copy || action == TextAction::cancel_selection) && !context.selection)
        return {false, TextActionBlock::no_selection};
    if (action == TextAction::cancel_paste && !context.paste_busy) return {false, TextActionBlock::no_paste};
    return {true, TextActionBlock::none};
}
inline const char* text_action_block_name(TextActionBlock reason) {
    switch (reason) {
    case TextActionBlock::none: return "none";
    case TextActionBlock::no_session: return "no-session";
    case TextActionBlock::closing: return "closing";
    case TextActionBlock::native_editor: return "native-editor";
    case TextActionBlock::composing: return "composing";
    case TextActionBlock::no_selection: return "no-selection";
    case TextActionBlock::paste_busy: return "paste-busy";
    case TextActionBlock::no_paste: return "no-paste";
    }
    return "unknown";
}
inline const wchar_t* text_action_feedback(TextActionBlock reason) {
    switch (reason) {
    case TextActionBlock::no_session: return L"No active session";
    case TextActionBlock::closing: return L"Session is closing";
    case TextActionBlock::native_editor: return L"Finish renaming before using terminal actions";
    case TextActionBlock::composing: return L"Finish text composition before using terminal actions";
    case TextActionBlock::no_selection: return L"No selected text";
    case TextActionBlock::paste_busy: return L"Paste already in progress";
    case TextActionBlock::no_paste: return L"No paste to cancel";
    case TextActionBlock::none: return L"";
    }
    return L"Text action unavailable";
}
} // namespace tf_shell
