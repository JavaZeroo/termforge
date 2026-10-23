// Only the owned, focused native editor handles this extra editing shortcut.
#pragma once
#include <windows.h>
#include <imm.h>

namespace tf_shell {
inline bool native_edit_composing(HWND editor) noexcept {
    if(!editor) return false;
    const HIMC context=ImmGetContext(editor);
    if(!context) return false;
    const auto bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);
    ImmReleaseContext(editor,context);
    return bytes>0;
}
inline bool route_edit_select_all(const MSG& message,HWND editor) {
    if(!editor || message.hwnd!=editor || GetFocus()!=editor || message.message!=WM_KEYDOWN || message.wParam!='A' ||
        !(GetKeyState(VK_CONTROL)&0x8000) || (GetKeyState(VK_MENU)&0x8000) || native_edit_composing(editor)) return false;
    SendMessageW(editor,EM_SETSEL,0,-1);
    // The caller consumes WM_KEYDOWN before TranslateMessage can create ^A.
    return true;
}
} // namespace tf_shell
