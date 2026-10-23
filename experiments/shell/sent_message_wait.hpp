// A UI-thread event wait that leaves posted application work queued.
#pragma once
#include <windows.h>

namespace tf_shell_test {
inline DWORD wait_event_with_sent_messages(HANDLE event,DWORD timeout_ms) noexcept {
    if(timeout_ms==INFINITE) { SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED; }
    const auto deadline=GetTickCount64()+timeout_ms;
    for(;;) {
        const auto ready=WaitForSingleObject(event,0);
        if(ready!=WAIT_TIMEOUT) return ready;
        const auto now=GetTickCount64();
        if(now>=deadline) return WAIT_TIMEOUT;
        const auto result=MsgWaitForMultipleObjectsEx(1,&event,static_cast<DWORD>(deadline-now),
            QS_SENDMESSAGE,MWMO_INPUTAVAILABLE);
        if(result!=WAIT_OBJECT_0+1) return result;
        // PeekMessage services synchronous cross-thread calls before looking
        // for posted work. PM_NOREMOVE leaves timers, commands and scenario
        // callbacks queued; never TranslateMessage or DispatchMessage here.
        MSG ignored{}; PeekMessageW(&ignored,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
    }
}
} // namespace tf_shell_test
