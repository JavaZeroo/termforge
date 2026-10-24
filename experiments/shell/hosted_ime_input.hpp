// Test-only real input driver. No caller may opt out of the hosted/input guards.
#pragma once
#include "ime.hpp"
#include "ime_input_expectation.hpp"
#include "../conpty/session.hpp"
#include <msctf.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace tf_shell_test {
inline void ime_check(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
inline std::string ime_utf8(const std::wstring& value) {
    if(value.empty()) return {};
    ime_check(value.size()<=4096,"IME evidence string exceeds bound");
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    ime_check(size>0,"IME evidence UTF-16 is invalid"); std::string out(static_cast<size_t>(size),0);
    ime_check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),size,nullptr,nullptr)==size,"IME evidence conversion failed");
    return out;
}
inline std::string ime_quote(const std::string& value) {
    std::string out="\""; constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value) {
        if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else { if(c=='"' || c=='\\') out+='\\'; out+=static_cast<char>(c); }
    }
    return out+'"';
}
inline void ime_rect(std::ostream& out,RECT r) { out<<'['<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<']'; }
inline std::wstring ime_object_name(HANDLE object) {
    wchar_t value[256]{}; DWORD size=0;
    ime_check(GetUserObjectInformationW(object,UOI_NAME,value,sizeof(value),&size)!=FALSE,"input desktop identity query failed");
    return value;
}
inline bool ime_environment(const char* name,const char* expected) {
    char value[256]{}; const DWORD size=GetEnvironmentVariableA(name,value,sizeof(value));
    return size && size<sizeof(value) && std::string(value,size)==expected;
}
inline std::wstring require_hosted_ime_input() {
    ime_check(ime_environment("GITHUB_ACTIONS","true") && ime_environment("RUNNER_ENVIRONMENT","github-hosted") &&
        ime_environment("RUNNER_OS","Windows") && ime_environment("GITHUB_REPOSITORY","JavaZeroo/termforge") &&
        (ime_environment("ImageOS","win22") || ime_environment("ImageOS","win25") || ime_environment("ImageOS","win25-vs2026")),
        "real IME input requires the designated disposable GitHub-hosted Windows VM");
    char version[96]{}; const DWORD length=GetEnvironmentVariableA("ImageVersion",version,sizeof(version));
    ime_check(length>0 && length<sizeof(version),"hosted image version missing");
    for(DWORD i=0;i<length;++i) ime_check((version[i]>='0' && version[i]<='9') || version[i]=='.',"invalid hosted image version");
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    ime_check(input!=nullptr,"hosted input desktop unavailable");
    std::wstring name;
    try { name=ime_object_name(input); } catch(...) { CloseDesktop(input); throw; }
    ime_check(CloseDesktop(input)!=FALSE,"input desktop query handle close failed");
    ime_check(ime_object_name(GetProcessWindowStation())==L"WinSta0" &&
        name==ime_object_name(GetThreadDesktop(GetCurrentThreadId())),"test thread is not on WinSta0 input desktop");
    BOOL in_job=FALSE;
    ime_check(IsProcessInJob(GetCurrentProcess(),nullptr,&in_job)!=FALSE && in_job,"real input requires an owning supervisor Job");
    return name;
}
inline bool is_product_ime_mode(const std::wstring& mode) {
    return mode==L"ui-ime-basic" || mode==L"ui-ime-geometry" || mode==L"ui-ime-focus" || mode==L"ui-ime-close" || mode==L"ui-ime-reading";
}
inline bool is_hosted_workflow_mode(const std::wstring& mode) { return mode==L"ui-workflow-system"; }
inline void require_hosted_ime_child_gate() {
    // The launcher resumes us only after inner Job assignment. No IME/profile
    // activation or window creation occurs until this second exact GO gate.
    char bytes[4]{}; size_t count=0;
    while(count<sizeof(bytes)) {
        DWORD received=0;
        ime_check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),bytes+count,1,&received,nullptr)!=FALSE && received==1,"hosted IME child gate closed");
        if(bytes[count++]=='\n') break;
    }
    ime_check(std::string_view(bytes,count)=="GO\n" || std::string_view(bytes,count)=="GO\r\n","hosted IME child requires inner Job GO gate");
    require_hosted_ime_input();
}

