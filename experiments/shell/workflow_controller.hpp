// Parent side of the data-only r108-console-v1 event/identity protocol.
// Owns events and one verified process HANDLE, never a ShellWindow or raw PID action.
#pragma once
#include "../conpty/session.hpp"
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace tf_shell_test {
enum class WorkflowPhase { output,append,evict };
class WorkflowController {
    std::array<tf_conpty::Handle,9> events_;
    tf_conpty::Handle process_;
    std::wstring prefix_;
    std::filesystem::path client_;
    unsigned started_=0,observed_generation_=0;
    DWORD process_id_=0;
    unsigned long long creation_time_=0,born_=0;
    bool bound_=false,stop_requested_=false;
    static void check(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
    static unsigned long long ticks(FILETIME value) {
        return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;
    }
    static unsigned long long nonce() {
        static std::atomic<unsigned long long> previous{0};
        auto old=previous.load();
        for(;;) { const auto next=(std::max)(static_cast<unsigned long long>(GetTickCount64()),old+1);
            if(previous.compare_exchange_weak(old,next)) return next; }
    }
    static bool signaled(HANDLE value) {
        const auto result=WaitForSingleObject(value,0);
        check(result==WAIT_OBJECT_0 || result==WAIT_TIMEOUT,"workflow controller wait failed"); return result==WAIT_OBJECT_0;
    }
    static void quoted_path(const std::filesystem::path& path) {
        const auto& value=path.native();
        check(!value.empty() && value.find_first_of(L"\"\r\n%!&|<>^")==std::wstring::npos && value.find(L'\0')==std::wstring::npos,
            "workflow controller path contains cmd metacharacters");
    }
    static unsigned index(WorkflowPhase phase) {
        const auto value=static_cast<unsigned>(phase);check(value<3,"workflow controller phase invalid");return value;
    }
    void bind() {
        tf_conpty::Handle identity(CreateFileW((path.wstring()+L".identity").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(identity.get()!=INVALID_HANDLE_VALUE,"workflow controller identity missing");
        LARGE_INTEGER size{}; DWORD data[2]{},read=0;
        check(GetFileSizeEx(identity.get(),&size) && size.QuadPart==sizeof(data) &&
            ReadFile(identity.get(),data,sizeof(data),&read,nullptr) && read==sizeof(data) && data[0] && data[0]!=GetCurrentProcessId() && data[1]==generation,
            "workflow controller PID/generation handshake invalid");
        process_.reset(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,data[0]));
        try {
            check(process_.get()!=nullptr && !signaled(process_.get()),"workflow controller child is not alive");
            BOOL owned=FALSE;check(IsProcessInJob(process_.get(),nullptr,&owned)!=FALSE && owned,"workflow controller child escaped Job containment");
            wchar_t image[32768]{}; DWORD count=32768;
            check(QueryFullProcessImageNameW(process_.get(),0,image,&count)!=FALSE && count>0,"workflow controller child image unavailable");
            const auto expected=client_.native();
            check(CompareStringOrdinal(image,static_cast<int>(count),expected.data(),static_cast<int>(expected.size()),TRUE)==CSTR_EQUAL,
                "workflow controller child image mismatch");
            FILETIME created{},exited{},kernel{},user{};
            check(GetProcessTimes(process_.get(),&created,&exited,&kernel,&user)!=FALSE && ticks(created)>=born_,
                "workflow controller child predates this generation");
            creation_time_=ticks(created);process_id_=data[0];observed_generation_=data[1];bound_=true;
        } catch(...) { process_.reset();throw; }
    }
public:
    const std::string role;
    const unsigned generation;
    const std::filesystem::path path;
    WorkflowController(std::filesystem::path capture,std::string receiver_role,unsigned identity)
        :role(std::move(receiver_role)),generation(identity),path(std::filesystem::absolute(capture).lexically_normal()) {
        check(generation>0 && generation<=999999 && (role=="A" || role=="B" || (role.size()==2 && role[0]=='F' && role[1]>='1' && role[1]<='5')),
            "workflow controller role/generation invalid");
        check(!capture.empty() && std::filesystem::is_directory(path.parent_path()),"workflow controller capture parent missing");quoted_path(path);
        for(const wchar_t* suffix:{L"",L".identity",L".receiver.json",L".source.bin",L".workflow.json",L".progress.jsonl",
            L".console.bin",L".console.json",L".cells.bin",L".transport.bin"})
            check(!std::filesystem::exists(path.wstring()+suffix),"workflow controller evidence must be fresh");
        BOOL owned=FALSE;check(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)!=FALSE && owned,"workflow controller requires an owned Job");
        wchar_t image[32768]{};const auto count=GetModuleFileNameW(nullptr,image,32768);
        check(count && count<32768,"workflow controller executable directory unavailable");
        client_=std::filesystem::path(image).parent_path()/L"shell_text_client.exe";quoted_path(client_);
        check(std::filesystem::is_regular_file(client_),"workflow controller text client missing");
        FILETIME born{};GetSystemTimeAsFileTime(&born);born_=ticks(born);
        std::wstring wide_role;for(char c:role) wide_role+=static_cast<wchar_t>(c);
        prefix_=L"Local\\TermForgeWorkflow-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(nonce())+L"-"+std::to_wstring(generation)+L"-"+wide_role;
        constexpr const wchar_t* names[]{L"ready",L"output",L"output-done",L"append",L"append-done",L"evict",L"evict-done",L"stop",L"stopped"};
        for(size_t i=0;i<events_.size();++i) {
            const auto event=CreateEventW(nullptr,TRUE,FALSE,(prefix_+L"-"+names[i]).c_str());const auto error=GetLastError();events_[i].reset(event);
            check(event && error!=ERROR_ALREADY_EXISTS,"workflow controller event collision/creation failure");
        }
    }
    WorkflowController(const WorkflowController&)=delete;
    WorkflowController& operator=(const WorkflowController&)=delete;
    // Best-effort own-event stop only. The caller explicitly observes join and
    // retains its containing Job; no callback can reach a destroyed window.
    ~WorkflowController() { if(events_[7].get()) SetEvent(events_[7].get()); }
    std::wstring arguments() const {
        std::wstring wide_role;for(char c:role) wide_role+=static_cast<wchar_t>(c);
        return L"--workflow-v1 --capture \""+path.wstring()+L"\" --events \""+prefix_+L"\" --generation "+std::to_wstring(generation)+L" --role "+wide_role;
    }
    std::wstring command() const {
        auto value=L"\""+client_.wstring()+L"\" "+arguments()+L"\r";check(value.size()<=4096,"workflow controller command exceeds bound");return value;
    }
    // Readiness acknowledges the handshake once; process_signaled is the
    // separate liveness observation, including after an owning target closes.
    bool ready() { if(bound_) return true;if(!signaled(events_[0].get())) return false;bind();return true; }
    bool bound() const noexcept { return bound_; }
    unsigned observed_generation() const noexcept { return observed_generation_; }
    DWORD process_id() const noexcept { return process_id_; }
    unsigned long long creation_time() const noexcept { return creation_time_; }
    bool process_signaled() const { return bound_ && signaled(process_.get()); }
    DWORD exit_code() const {
        DWORD value=STILL_ACTIVE;check(bound_ && GetExitCodeProcess(process_.get(),&value),"workflow controller child exit code unavailable");return value;
    }
    void start(WorkflowPhase phase) {
        const auto value=index(phase);
        check(bound_ && !process_signaled() && !stop_requested_,"workflow controller target is not running");
        check(role!="B" && (role=="A" || value==0) && value==started_,"workflow controller phase order invalid");
        check(!started_ || done(static_cast<WorkflowPhase>(started_-1)),"workflow controller previous phase incomplete");
        check(!signaled(events_[2+value*2].get()),"workflow controller completion was already signaled");
        check(SetEvent(events_[1+value*2].get())!=FALSE,"workflow controller phase signal failed");++started_;
    }
    bool done(WorkflowPhase phase) const { const auto value=index(phase);return value<started_ && signaled(events_[2+value*2].get()); }
    void stop() { check(SetEvent(events_[7].get())!=FALSE,"workflow controller stop signal failed");stop_requested_=true; }
    bool stop_requested() const noexcept { return stop_requested_; }
    bool stopped_event() const { return signaled(events_[8].get()); }
    bool join(unsigned timeout_ms=3000) const {
        check(timeout_ms>0 && timeout_ms<=3000,"workflow controller join timeout exceeds bound");if(!bound_) return false;
        const auto state=WaitForSingleObject(process_.get(),timeout_ms);check(state==WAIT_OBJECT_0 || state==WAIT_TIMEOUT,"workflow controller child join failed");return state==WAIT_OBJECT_0;
    }
    bool finished() const {
        if(!process_signaled()) return false;
        const auto code=exit_code();if(code) throw std::runtime_error("workflow controller "+role+" exited="+std::to_string(code));
        return stopped_event();
    }
    std::string bytes() const {
        std::ifstream input(path,std::ios::binary|std::ios::ate);check(input.good(),"workflow controller capture missing");
        const auto length=input.tellg();check(length>=0 && length<=16384,"workflow controller capture exceeds bound");
        std::string value(static_cast<size_t>(length),0);input.seekg(0);if(!value.empty()) input.read(value.data(),length);
        check(input.good(),"workflow controller capture read failed");return value;
    }
};
} // namespace tf_shell_test
