// Real owned ConPTY fixture protocol probe. No UI, clipboard, or IME driver.
#include "../conpty/session.hpp"
#include "workflow_controller.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void check(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
std::wstring module_path() {
    std::wstring value(32768,0);
    const auto size=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    check(size && size<value.size(),"workflow test module path failed"); value.resize(size); return value;
}
std::string read(const std::filesystem::path& path,size_t maximum=4*1024*1024) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); check(file.good(),"workflow evidence missing");
    const auto size=file.tellg(); check(size>=0 && static_cast<size_t>(size)<=maximum,"workflow evidence bound failed");
    std::string value(static_cast<size_t>(size),0); file.seekg(0);
    if(!value.empty()) file.read(value.data(),size); check(file.good(),"workflow evidence read failed"); return value;
}
void save(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    file.close(); check(file.good(),"workflow transport save failed");
}
struct Events {
    static constexpr const wchar_t* names[]{L"ready",L"output",L"output-done",L"append",L"append-done",L"evict",L"evict-done",L"stop",L"stopped"};
    std::array<tf_conpty::Handle,9> values;
    std::wstring prefix;
    Events(const std::wstring& role,unsigned generation):prefix(L"Local\\TermForgeWorkflow-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(generation)+L"-"+role) {
        for(size_t i=0;i<values.size();++i) {
            const auto event=CreateEventW(nullptr,TRUE,FALSE,(prefix+L"-"+names[i]).c_str());
            const auto error=GetLastError(); values[i].reset(event);
            check(event && error!=ERROR_ALREADY_EXISTS,"workflow event collision");
        }
    }
    void signal(size_t i) { check(SetEvent(values[i].get())!=FALSE,"workflow test event signal failed"); }
    bool wait(size_t i,DWORD timeout=5000) { return WaitForSingleObject(values[i].get(),timeout)==WAIT_OBJECT_0; }
};
bool case_a(const std::filesystem::path& directory) {
    check(std::filesystem::create_directory(directory),"workflow case directory must be new");
    Events events(L"A",1); tf_conpty::Session session;
    const auto capture=directory/L"capture.bin";
    const auto client=std::filesystem::path(module_path()).parent_path()/L"shell_text_client.exe";
    session.start(client.wstring(),L"--workflow-v1 --capture \""+capture.wstring()+L"\" --events \""+events.prefix+L"\" --generation 1 --role A");
    const bool ready=events.wait(0); bool completed=false;
    if(ready) {
        events.signal(1);
        if(events.wait(2)) { events.signal(3); if(events.wait(4)) { events.signal(5); completed=events.wait(6); } }
    }
    events.signal(7); const bool exited=session.wait_exit(6000),stopped=events.wait(8,0);
    session.close(); save(directory/L"transport.bin",session.output());
    bool payload=false;
    if(ready && completed && exited && session.exit_code==0) {
        const auto source=read(capture.wstring()+L".source.bin");
        const std::string first="TF_R108_A_0000 \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81\r\n";
        const std::string last="TF_R108_A_0679 \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81\r\n";
        payload=source.rfind(first,0)==0 && source.size()>=last.size() && source.compare(source.size()-last.size(),last.size(),last)==0 &&
            source.find("READY")==std::string::npos && read(capture).empty();
    }
    const bool passed=ready && completed && payload && exited && stopped && session.exit_code==0 && session.cleanup_complete && !session.active_after;
    std::cout<<"{\"name\":\"A\",\"role\":\"A\",\"generation\":1,\"ready\":"<<(ready?"true":"false")
        <<",\"output_complete\":"<<(completed?"true":"false")<<",\"stopped_event\":"<<(stopped?"true":"false")
        <<",\"exit_code\":"<<session.exit_code<<",\"inner_job_active_after\":"<<session.active_after
        <<",\"cleanup_complete\":"<<(session.cleanup_complete?"true":"false")<<",\"transport_file\":\"transport.bin\",\"passed\":"<<(passed?"true":"false")<<'}';
    return passed;
}
bool case_other(const std::filesystem::path& directory,const std::wstring& role,unsigned generation,bool early_stop=false,bool wrong_order=false) {
    check(std::filesystem::create_directory(directory),"workflow case directory must be new");
    Events events(role,generation); tf_conpty::Session session;
    const auto capture=directory/L"capture.bin";
    const auto client=std::filesystem::path(module_path()).parent_path()/L"shell_text_client.exe";
    session.start(client.wstring(),L"--workflow-v1 --capture \""+capture.wstring()+L"\" --events \""+events.prefix+L"\" --generation "+std::to_wstring(generation)+L" --role "+role);
    const bool ready=events.wait(0); bool completed=false,prefix_seen=false;
    const bool is_b=role==L"B";
    const std::string expected=is_b?std::string("TF_R108_PASTE \xe4\xb8\xad\xe6\x96\x87\rNEXT\r\xe4\xbd\xa0\xe5\xa5\xbd"):std::string();
    if(ready) {
        if(is_b) {
            session.send("TF_R108_PASTE \xe4\xb8\xad\xe6\x96\x87\rNEXT\r"); session.send("\xe4\xbd\xa0\xe5\xa5\xbd");
            const auto deadline=GetTickCount64()+5000;
            while(GetTickCount64()<deadline) { if(read(capture)==expected) { prefix_seen=true; break; } Sleep(5); }
        } else if(wrong_order) {
            events.signal(3); completed=session.wait_exit(5000); // append is not a permitted F operation
        } else {
            events.signal(1);
            if(early_stop) {
                const auto deadline=GetTickCount64()+5000;
                const auto source=capture.wstring()+L".source.bin";
                while(GetTickCount64()<deadline) {
                    if(std::filesystem::exists(source) && std::filesystem::file_size(source)>=4096) { prefix_seen=true; break; }
                    Sleep(1);
                }
            } else completed=events.wait(2,16000);
        }
    }
    events.signal(7); const bool exited=session.wait_exit(6000),stopped=events.wait(8,0);
    session.close(); save(directory/L"transport.bin",session.output());
    bool payload=false;
    if(ready && exited) {
        const auto source=read(capture.wstring()+L".source.bin");
        const auto manifest=read(capture.wstring()+L".workflow.json");
        if(is_b) payload=source.empty() && prefix_seen && read(capture)==expected;
        else if(wrong_order) payload=source.empty() && read(capture).empty() && completed &&
            manifest.find("\"status\":\"failed\"")!=std::string::npos && manifest.find("workflow command order invalid")!=std::string::npos && !events.wait(2,0) && !events.wait(4,0);
        else if(early_stop) payload=prefix_seen && source.size()>=4096 && source.size()<2048000 && source.size()%64==0 && !events.wait(2,0) && read(capture).empty();
        else payload=completed && source.size()==2048000 && source.rfind("TF_R108_F1_00000 ",0)==0 && source.find("TF_R108_F1_31999 ")!=std::string::npos && read(capture).empty();
    }
    const bool passed=ready && payload && exited && stopped && session.exit_code==(wrong_order?DWORD{2}:DWORD{0}) && session.cleanup_complete && !session.active_after;
    const std::string name=directory.filename().string(),narrow_role=role==L"B"?"B":role==L"F1"?"F1":"F2";
    std::cout<<"{\"name\":\""<<name<<"\",\"role\":\""<<narrow_role<<"\",\"generation\":"<<generation<<",\"ready\":"<<(ready?"true":"false")
        <<",\"output_complete\":"<<(!early_stop && !wrong_order && !is_b && completed?"true":"false")
        <<",\"expected_failure\":"<<(wrong_order?"true":"false")<<",\"stopped_event\":"<<(stopped?"true":"false")
        <<",\"exit_code\":"<<session.exit_code<<",\"inner_job_active_after\":"<<session.active_after
        <<",\"cleanup_complete\":"<<(session.cleanup_complete?"true":"false")<<",\"transport_file\":\"transport.bin\",\"passed\":"<<(passed?"true":"false")<<'}';
    return passed;
}
int host(const std::filesystem::path& directory) {
    char gate[3]{}; DWORD count=0;
    check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),gate,3,&count,nullptr) && count==3 && std::string(gate,3)=="GO\n","workflow outer GO gate closed");
    BOOL owned=FALSE; check(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)!=FALSE && owned,"workflow test requires outer Job");
    check(std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory),"workflow host directory must be fresh");
    std::cout<<"{\"schema\":\"termforge-workflow-fixture-test-v1\",\"data_only\":true,\"ime_or_global_input_used\":false,\"cases\":[";
    bool passed=case_a(directory/L"A"); std::cout<<',';
    passed=case_other(directory/L"B",L"B",2) && passed; std::cout<<',';
    passed=case_other(directory/L"F1",L"F1",3) && passed; std::cout<<',';
    passed=case_other(directory/L"F2-stop",L"F2",4,true) && passed; std::cout<<',';
    passed=case_other(directory/L"F2-order",L"F2",5,false,true) && passed;
    std::cout<<"],\"passed\":"<<(passed?"true":"false")<<"}\n"; return passed?0:2;
}
bool controller_case(const std::filesystem::path& directory,const std::string& role,unsigned generation,bool close_target=false) {
    check(std::filesystem::create_directory(directory),"controller case directory must be new");
    const auto capture=directory/L"capture.bin";
    tf_shell_test::WorkflowController controller(capture,role,generation); tf_conpty::Session session;
    wchar_t system[32768]{}; const auto count=GetSystemDirectoryW(system,32768); check(count && count<32768,"controller system directory unavailable");
    session.start((std::filesystem::path(system)/L"cmd.exe").wstring(),L"/D /Q /K \"prompt TF_CONTROLLER$G\"");
    check(session.wait_text("TF_CONTROLLER>"),"controller cmd not ready");
    const auto command=controller.command();
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,command.data(),static_cast<int>(command.size()),nullptr,0,nullptr,nullptr);
    check(size>0 && size<=1024,"controller command exceeds native probe input bound");std::string encoded(static_cast<size_t>(size),0);
    check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,command.data(),static_cast<int>(command.size()),encoded.data(),size,nullptr,nullptr)==size,"controller command encoding failed");
    session.send(encoded); const auto deadline=GetTickCount64()+5000;
    while(!controller.ready()) { check(GetTickCount64()<deadline,"controller ready deadline"); Sleep(1); }
    const bool identity=controller.observed_generation()==generation && controller.process_id()!=0 && controller.creation_time()!=0;
    check(controller.bytes().empty(),"controller startup polluted receiver");
    const std::string expected=role=="B" && !close_target?std::string("TF_R108_PASTE \xe4\xb8\xad\xe6\x96\x87\rNEXT\r\xe4\xbd\xa0\xe5\xa5\xbd"):std::string();
    bool phases=false,order_rejected=false;
    if(role=="A") {
        using tf_shell_test::WorkflowPhase;
        check(!controller.done(WorkflowPhase::output) && !controller.done(WorkflowPhase::append) && !controller.done(WorkflowPhase::evict),"new controller completion was not empty");
        try { controller.start(WorkflowPhase::append); } catch(const std::runtime_error&) { order_rejected=true; }
        check(order_rejected,"parent controller allowed append before output");
        for(const auto phase:{WorkflowPhase::output,WorkflowPhase::append,WorkflowPhase::evict}) {
            controller.start(phase);
            const auto phase_deadline=GetTickCount64()+5000;
            while(!controller.done(phase)) { check(GetTickCount64()<phase_deadline,"controller phase completion deadline");Sleep(1); }
        }
        phases=true;
    } else if(!close_target) {
        session.send(expected);
        while(controller.bytes()!=expected) { check(GetTickCount64()<deadline,"controller exact input deadline"); Sleep(1); }
    }
    bool finished=false,abnormal_rejected=false;
    if(close_target) session.cancel();else controller.stop();
    const bool joined=controller.join(3000);
    if(close_target) {
        try { finished=controller.finished(); } catch(const std::runtime_error&) { abnormal_rejected=true; }
        controller.stop(); // Safe after the actual target died; only its owned event is signaled.
    } else finished=controller.finished();
    const DWORD child_exit=controller.exit_code();
    if(!close_target) session.send("exit\r");
    const bool cmd_exited=session.wait_exit(5000);session.close();save(directory/L"transport.bin",session.output());
    bool payload=true;
    if(role=="A") {
        const auto source=read(capture.wstring()+L".source.bin");
        payload=source.size()==18360 && std::count(source.begin(),source.end(),'\n')==680 && source.find("TF_R108_A_0679 ")!=std::string::npos;
    }
    const DWORD expected_exit=close_target?DWORD{91}:DWORD{0};
    const bool passed=identity && joined && (close_target?abnormal_rejected && !finished && !controller.stopped_event():finished) && child_exit==expected_exit && cmd_exited &&
        session.exit_code==expected_exit && session.cleanup_complete && !session.active_after && controller.bytes()==expected && payload;
    std::cout<<"{\"name\":\""<<directory.filename().string()<<"\",\"role\":\""<<role<<"\",\"generation\":"<<generation
        <<",\"launched_through_existing_cmd\":true,\"identity_verified\":"<<(identity?"true":"false")<<",\"child_joined\":"<<(joined?"true":"false")
        <<",\"observed_generation\":"<<controller.observed_generation()<<",\"identity_pid\":"<<controller.process_id()<<",\"process_creation_time\":"<<controller.creation_time()
        <<",\"phases_complete\":"<<(phases?"true":"false")<<",\"parent_order_rejected\":"<<(order_rejected?"true":"false")<<",\"target_job_cancelled\":"<<(close_target?"true":"false")
        <<",\"abnormal_completion_rejected\":"<<(abnormal_rejected?"true":"false")<<",\"stopped_event\":"<<(controller.stopped_event()?"true":"false")
        <<",\"child_finished\":"<<(finished?"true":"false")<<",\"child_exit_code\":"<<child_exit<<",\"cmd_exit_code\":"<<session.exit_code
        <<",\"inner_job_active_after\":"<<session.active_after<<",\"cleanup_complete\":"<<(session.cleanup_complete?"true":"false")<<",\"passed\":"<<(passed?"true":"false")<<'}';
    return passed;
}
int controller_host(const std::filesystem::path& directory) {
    char gate[3]{};
    for(size_t i=0;i<sizeof(gate);++i) { DWORD count=0; check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),gate+i,1,&count,nullptr) && count==1,"controller outer GO gate closed"); }
    check(std::string(gate,3)=="GO\n","controller outer GO gate invalid");
    BOOL owned=FALSE; check(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)!=FALSE && owned,"controller test requires outer Job");
    check(std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory),"controller directory must be fresh");
    std::cout<<"{\"schema\":\"termforge-workflow-controller-test-v1\",\"data_only\":true,\"ime_or_global_input_used\":false,\"cases\":[";
    bool passed=controller_case(directory/L"B graceful","B",21);std::cout<<',';
    passed=controller_case(directory/L"A phases","A",22) && passed;std::cout<<',';
    passed=controller_case(directory/L"B closed","B",23,true) && passed;
    std::cout<<"],\"passed\":"<<(passed?"true":"false")<<"}\n";return passed?0:2;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==3,"usage: workflow_fixture_test --host|--controller-host fresh-directory");
        if(std::wstring(argv[1])==L"--controller-host") return controller_host(argv[2]);
        check(std::wstring(argv[1])==L"--host","unknown workflow probe mode");return host(argv[2]);
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 18; }
}