// Per-ShellWindow by-value evidence, disabled outside an explicit hosted test.
// Counts describe real received messages; receiver bytes remain a separate oracle.
struct HostedImeObservation {
    bool enabled=false, composing=false, caret_positioned=false;
    unsigned generation=0, composition_events=0, result_events=0, character_events=0;
    unsigned caret_updates=0;
    std::wstring preedit;
    std::string committed,error;
    std::vector<unsigned> character_units;
    RECT caret_client{},caret_screen{};
    void begin(unsigned identity) {
        require_hosted_ime_input(); ime_check(identity!=0,"IME observation generation must be nonzero");
        *this={}; enabled=true; generation=identity;
    }
    void message(UINT message,WPARAM wp,LPARAM lp,const tf_shell::ImeMessage& result) {
        if(!enabled) return;
        if(message==WM_IME_COMPOSITION) { ++composition_events; if(lp&GCS_RESULTSTR) ++result_events; }
        if(message==WM_CHAR || message==WM_SYSCHAR) {
            ++character_events; ime_check(character_units.size()<256,"IME WM_CHAR evidence overflow");
            character_units.push_back(static_cast<unsigned>(wp));
        }
        ime_check(result.committed.size()<=4096-committed.size(),"IME commit evidence overflow");
        committed+=result.committed; if(!result.error.empty()) error=result.error;
    }
    void caret(HWND window,RECT value,bool positioned) {
        if(!enabled) return;
        caret_client=value; POINT points[2]{{value.left,value.top},{value.right,value.bottom}};
        ime_check(ClientToScreen(window,&points[0]) && ClientToScreen(window,&points[1]),"IME screen caret mapping failed");
        caret_screen={points[0].x,points[0].y,points[1].x,points[1].y}; caret_positioned=positioned; ++caret_updates;
    }
    HostedImeObservation snapshot(const tf_shell::ImeContext& context,HWND window) const {
        auto copy=*this; copy.composing=context.composing(); copy.preedit=context.preedit();
        if(enabled && window && IsWindow(window)) {
            POINT points[2]{{caret_client.left,caret_client.top},{caret_client.right,caret_client.bottom}};
            ime_check(ClientToScreen(window,&points[0]) && ClientToScreen(window,&points[1]),"IME snapshot screen mapping failed");
            copy.caret_screen={points[0].x,points[0].y,points[1].x,points[1].y};
        }
        return copy;
    }
};

