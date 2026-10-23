// Owned ConPTY data-only stop regression. This binary has no IME/UI driver.
#include "raw_console_receiver.hpp"
#include <iostream>

using tf_conpty::Handle;
using tf_shell_test::raw_receiver_check;
static std::wstring module_path() {
    std::wstring value(32768,0); const DWORD count=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    raw_receiver_check(count && count<value.size(),"receiver test module path failed"); value.resize(count); return value;
}
static std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate); raw_receiver_check(input.good(),"receiver test evidence missing");
    const auto size=input.tellg(); raw_receiver_check(size>=0 && size<=16384,"receiver test evidence exceeds bound");
    std::string out(static_cast<size_t>(size),0); input.seekg(0); if(!out.empty()) input.read(out.data(),size);
    raw_receiver_check(input.good(),"receiver test evidence read failed"); return out;
}
static std::string hex(const std::string& value) {
    constexpr char alphabet[]="0123456789abcdef"; std::string out;
    for(unsigned char c:value) { out+=alphabet[c>>4]; out+=alphabet[c&15]; } return out;
}
static void policy() {
    using tf_shell_test::RawReadDisposition; using tf_shell_test::raw_read_disposition;
    raw_receiver_check(raw_read_disposition(true,true,0,0)==RawReadDisposition::stopped,"requested stop must accept actual successful zero-count cancellation completion");
    raw_receiver_check(raw_read_disposition(false,true,0,0)==RawReadDisposition::failed,"unsolicited zero-count read must fail");
    raw_receiver_check(raw_read_disposition(true,false,0,ERROR_OPERATION_ABORTED)==RawReadDisposition::stopped,"requested abort must stop");
    raw_receiver_check(raw_read_disposition(false,false,0,ERROR_OPERATION_ABORTED)==RawReadDisposition::failed,"unsolicited abort must fail");
    for(const bool requested:{false,true}) {
        raw_receiver_check(raw_read_disposition(requested,true,2,0)==RawReadDisposition::data,"completed Unicode input must remain data");
        for(const DWORD error:{ERROR_INVALID_HANDLE,ERROR_BROKEN_PIPE,ERROR_INVALID_DATA,ERROR_SUCCESS})
            raw_receiver_check(raw_read_disposition(requested,false,0,error)==RawReadDisposition::failed,"stop cannot mask another read failure");
    }
    raw_receiver_check(tf_shell_test::raw_cancel_request_valid(true,0),"successful cancellation request is valid");
    raw_receiver_check(tf_shell_test::raw_cancel_request_valid(false,ERROR_NOT_FOUND),"no pending I/O may race with worker completion");
    for(const DWORD error:{ERROR_INVALID_HANDLE,ERROR_ACCESS_DENIED,ERROR_SUCCESS})
        raw_receiver_check(!tf_shell_test::raw_cancel_request_valid(false,error),"another cancellation failure must not be ignored");
    tf_shell_test::RawCancelFailure race;
    race.observe(false,ERROR_NOT_FOUND);
    raw_receiver_check(!race.observed && race.after_join(0)==0,"no pending I/O race must not invent a cancellation error");
    tf_shell_test::RawCancelFailure failed;
    failed.observe(false,ERROR_INVALID_HANDLE); failed.observe(false,ERROR_ACCESS_DENIED); failed.observe(false,ERROR_NOT_FOUND);
    raw_receiver_check(failed.observed && failed.error==ERROR_INVALID_HANDLE && failed.after_join(0)==ERROR_INVALID_HANDLE,"first unexpected cancellation error must survive later requests");
    raw_receiver_check(failed.after_join(ERROR_INVALID_DATA)==ERROR_INVALID_DATA,"cancellation error must not overwrite an existing worker failure");
    tf_shell_test::RawCancelFailure zero_error;
    zero_error.observe(false,ERROR_SUCCESS);
    raw_receiver_check(zero_error.observed && zero_error.error==0 && zero_error.after_join(0)==ERROR_GEN_FAILURE,"failed cancel reporting zero must still fail without inventing a native error");
}
static int child(const std::string& mode,const std::filesystem::path& directory,const std::wstring& prefix) {
    const auto open=[&](const wchar_t* suffix) {
        const HANDLE value=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(prefix+suffix).c_str());
        raw_receiver_check(value!=nullptr,"data-only receiver event missing"); return value;
    };
    Handle ready(open(L"-ready")),blocked(open(L"-blocked")),stop(open(L"-stop")),stopped(open(L"-stopped"));
    raw_receiver_check(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),ENABLE_VIRTUAL_TERMINAL_INPUT),"data-only raw console mode failed");
    tf_shell_test::RawConsoleReceiver receiver(directory/L"capture.bin");
    const auto deadline=GetTickCount64()+10000;
    while(!receiver.status().reads_entered) { raw_receiver_check(GetTickCount64()<deadline,"data-only reader did not enter first read"); Sleep(1); }
    raw_receiver_check(SetEvent(ready.get())!=FALSE,"data-only ready failed");
    const size_t expected_bytes=mode=="empty"?0:6;
    for(;;) {
        const auto state=receiver.status();
        if(state.saved_bytes==expected_bytes && state.reads_entered>state.reads_completed) break;
        raw_receiver_check(GetTickCount64()<deadline,"data-only reader did not reenter blocked read after exact bytes"); Sleep(1);
    }
    raw_receiver_check(SetEvent(blocked.get())!=FALSE,"data-only blocked read signal failed");
    raw_receiver_check(WaitForSingleObject(stop.get(),10000)==WAIT_OBJECT_0,"data-only stop deadline");
    receiver.finish();
    if(mode=="repeat-stop") receiver.finish();
    raw_receiver_check(SetEvent(stopped.get())!=FALSE,"data-only stopped handshake failed"); return 0;
}
static int host(const std::filesystem::path& directory) {
    char gate[3]{};
    for(size_t i=0;i<sizeof(gate);++i) { DWORD count=0; raw_receiver_check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),gate+i,1,&count,nullptr) && count==1,"receiver test outer GO gate closed"); }
    raw_receiver_check(std::string(gate,3)=="GO\n","receiver test outer GO gate invalid");
    BOOL owned=FALSE; raw_receiver_check(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)!=FALSE && owned,"receiver test requires outer Job");
    raw_receiver_check(std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory),"receiver test directory must be fresh");
    bool all=true,first=true;
    std::cout<<"{\"schema\":\"termforge-raw-console-lifecycle-v1\",\"data_only\":true,\"ime_or_global_input_used\":false,\"cases\":[";
    for(const std::string mode:{"empty","unicode","repeat-stop"}) {
        const auto output=directory/mode; raw_receiver_check(std::filesystem::create_directory(output),"receiver case directory must be new");
        const auto prefix=L"Local\\TermForgeRawReceiver-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        const auto event=[&](const wchar_t* suffix) {
            const HANDLE value=CreateEventW(nullptr,TRUE,FALSE,(prefix+suffix).c_str());
            raw_receiver_check(value && GetLastError()!=ERROR_ALREADY_EXISTS,"receiver case event collision"); return value;
        };
        Handle ready(event(L"-ready")),blocked(event(L"-blocked")),stop(event(L"-stop")),stopped(event(L"-stopped"));
        tf_conpty::Session session;
        const std::wstring wide(mode.begin(),mode.end());
        session.start(module_path(),L"--data-child "+wide+L" \""+output.wstring()+L"\" \""+prefix+L"\"");
        raw_receiver_check(WaitForSingleObject(ready.get(),5000)==WAIT_OBJECT_0,"data-only receiver did not become ready");
        const std::string expected=mode=="empty"?std::string():std::string("\xe4\xbd\xa0\xe5\xa5\xbd");
        if(!expected.empty()) session.send(expected);
        raw_receiver_check(WaitForSingleObject(blocked.get(),5000)==WAIT_OBJECT_0,"data-only console read not blocked before stop");
        raw_receiver_check(SetEvent(stop.get())!=FALSE,"data-only stop event failed");
        const bool exited=session.wait_exit(6000),signaled=WaitForSingleObject(stopped.get(),0)==WAIT_OBJECT_0;
        session.close(); const auto received=read_file(output/L"capture.bin");
        const bool passed=exited && signaled && session.exit_code==0 && received==expected && session.cleanup_complete && !session.active_after;
        all=all && passed;
        if(!first) std::cout<<','; first=false;
        std::cout<<"{\"name\":\""<<mode<<"\",\"passed\":"<<(passed?"true":"false")<<",\"child_signaled\":"<<(exited?"true":"false")
            <<",\"exit_code\":"<<session.exit_code<<",\"stopped_event\":"<<(signaled?"true":"false")<<",\"capture_hex\":\""<<hex(received)
            <<"\",\"inner_job_active_after\":"<<session.active_after<<",\"cleanup_complete\":"<<(session.cleanup_complete?"true":"false")
            <<",\"receiver\":"<<read_file(output/L"capture.bin.receiver.json")<<'}';
    }
    std::cout<<"],\"passed\":"<<(all?"true":"false")<<"}\n"; return all?0:2;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wstring(argv[1])==L"--policy") { policy(); std::cout<<"raw receiver stop policy passed\n"; return 0; }
        if(argc==5 && std::wstring(argv[1])==L"--data-child") {
            const std::wstring mode(argv[2]); raw_receiver_check(mode==L"empty" || mode==L"unicode" || mode==L"repeat-stop","unknown data-only case");
            return child(mode==L"empty"?"empty":mode==L"unicode"?"unicode":"repeat-stop",argv[3],argv[4]);
        }
        raw_receiver_check(argc==3 && std::wstring(argv[1])==L"--host","usage: raw_console_receiver_test --policy|--host new-directory");
        return host(argv[2]);
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 18; }
}
