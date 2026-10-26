// Synthetic console client: query the OS, not the host's requested dimensions.
#include "../conpty/session.hpp"
#include <string>
int wmain(int argc, wchar_t** argv) {
    tf_conpty::Handle release;
    if(argc!=1) {
        if(argc!=3 || std::wstring_view(argv[1])!=L"--wait-event") return 4;
        const std::wstring_view name(argv[2]);
        if(name.rfind(L"Local\\TermForgeShellSize-",0)!=0 || name.size()>160) return 4;
        release.reset(OpenEventW(SYNCHRONIZE,FALSE,argv[2]));
        if(!release.get()) return 4;
    }
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(output, &info)) return 2;
    const auto text = "\r\nTF_SIZE_" + std::to_string(info.dwSize.X) + "x" + std::to_string(info.dwSize.Y) + "\r\n";
    DWORD written = 0;
    if(!WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written!=text.size()) return 3;
    // A marker proves dimensions, not that this process has returned to its shell.
    // The opt-in fixture holds that boundary until the UI observes it explicitly.
    if(release.get() && WaitForSingleObject(release.get(),15000)!=WAIT_OBJECT_0) return 5;
    return 0;
}