class HostedPinyin {
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_;
    bool com_=false,active_=false;
public:
    HRESULT language_result=E_FAIL,activation_result=E_FAIL;
    HostedPinyin() {
        require_hosted_ime_input();
        const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        ime_check(SUCCEEDED(initialized),"product IME COM initialization failed"); com_=true;
        try {
            ime_check(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread_))),"product TSF thread manager unavailable");
            TfClientId client=0; ime_check(SUCCEEDED(thread_->Activate(&client)),"product TSF activation failed"); active_=true;
            Microsoft::WRL::ComPtr<ITfInputProcessorProfiles> profiles;
            ime_check(SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles))),"product TSF profiles unavailable");
            const CLSID provider{0x81d4e9c9,0x1d3b,0x41bc,{0x9e,0x6c,0x4b,0x40,0xbf,0x79,0xe3,0x5e}};
            const GUID profile{0xfa550b04,0x5ad7,0x411f,{0xa5,0xac,0xca,0x03,0x8e,0xc5,0x15,0xd7}};
            BOOL enabled=FALSE;
            ime_check(SUCCEEDED(profiles->IsEnabledLanguageProfile(provider,0x0804,profile,&enabled)) && enabled,"fixed Microsoft Pinyin profile is not enabled");
            language_result=profiles->ChangeCurrentLanguage(0x0804);
            Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> manager;
            ime_check(SUCCEEDED(profiles.As(&manager)),"TSF profile manager unavailable");
            activation_result=manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x0804,provider,profile,nullptr,TF_IPPMF_FORPROCESS);
            ime_check(SUCCEEDED(language_result) && SUCCEEDED(activation_result),"fixed Pinyin activation failed");
        } catch(...) { if(active_) thread_->Deactivate(); thread_.Reset(); CoUninitialize(); com_=false; throw; }
    }
    ~HostedPinyin() { if(active_) thread_->Deactivate(); thread_.Reset(); if(com_) CoUninitialize(); }
    HostedPinyin(const HostedPinyin&)=delete;
    HostedPinyin& operator=(const HostedPinyin&)=delete;
    static void focus(HWND target) {
        require_hosted_ime_input(); DWORD pid=0;
        const auto tid=GetWindowThreadProcessId(target,&pid);
        ime_check(IsWindow(target) && IsWindowVisible(target) && tid==GetCurrentThreadId() && pid==GetCurrentProcessId(),"IME target must be an own visible UI-thread control");
        HWND root=GetAncestor(target,GA_ROOT);
        ime_check(root && (GetForegroundWindow()==root || SetForegroundWindow(root)!=FALSE),"cannot foreground own IME workspace");
        SetActiveWindow(root); SetFocus(target);
        ime_check(GetForegroundWindow()==root && GetFocus()==target,"own IME target focus not established");
        HIMC context=ImmGetContext(target); ime_check(context!=nullptr,"focused product target has no HIMC");
        const bool open=ImmSetOpenStatus(context,TRUE)!=FALSE;
        const bool conversion=ImmSetConversionStatus(context,IME_CMODE_NATIVE,IME_SMODE_NONE)!=FALSE;
        const bool released=ImmReleaseContext(target,context)!=FALSE;
        ime_check(open && conversion && released,"focused target Pinyin native mode failed");
    }
    static void key(HWND target,unsigned expected_generation,unsigned actual_generation,WORD key) {
        require_hosted_ime_input(); DWORD pid=0;
        const auto tid=GetWindowThreadProcessId(target,&pid);
        ime_check(expected_generation!=0 && expected_generation==actual_generation && IsWindow(target) && IsWindowVisible(target) &&
            tid==GetCurrentThreadId() && pid==GetCurrentProcessId() && GetForegroundWindow()==GetAncestor(target,GA_ROOT) && GetFocus()==target,
            "IME key batch lost its own foreground/root/focus/generation");
        for(int modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN})
            ime_check((GetAsyncKeyState(modifier)&0x8000)==0,"IME key injection rejected active modifier");
        INPUT input[2]{}; input[0].type=input[1].type=INPUT_KEYBOARD;
        input[0].ki.wVk=input[1].ki.wVk=key; input[1].ki.dwFlags=KEYEVENTF_KEYUP;
        ime_check(SendInput(2,input,sizeof(INPUT))==2,"guarded product IME SendInput failed");
    }
};

struct HostedCandidate { unsigned instance=0; std::string class_name; RECT rectangle{}; HWND native=nullptr; };
class HostedCandidateObserver {
    std::map<HWND,unsigned> identities_;
    unsigned next_=1;
    struct Enumeration { std::vector<HostedCandidate> rows; bool failed=false; };
    static BOOL CALLBACK enumerate(HWND window,LPARAM data) noexcept {
      auto& state=*reinterpret_cast<Enumeration*>(data);
      try {
        auto& result=state.rows;
        wchar_t name[256]{}; GetClassNameW(window,name,256); const std::wstring cls(name);
        if(IsWindowVisible(window) && (cls.find(L"Candidate")!=std::wstring::npos || cls.find(L"candidate")!=std::wstring::npos || cls.find(L"IME")!=std::wstring::npos)) {
            ime_check(result.size()<32,"candidate evidence exceeds window bound");
            RECT rect{}; if(GetWindowRect(window,&rect)) result.push_back({0,ime_utf8(cls),rect,window});
        }
        return TRUE;
      } catch(...) { state.failed=true; return FALSE; }
    }
public:
    std::vector<HostedCandidate> sample() {
        require_hosted_ime_input(); Enumeration state;
        ime_check(EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),enumerate,reinterpret_cast<LPARAM>(&state))!=FALSE && !state.failed,"candidate window enumeration failed");
        auto result=std::move(state.rows);
        for(auto& row:result) { const auto found=identities_.find(row.native); if(found==identities_.end()) row.instance=identities_.emplace(row.native,next_++).first->second; else row.instance=found->second; }
        return result;
    }
    static bool candidate(const HostedCandidate& row) { return row.class_name.find("Candidate")!=std::string::npos || row.class_name.find("candidate")!=std::string::npos; }
    static bool moved(const std::vector<HostedCandidate>& before,const std::vector<HostedCandidate>& after,RECT caret_before,RECT caret_after) {
        const LONG dx=caret_after.left-caret_before.left,dy=caret_after.bottom-caret_before.bottom;
        if(!dx && !dy) return false;
        for(const auto& a:before) for(const auto& b:after)
            if(candidate(a) && a.instance==b.instance && a.class_name==b.class_name &&
                std::abs((b.rectangle.left-a.rectangle.left)-dx)<=8 && std::abs((b.rectangle.top-a.rectangle.top)-dy)<=8) return true;
        return false;
    }
};

