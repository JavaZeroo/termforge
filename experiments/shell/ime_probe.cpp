// Native-context and real-IME evidence probe. SendInput is restricted to an
// explicitly gated disposable hosted VM; local modes never inject global input.
#include "ime.hpp"
#include "ime_desktop_process.hpp"
#include <msctf.h>
#include <wrl/client.h>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;
static const char* isolation = "owned-window-station";
static bool hosted_input = false;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::string utf8(const std::wstring& value) {
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr,0,nullptr,nullptr);
    std::string result(n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),n,nullptr,nullptr);
    return result;
}
static std::string quoted(const std::string& text) {
    std::string value = "\"";
    for (unsigned char c : text) {
        if(c<32) { static const char hex[]="0123456789abcdef"; value+="\\u00"; value+=hex[c>>4]; value+=hex[c&15]; }
        else { if(c=='\\' || c=='"') value+='\\'; value+=static_cast<char>(c); }
    }
    return value + '"';
}
static std::string guid(REFGUID value) { wchar_t text[40]{}; StringFromGUID2(value,text,40); return utf8(text); }
static void pump() {
    MSG message{};
    for(int count=0;count<1000 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++count) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
static void pump_for(int milliseconds) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    while(std::chrono::steady_clock::now()<deadline) { pump(); MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT); }
}
static bool environment_is(const char* name, const char* value) {
    char found[256]{}; const auto count=GetEnvironmentVariableA(name,found,sizeof(found));
    return count>0 && count<sizeof(found) && std::string(found,count)==value;
}
static std::wstring object_name(HANDLE object) {
    wchar_t value[256]{}; DWORD used=0;
    require(GetUserObjectInformationW(object,UOI_NAME,value,sizeof(value),&used)!=FALSE,"desktop identity query failed");
    return value;
}
static void require_hosted_input() {
    require(environment_is("GITHUB_ACTIONS","true") && environment_is("RUNNER_ENVIRONMENT","github-hosted") &&
            environment_is("RUNNER_OS","Windows") && environment_is("GITHUB_REPOSITORY","JavaZeroo/termforge") &&
            (environment_is("ImageOS","win22") || environment_is("ImageOS","win25")),
            "input-desktop mode is restricted to the disposable hosted Windows test VM");
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    require(input!=nullptr,"hosted VM has no accessible input desktop");
    const auto name=object_name(input); CloseDesktop(input);
    require(name==object_name(GetThreadDesktop(GetCurrentThreadId())) && object_name(GetProcessWindowStation())==L"WinSta0",
            "hosted runner is not attached to the input desktop");
}
static void inject_owned_key(HWND window, WORD key) {
    require(hosted_input && GetForegroundWindow()==window && GetFocus()==window,"owned window lost foreground; key injection stopped");
    INPUT input[2]{}; input[0].type=input[1].type=INPUT_KEYBOARD;
    input[0].ki.wVk=input[1].ki.wVk=key; input[1].ki.dwFlags=KEYEVENTF_KEYUP;
    require(SendInput(2,input,sizeof(INPUT))==2,"hosted IME key injection failed"); pump_for(100);
}
struct WindowState {
    tf_shell::ImeContext ime;
    tf_shell::KeyboardEncoder characters;
    int composition_events = 0, result_events = 0, candidate_events = 0;
    std::string committed, character_commits, error;
};
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if (state) {
        if(message==WM_CHAR) state->character_commits+=state->characters.character(static_cast<char16_t>(wp));
        if (message == WM_IME_COMPOSITION) { ++state->composition_events; if (lp & GCS_RESULTSTR) ++state->result_events; }
        if (message == WM_IME_NOTIFY && (wp == IMN_OPENCANDIDATE || wp == IMN_CHANGECANDIDATE)) ++state->candidate_events;
        auto result = state->ime.handle(message,wp,lp);
        state->committed += result.committed;
        if(!result.error.empty()) state->error=result.error;
        if (result.handled) return result.result;
    }
    return DefWindowProcW(window,message,wp,lp);
}
struct CandidateWindows { int visible = 0; std::vector<RECT> rectangles; std::vector<std::string> classes; std::vector<HWND> windows; };
static BOOL CALLBACK enumerate_windows(HWND window, LPARAM param) {
    auto& found = *reinterpret_cast<CandidateWindows*>(param);
    wchar_t name[256]{}; GetClassNameW(window,name,256);
    const std::wstring cls(name);
    if (IsWindowVisible(window) && (cls.find(L"Candidate") != std::wstring::npos ||
        cls.find(L"candidate") != std::wstring::npos || cls.find(L"IME") != std::wstring::npos ||
        cls.find(L"Weasel") != std::wstring::npos)) {
        RECT rectangle{}; if (GetWindowRect(window,&rectangle)) {
            ++found.visible; found.rectangles.push_back(rectangle); found.classes.push_back(utf8(cls)); found.windows.push_back(window);
        }
    }
    return TRUE;
}
static int run() {
    require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)), "COM initialization failed");
    struct ComScope { ~ComScope() { CoUninitialize(); } } com;
    ComPtr<ITfThreadMgr> thread;
    require(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread))), "TSF thread manager unavailable");
    TfClientId client = 0; const HRESULT activated = thread->Activate(&client);
    require(SUCCEEDED(activated), "TSF activation failed");
    ComPtr<ITfInputProcessorProfiles> profiles;
    require(SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles))), "TSF profiles unavailable");
    ComPtr<IEnumTfLanguageProfiles> enumeration;
    require(SUCCEEDED(profiles->EnumLanguageProfiles(0x0804,&enumeration)), "Chinese profile enumeration failed");
    TF_LANGUAGEPROFILE selected{}; bool have_selected = false;
    std::cout << "{\"schema\":\"termforge-ime-probe-v1\",\"isolation\":" << quoted(isolation) << ",\"profiles\":[";
    TF_LANGUAGEPROFILE profile{}; ULONG count = 0; bool first = true;
    while (enumeration->Next(1,&profile,&count) == S_OK && count) {
        BSTR description = nullptr;
        profiles->GetLanguageProfileDescription(profile.clsid,profile.langid,profile.guidProfile,&description);
        const std::wstring name = description ? std::wstring(description,SysStringLen(description)) : std::wstring();
        SysFreeString(description);
        BOOL enabled = FALSE; const auto enabled_status = profiles->IsEnabledLanguageProfile(profile.clsid,profile.langid,profile.guidProfile,&enabled);
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"description\":" << quoted(utf8(name)) << ",\"clsid\":" << quoted(guid(profile.clsid))
                  << ",\"profile\":" << quoted(guid(profile.guidProfile)) << ",\"enabled\":" << (SUCCEEDED(enabled_status) && enabled ? "true" : "false") << '}';
        // Pin the official Microsoft Pinyin provider and language profile; a
        // translated or third-party description is not sufficient identity.
        const bool pinyin=guid(profile.clsid)=="{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}" &&
                          guid(profile.guidProfile)=="{FA550B04-5AD7-411F-A5AC-CA038EC515D7}";
        if (SUCCEEDED(enabled_status) && enabled && (pinyin ||
            (!hosted_input && !have_selected && name.find(L"Weasel") != std::wstring::npos))) {
            selected = profile; have_selected = true;
        }
    }
    std::cout << "],\"profile_enabled\":" << (have_selected ? "true" : "false");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=window_proc; cls.lpszClassName=L"TermForgeImeProbe";
    require(RegisterClassW(&cls)!=0,"IME probe registration failed");
    WindowState state;
    HWND window = CreateWindowExW(0,cls.lpszClassName,L"TermForge owned IME probe",WS_OVERLAPPEDWINDOW,
                                 100,100,640,480,nullptr,nullptr,cls.hInstance,&state);
    require(window!=nullptr,"IME probe window failed"); state.ime.attach(window);
    ShowWindow(window,hosted_input ? SW_SHOW : SW_SHOWNOACTIVATE);
    if(hosted_input) require(SetForegroundWindow(window)!=FALSE,"hosted window foreground failed");
    SetActiveWindow(window); SetFocus(window); state.ime.focus_gained();
    const RECT caret{96,120,108,144}; const bool positioned = state.ime.set_caret(caret);
    CANDIDATEFORM candidate{}; const bool queried = ImmGetCandidateWindow(state.ime.native_context(),0,&candidate)!=FALSE;
    const bool position_roundtrip = queried && candidate.dwStyle==CFS_EXCLUDE && EqualRect(&candidate.rcArea,&caret);
    HRESULT profile_result = S_FALSE, language_result = S_FALSE; int keys_eaten = 0;
    if (have_selected) {
        language_result = profiles->ChangeCurrentLanguage(selected.langid);
        ComPtr<ITfInputProcessorProfileMgr> manager;
        if (SUCCEEDED(profiles.As(&manager)))
            profile_result = manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,selected.langid,selected.clsid,
                                                      selected.guidProfile,nullptr,TF_IPPMF_FORPROCESS);
        pump();
        ComPtr<ITfKeystrokeMgr> keys;
        if (profile_result == S_OK && hosted_input) {
            ImmSetOpenStatus(state.ime.native_context(),TRUE);
            ImmSetConversionStatus(state.ime.native_context(),IME_CMODE_NATIVE,IME_SMODE_NONE);
            for(char key : std::string("NIHAO")) inject_owned_key(window,static_cast<WORD>(key));
        } else if (profile_result == S_OK && SUCCEEDED(thread.As(&keys))) {
            for (char key : std::string("NIHAO")) {
                BOOL eaten = FALSE;
                const LPARAM flags = 1 | (MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC) << 16);
                if (keys->TestKeyDown(key,flags,&eaten)==S_OK && eaten && keys->KeyDown(key,flags,&eaten)==S_OK && eaten) ++keys_eaten;
                keys->KeyUp(key,flags | (static_cast<LPARAM>(3)<<30),&eaten); pump();
            }
        }
    }
    pump_for(600);
    CandidateWindows candidates; EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),enumerate_windows,reinterpret_cast<LPARAM>(&candidates));
    // Compare a real visible candidate window at two physical caret locations.
    // The mere IMM coordinate roundtrip above is not this observation.
    const bool composition_observed=state.ime.composing() && !state.ime.preedit().empty() && state.committed.empty();
    const auto observed_preedit=utf8(state.ime.preedit());
    CandidateWindows moved; bool candidate_moved=false, commit_verified=false, cancel_verified=false;
    if(hosted_input && profile_result==S_OK) {
        state.ime.set_caret(RECT{240,216,252,240}); pump_for(400);
        EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),enumerate_windows,reinterpret_cast<LPARAM>(&moved));
        for(size_t i=0;i<candidates.classes.size();++i) for(size_t j=0;j<moved.classes.size();++j) {
            const auto& name=candidates.classes[i]; const auto a=candidates.rectangles[i], b=moved.rectangles[j];
            if(candidates.windows[i]==moved.windows[j] && name==moved.classes[j] &&
               (name.find("Candidate")!=std::string::npos || name.find("candidate")!=std::string::npos) &&
               abs((b.left-a.left)-144)<=8 && abs((b.top-a.top)-96)<=8) candidate_moved=true;
        }
        inject_owned_key(window,VK_SPACE); pump_for(300);
        commit_verified=state.committed=="\xe4\xbd\xa0\xe5\xa5\xbd" && state.result_events==1 && state.character_commits.empty();
        const auto committed_before_cancel=state.committed;
        for(char key : std::string("NI")) inject_owned_key(window,static_cast<WORD>(key));
        const bool had_preedit=state.ime.composing() && !state.ime.preedit().empty();
        inject_owned_key(window,VK_ESCAPE); pump_for(200);
        cancel_verified=had_preedit && !state.ime.composing() && state.committed==committed_before_cancel;
    }
    std::cout << ",\"language_hresult\":" << language_result << ",\"activation_hresult\":" << profile_result
              << ",\"keys_eaten\":" << keys_eaten << ",\"composition_events\":" << state.composition_events
              << ",\"result_events\":" << state.result_events << ",\"candidate_events\":" << state.candidate_events
              << ",\"visible_candidate_windows\":" << candidates.visible << ",\"candidate_rectangles\":[";
    for (size_t i=0;i<candidates.rectangles.size();++i) { if(i) std::cout<<','; const auto r=candidates.rectangles[i]; std::cout<<'['<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<']'; }
    std::cout << "],\"candidate_classes\":[";
    for(size_t i=0;i<candidates.classes.size();++i) {if(i) std::cout<<',';std::cout<<quoted(candidates.classes[i]);}
    std::cout << "],\"moved_candidate_rectangles\":[";
    for(size_t i=0;i<moved.rectangles.size();++i) {if(i)std::cout<<',';const auto r=moved.rectangles[i];std::cout<<'['<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<']';}
    const bool workflow=hosted_input && composition_observed && candidate_moved && commit_verified && cancel_verified && state.error.empty();
    std::cout << "],\"observed_preedit\":" << quoted(observed_preedit)
              << ",\"committed\":" << quoted(state.committed)
              << ",\"unexpected_character_commits\":" << quoted(state.character_commits)
              << ",\"ime_error\":" << quoted(state.error)
              << ",\"composition_observed\":" << (composition_observed ? "true":"false")
              << ",\"requested_position_roundtrip\":" << (positioned && position_roundtrip ? "true" : "false")
              << ",\"actual_candidate_position_verified\":" << (candidate_moved ? "true":"false")
              << ",\"commit_verified\":" << (commit_verified ? "true":"false")
              << ",\"cancel_verified\":" << (cancel_verified ? "true":"false")
              << ",\"real_ime_workflow_verified\":" << (workflow ? "true":"false");
    state.ime.focus_lost(); const bool canceled = !state.ime.composing();
    require(state.ime.detach(),"probe HIMC destruction failed");
    require(DestroyWindow(window)!=FALSE,"probe window cleanup failed");
    require(UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,"probe class cleanup failed");
    thread->Deactivate();
    std::cout << ",\"context_cancelled\":" << (canceled ? "true" : "false") << ",\"owned_window_destroyed\":true}" << std::endl;
    return hosted_input ? (workflow ? 0 : 3) : (positioned && position_roundtrip && canceled ? 0 : 2);
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--hosted-input") {
            require_hosted_input(); hosted_input=true; isolation="disposable-hosted-input-desktop"; return run();
        }
        if (argc == 2 && std::string(argv[1]) == "--desktop-only") {
            return tf_shell::run_ime_desktop_child(45000,L"--probe-owned-desktop");
        }
        if(argc==2 && (std::string(argv[1])=="--probe-owned-desktop" || std::string(argv[1])=="--probe-owned-station")) {
            require(tf_shell::is_ime_desktop_child(),"IME probe child requires its own private desktop");
            const bool station_child=std::string(argv[1])=="--probe-owned-station";
            if(station_child) require(object_name(GetProcessWindowStation()).rfind(L"TermForgeImeStation-",0)==0,
                                      "IME probe child requires its own private station");
            isolation=station_child ? "owned-window-station" : "private-non-input-desktop";
            return run();
        }
        require(argc == 1, "usage: ime_probe [--desktop-only|--hosted-input]");
        return tf_shell::run_ime_desktop_child(45000,L"--probe-owned-station",true);
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