inline std::string ime_hex(const std::string& bytes) {
    constexpr char hex[]="0123456789abcdef"; std::string out; out.reserve(bytes.size()*2);
    for(unsigned char c:bytes) { out+=hex[c>>4]; out+=hex[c&15]; } return out;
}
class HostedImeReceiver {
    tf_conpty::Handle ready_,output_,output_done_,stop_,stopped_,process_;
    static void event(tf_conpty::Handle& handle,const std::wstring& name) {
        handle.reset(CreateEventW(nullptr,TRUE,FALSE,name.c_str()));
        ime_check(handle.get() && GetLastError()!=ERROR_ALREADY_EXISTS,"IME gate name collision/creation failure");
    }
    static bool signaled(HANDLE handle) {
        const auto state=WaitForSingleObject(handle,0);
        ime_check(state==WAIT_OBJECT_0 || state==WAIT_TIMEOUT,"IME gate wait failed"); return state==WAIT_OBJECT_0;
    }
public:
    std::wstring prefix;
    std::string name;
    unsigned generation;
    std::filesystem::path path;
    ImeInputExpectation expected_input;
    bool stopped_by_close=false;
    HostedImeReceiver(std::filesystem::path capture,std::string receiver,unsigned identity,std::string accepted_input_prefix={})
        :name(std::move(receiver)),generation(identity),path(std::move(capture)),expected_input(std::move(accepted_input_prefix)) {
        require_hosted_ime_input(); ime_check(!std::filesystem::exists(path),"IME capture path already exists");
        prefix=L"Local\\TermForgeIme-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(generation);
        event(ready_,prefix+L"-ready"); event(output_,prefix+L"-output"); event(output_done_,prefix+L"-output-done");
        event(stop_,prefix+L"-stop"); event(stopped_,prefix+L"-stopped");
    }
    std::wstring command() const {
        wchar_t module[32768]{}; const DWORD count=GetModuleFileNameW(nullptr,module,32768);
        ime_check(count && count<32768,"IME receiver path unavailable");
        const auto client=std::filesystem::path(module).parent_path()/L"shell_ime_client.exe";
        // Paths are task-owned generated values, never shell-input content.
        for(const auto* value:{&path.native(),&client.native()})
            ime_check(value->find_first_of(L"\"\r\n%!") == std::wstring::npos,"IME fixture path contains cmd metacharacters");
        return L"\""+client.wstring()+L"\" --capture \""+path.wstring()+L"\" --events \""+prefix+L"\" --generation "+std::to_wstring(generation)+L"\r";
    }
    bool ready() {
        if(!signaled(ready_.get())) return false;
        if(!process_.get()) {
            std::ifstream in(path.wstring()+L".identity",std::ios::binary); DWORD data[2]{};
            in.read(reinterpret_cast<char*>(data),sizeof(data));
            ime_check(in.gcount()==sizeof(data) && in.peek()==EOF && data[0] && data[1]==generation,"IME receiver identity handshake invalid");
            process_.reset(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,data[0]));
            ime_check(process_.get()!=nullptr,"IME receiver process handle unavailable"); BOOL in_job=FALSE;
            ime_check(IsProcessInJob(process_.get(),nullptr,&in_job)!=FALSE && in_job,"IME receiver escaped process containment");
            wchar_t image[32768]{}; DWORD size=32768;
            ime_check(QueryFullProcessImageNameW(process_.get(),0,image,&size)!=FALSE &&
                std::filesystem::path(image).filename()==L"shell_ime_client.exe","IME handshake process image mismatch");
        }
        return true;
    }
    std::string bytes() const {
        std::ifstream input(path,std::ios::binary|std::ios::ate); ime_check(input.good(),"IME capture missing");
        const auto length=input.tellg(); ime_check(length>=0 && length<=16384,"IME receiver capture exceeds bound");
        std::string bytes(static_cast<size_t>(length),0); input.seekg(0);
        if(!bytes.empty()) input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        ime_check(input.good(),"IME receiver capture read failed"); return bytes;
    }
    void emit_output() { ime_check(SetEvent(output_.get())!=FALSE,"IME output event failed"); }
    bool output_done() const { return signaled(output_done_.get()); }
    void stop() { ime_check(process_.get()!=nullptr,"IME receiver not bound before stop"); ime_check(SetEvent(stop_.get())!=FALSE,"IME stop event failed"); }
    bool process_signaled() const { return process_.get() && signaled(process_.get()); }
    bool stopped_event() const { return signaled(stopped_.get()); }
    DWORD exit_code() const { DWORD code=STILL_ACTIVE; ime_check(process_.get() && GetExitCodeProcess(process_.get(),&code),"IME receiver exit code unavailable"); return code; }
    bool finished() const {
        if(!process_signaled()) return false;
        const auto code=exit_code();
        if(code!=0) {
            std::string diagnostic="missing";
            std::ifstream input(path.wstring()+L".receiver.json",std::ios::binary|std::ios::ate);
            if(input.good()) {
                const auto size=input.tellg();
                if(size>=0 && size<=4096) {
                    diagnostic.assign(static_cast<size_t>(size),0); input.seekg(0);
                    if(!diagnostic.empty()) input.read(diagnostic.data(),static_cast<std::streamsize>(diagnostic.size()));
                    if(!input.good()) diagnostic="read-failed";
                } else diagnostic="invalid-size";
            }
            throw std::runtime_error("IME receiver "+name+" exited="+std::to_string(code)+" stopped_event="+
                (stopped_event()?"true":"false")+" diagnostic="+diagnostic);
        }
        return stopped_event();
    }
};
struct HostedImeCheckpoint {
    std::string name,session,received;
    HostedImeObservation observation;
    std::vector<HostedCandidate> candidates;
    int display_dpi=0,zoom_percent=0;
    std::pair<int,int> cell_pixels{};
    RECT monitor_work{};
    UINT os_dpi=0;
};
struct HostedImeTextContext {
    std::string name,focus,main_received,bottom_received;
    size_t top=0,maximum=0;
    bool following=false;
    int cursor_row=-1,rows=0,columns=0;
    uint64_t first_line_id=0;
    bool find_present=false,find_visible=false,find_focused=false,edit_destroyed=false;
    std::string edit_class,edit_text,edit_preedit;
    std::vector<HostedCandidate> candidates;
    bool has_match=false,search_complete=false;
};
class ProductImeScenarioState {
    struct Step { std::string name; std::function<void()> action; std::function<bool()> ready; unsigned minimum_ms; ULONGLONG begun=0; };
    std::vector<Step> steps_;
    size_t step_=0;
    unsigned progress_records_=0;
    void progress(const std::string& record) {
        ime_check(!progress_path.empty() && ++progress_records_<=320,"IME progress evidence bound exceeded/not configured");
        std::ofstream out(progress_path,std::ios::binary|std::ios::app); out<<record<<'\n'; out.close();
        ime_check(out.good(),"IME progress evidence write failed");
    }
    static void write_context(std::ostream& out,const HostedImeTextContext& row) {
        out<<"{\"name\":"<<ime_quote(row.name)<<",\"focus\":"<<ime_quote(row.focus)
           <<",\"main_view\":{\"top\":"<<row.top<<",\"maximum\":"<<row.maximum<<",\"following\":"<<(row.following?"true":"false")
           <<",\"cursor_row\":"<<row.cursor_row<<",\"rows\":"<<row.rows<<",\"columns\":"<<row.columns<<",\"first_line_id\":"<<row.first_line_id<<'}'
           <<",\"main_received_hex\":"<<ime_quote(ime_hex(row.main_received))<<",\"bottom_received_hex\":"<<ime_quote(ime_hex(row.bottom_received))
           <<",\"find\":{\"present\":"<<(row.find_present?"true":"false")<<",\"visible\":"<<(row.find_visible?"true":"false")
           <<",\"focused\":"<<(row.find_focused?"true":"false")<<",\"edit_destroyed\":"<<(row.edit_destroyed?"true":"false")
           <<",\"class\":"<<ime_quote(row.edit_class)<<",\"text\":"<<ime_quote(row.edit_text)<<",\"preedit\":"<<ime_quote(row.edit_preedit)
           <<",\"has_match\":"<<(row.has_match?"true":"false")<<",\"search_complete\":"<<(row.search_complete?"true":"false")<<"},\"visible_candidates\":[";
        for(size_t i=0;i<row.candidates.size();++i) {
            if(i) out<<','; const auto& candidate=row.candidates[i];
            out<<"{\"instance\":"<<candidate.instance<<",\"class\":"<<ime_quote(candidate.class_name)<<",\"rect\":";
            ime_rect(out,candidate.rectangle); out<<'}';
        }
        out<<"]}";
    }
public:
    std::filesystem::path progress_path;
    std::unique_ptr<HostedPinyin> provider;
    HostedCandidateObserver candidate_observer;
    std::vector<std::unique_ptr<HostedImeReceiver>> receivers;
    std::vector<HostedImeCheckpoint> checkpoints;
    std::map<std::string,bool> actions;
    std::map<std::string,std::string> terminal_output;
    std::vector<std::string> output_screen_lines;
    std::vector<HostedImeTextContext> reading_context;
    HWND reading_edit=nullptr;
    std::string rename_preedit,rename_text;
    std::string rename_cancel_preedit,rename_cancelled_text;
    HWND rename_cancel_editor=nullptr;
    bool rename_first_escape_kept_editor=false,rename_second_escape_closed_editor=false;
    std::vector<HostedCandidate> rename_cancel_candidates,rename_after_cancel_candidates;
    std::vector<HostedCandidate> rename_candidates;
    std::vector<HostedCandidate> after_close_candidates;
    unsigned key_batches=0;
    HWND closed_target=nullptr;
    ULONGLONG started=GetTickCount64();
    void add(std::string name,std::function<void()> action,std::function<bool()> ready=[] { return true; },unsigned minimum_ms=100) {
        ime_check(steps_.size()<128,"IME scenario step budget exceeded"); steps_.push_back({std::move(name),std::move(action),std::move(ready),minimum_ms});
    }
    bool tick() {
        ime_check(GetTickCount64()-started<45000,"product IME scenario exceeded 45 seconds");
        if(step_==steps_.size()) return true;
        auto& step=steps_[step_];
        if(!step.begun) {
            step.begun=GetTickCount64();
            progress("{\"kind\":\"step\",\"state\":\"started\",\"name\":"+ime_quote(step.name)+",\"elapsed_ms\":"+std::to_string(step.begun-started)+"}");
            step.action(); return false;
        }
        const auto elapsed=GetTickCount64()-step.begun;
        if(elapsed>=step.minimum_ms && step.ready()) {
            progress("{\"kind\":\"step\",\"state\":\"ended\",\"name\":"+ime_quote(step.name)+",\"elapsed_ms\":"+std::to_string(GetTickCount64()-started)+"}");
            ++step_; return step_==steps_.size();
        }
        if(elapsed>5000) throw std::runtime_error("product IME step timed out: "+step.name);
        return false;
    }
    void key(HWND target,unsigned generation,unsigned actual,WORD code) { HostedPinyin::key(target,generation,actual,code); ++key_batches; }
    void text_context(HostedImeTextContext row) {
        ime_check(reading_context.size()<16 && row.name.size()<=64 && row.edit_text.size()<=4096 && row.edit_preedit.size()<=4096 &&
                  row.main_received.size()<=4096 && row.bottom_received.size()<=4096 && row.candidates.size()<=32,
                  "IME text context evidence exceeds bound");
        std::ostringstream out; out<<"{\"kind\":\"text-context\",\"observation\":"; write_context(out,row); out<<'}';
        progress(out.str()); reading_context.push_back(std::move(row));
    }
    void checkpoint(std::string name,HostedImeReceiver& receiver,HostedImeObservation observation,int dpi,int zoom,std::pair<int,int> cell,HWND window) {
        ime_check(checkpoints.size()<32 && observation.enabled && observation.generation==receiver.generation,"IME checkpoint generation mismatch/budget exceeded");
        ime_check(observation.error.empty(),"product IME reported an error");
        checkpoints.push_back({std::move(name),receiver.name,receiver.bytes(),std::move(observation),candidate_observer.sample(),dpi,zoom,cell});
        auto& row=checkpoints.back(); const auto& obs=row.observation;
        MONITORINFO monitor{sizeof(monitor)};
        ime_check(GetMonitorInfoW(MonitorFromRect(&obs.caret_screen,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,"IME caret monitor work area unavailable");
        row.monitor_work=monitor.rcWork; row.os_dpi=GetDpiForWindow(window);
        ime_check(row.os_dpi!=0,"IME target OS DPI unavailable");
        std::ostringstream out;
        out<<"{\"kind\":\"checkpoint\",\"name\":"<<ime_quote(row.name)<<",\"session\":"<<ime_quote(row.session)
           <<",\"generation\":"<<obs.generation<<",\"composing\":"<<(obs.composing?"true":"false")<<",\"preedit\":"<<ime_quote(ime_utf8(obs.preedit))
           <<",\"result_events\":"<<obs.result_events<<",\"committed\":"<<ime_quote(obs.committed)<<",\"received_hex\":"<<ime_quote(ime_hex(row.received))<<",\"caret_screen\":";
        ime_rect(out,obs.caret_screen); out<<",\"candidates\":[";
        for(size_t i=0;i<row.candidates.size();++i) {
            if(i) out<<','; const auto& candidate=row.candidates[i];
            out<<"{\"instance\":"<<candidate.instance<<",\"class\":"<<ime_quote(candidate.class_name)<<",\"rect\":"; ime_rect(out,candidate.rectangle); out<<'}';
        }
        out<<"]}"; progress(out.str());
    }
    const HostedImeCheckpoint& find(const std::string& name) const {
        for(const auto& row:checkpoints) if(row.name==name) return row;
        throw std::runtime_error("missing IME checkpoint");
    }
    void require_moved(const std::string& before,const std::string& after) const {
        const auto& a=find(before); const auto& b=find(after);
        ime_check(HostedCandidateObserver::moved(a.candidates,b.candidates,a.observation.caret_screen,b.observation.caret_screen),"real candidate did not follow physical product caret");
    }
    void save(const std::filesystem::path& path,const std::string& mode) const {
        ime_check(provider!=nullptr && key_batches>0,"product IME scenario not initialized");
        std::ofstream out(path,std::ios::binary);
        const auto desktop=require_hosted_ime_input();
        out<<"{\"schema\":\"termforge-product-ime-v1\",\"mode\":"<<ime_quote(mode)
           <<",\"input_desktop\":{\"station\":\"WinSta0\",\"thread_name\":"<<ime_quote(ime_utf8(desktop))<<",\"input_name\":"<<ime_quote(ime_utf8(desktop))<<",\"query_only\":true}"
           <<",\"provider\":{\"clsid\":\"{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}\",\"profile\":\"{FA550B04-5AD7-411F-A5AC-CA038EC515D7}\",\"langid\":2052,\"language_hresult\":"
           <<provider->language_result<<",\"activation_hresult\":"<<provider->activation_result<<"},\"key_batches\":"<<key_batches<<",\"guarded_key_batches\":"<<key_batches<<",\"checkpoints\":[";
        for(size_t i=0;i<checkpoints.size();++i) {
            if(i) out<<','; const auto& row=checkpoints[i]; const auto& obs=row.observation;
            out<<"{\"name\":"<<ime_quote(row.name)<<",\"session\":"<<ime_quote(row.session)<<",\"generation\":"<<obs.generation
               <<",\"composing\":"<<(obs.composing?"true":"false")<<",\"preedit\":"<<ime_quote(ime_utf8(obs.preedit))
               <<",\"display_dpi\":"<<row.display_dpi<<",\"zoom_percent\":"<<row.zoom_percent<<",\"cell_pixels\":["<<row.cell_pixels.first<<','<<row.cell_pixels.second<<']'
               <<",\"composition_events\":"<<obs.composition_events<<",\"result_events\":"<<obs.result_events<<",\"character_units\":[";
            for(size_t j=0;j<obs.character_units.size();++j) { if(j) out<<','; out<<obs.character_units[j]; }
            out<<"],\"committed\":"<<ime_quote(obs.committed)<<",\"ime_error\":"<<ime_quote(obs.error)<<",\"caret_client\":"; ime_rect(out,obs.caret_client);
            out<<",\"caret_screen\":"; ime_rect(out,obs.caret_screen);
            out<<",\"monitor_work\":"; ime_rect(out,row.monitor_work); out<<",\"os_dpi\":"<<row.os_dpi;
            out<<",\"caret_positioned\":"<<(obs.caret_positioned?"true":"false")<<",\"caret_updates\":"<<obs.caret_updates<<",\"candidates\":[";
            for(size_t j=0;j<row.candidates.size();++j) {
                if(j) out<<','; const auto& candidate=row.candidates[j];
                out<<"{\"instance\":"<<candidate.instance<<",\"class\":"<<ime_quote(candidate.class_name)<<",\"rect\":"; ime_rect(out,candidate.rectangle); out<<'}';
            }
            out<<"],\"received_hex\":"<<ime_quote(ime_hex(row.received))<<'}';
        }
        out<<"],\"receivers\":[";
        for(size_t i=0;i<receivers.size();++i) {
            if(i) out<<','; const auto& row=*receivers[i]; const auto bytes=row.bytes();
            ime_check(row.process_signaled() && row.exit_code()!=STILL_ACTIVE && (row.stopped_by_close || row.finished()),"IME receiver cleanup is incomplete");
            out<<"{\"name\":"<<ime_quote(row.name)<<",\"generation\":"<<row.generation<<",\"capture\":"<<ime_quote(row.path.filename().string())
               <<",\"bytes\":"<<bytes.size()<<",\"hex\":"<<ime_quote(ime_hex(bytes))<<",\"expected_prefix_hex\":"<<ime_quote(ime_hex(row.expected_input.prefix()))
               <<",\"stopped_event\":"<<(row.stopped_event()?"true":"false")
               <<",\"stopped_by_close\":"<<(row.stopped_by_close?"true":"false")<<",\"process_signaled\":true,\"exit_code\":"<<row.exit_code()<<'}';
        }
        out<<"],\"rename\":{\"preedit\":"<<ime_quote(rename_preedit)<<",\"text\":"<<ime_quote(rename_text)<<",\"candidates\":[";
        for(size_t i=0;i<rename_candidates.size();++i) {
            if(i) out<<','; const auto& row=rename_candidates[i]; out<<"{\"instance\":"<<row.instance<<",\"class\":"<<ime_quote(row.class_name)<<",\"rect\":"; ime_rect(out,row.rectangle); out<<'}';
        }
        out<<"],\"cancel_preedit\":"<<ime_quote(rename_cancel_preedit)<<",\"cancelled_text\":"<<ime_quote(rename_cancelled_text)
           <<",\"first_escape_kept_editor\":"<<(rename_first_escape_kept_editor?"true":"false")
           <<",\"second_escape_closed_editor\":"<<(rename_second_escape_closed_editor?"true":"false")<<",\"cancel_candidates\":[";
        for(size_t i=0;i<rename_cancel_candidates.size();++i) {
            if(i) out<<','; const auto& row=rename_cancel_candidates[i]; out<<"{\"instance\":"<<row.instance<<",\"class\":"<<ime_quote(row.class_name)<<",\"rect\":"; ime_rect(out,row.rectangle); out<<'}';
        }
        out<<"],\"after_cancel_candidates\":[";
        for(size_t i=0;i<rename_after_cancel_candidates.size();++i) {
            if(i) out<<','; const auto& row=rename_after_cancel_candidates[i]; out<<"{\"instance\":"<<row.instance<<",\"class\":"<<ime_quote(row.class_name)<<",\"rect\":"; ime_rect(out,row.rectangle); out<<'}';
        }
        out<<"]},\"after_close_candidates\":[";
        for(size_t i=0;i<after_close_candidates.size();++i) {
            if(i) out<<','; const auto& row=after_close_candidates[i]; out<<"{\"instance\":"<<row.instance<<",\"class\":"<<ime_quote(row.class_name)<<",\"rect\":"; ime_rect(out,row.rectangle); out<<'}';
        }
        out<<"],\"terminal_output\":{"; bool first=true;
        for(const auto& output:terminal_output) {
            ime_check(output.second.size()<=16384,"IME terminal output evidence exceeds bound");
            if(!first) out<<','; first=false; out<<ime_quote(output.first)<<':'<<ime_quote(ime_hex(output.second));
        }
        out<<"},\"output_screen_lines\":[";
        for(size_t i=0;i<output_screen_lines.size();++i) { if(i) out<<','; out<<ime_quote(output_screen_lines[i]); }
        out<<"],\"reading_context\":[";
        for(size_t i=0;i<reading_context.size();++i) { if(i) out<<','; write_context(out,reading_context[i]); }
        out<<"],\"actions\":{"; first=true;
        for(const auto& action:actions) { if(!first) out<<','; first=false; out<<ime_quote(action.first)<<':'<<(action.second?"true":"false"); }
        out<<"}}\n"; out.close(); ime_check(out.good(),"product IME manifest write failed");
    }
};
} // namespace tf_shell_test
