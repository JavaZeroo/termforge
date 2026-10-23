// Independent raw console receiver. Test control uses named events, never keys.
#include "hosted_ime_input.hpp"
#include "raw_console_receiver.hpp"
#include "workflow_fixture.hpp"

namespace {
using tf_conpty::Handle;
using tf_shell_test::ime_check;
Handle open_event(const std::wstring& prefix,const wchar_t* suffix) {
    const HANDLE value=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(prefix+suffix).c_str());
    ime_check(value!=nullptr,"IME receiver control event missing"); return Handle(value);
}
void output(const std::string& text) {
    DWORD written=0;
    ime_check(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE && written==text.size(),"IME receiver output failed");
}
int run(const wchar_t* path,const wchar_t* events,unsigned generation) {
    tf_shell_test::require_hosted_ime_input();
    const std::wstring prefix(events);
    ime_check(prefix.rfind(L"Local\\TermForgeIme-",0)==0 && prefix.size()<180 && generation>0,"IME receiver event identity invalid");
    const HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
    ime_check(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) && SetConsoleMode(input,ENABLE_VIRTUAL_TERMINAL_INPUT),"IME receiver raw console mode failed");
    auto ready=open_event(prefix,L"-ready"), flood=open_event(prefix,L"-output"), output_done=open_event(prefix,L"-output-done");
    auto stop=open_event(prefix,L"-stop"), stopped=open_event(prefix,L"-stopped");
    tf_shell_test::RawConsoleReceiver receiver(path);
    // Private handshake only; Python's public allowlist excludes this identity.
    const auto identity=std::filesystem::path(path).wstring()+L".identity";
    Handle identity_file(CreateFileW(identity.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    ime_check(identity_file.get()!=INVALID_HANDLE_VALUE,"IME receiver identity must be new");
    const DWORD data[2]{GetCurrentProcessId(),generation}; DWORD written=0;
    ime_check(WriteFile(identity_file.get(),data,sizeof(data),&written,nullptr) && written==sizeof(data) && FlushFileBuffers(identity_file.get()),"IME receiver identity write failed"); identity_file.reset();
    output("\x1b[2J\x1b[HTF_IME_READY\r\n"); ime_check(SetEvent(ready.get())!=FALSE,"IME receiver ready signal failed");
    const auto deadline=GetTickCount64()+55000; bool emitted=false;
    for(;;) {
        const HANDLE controls[]{stop.get(),flood.get()};
        const auto state=WaitForMultipleObjects(emitted?1:2,controls,FALSE,50);
        if(state==WAIT_OBJECT_0) break;
        if(state==WAIT_OBJECT_0+1) {
            std::string lines;
            for(int i=0;i<20;++i) lines+="TF_IME_OUTPUT "+std::to_string(i)+"\r\n";
            lines+="TF_IME_OUTPUT_DONE\r\n"; output(lines); emitted=true;
            ime_check(SetEvent(output_done.get())!=FALSE,"IME receiver output-done signal failed");
        } else ime_check(state==WAIT_TIMEOUT,"IME receiver event wait failed");
        ime_check(GetTickCount64()<deadline,"IME receiver control deadline expired");
    }
    receiver.finish(); output("TF_IME_DONE\r\n");
    ime_check(SetEvent(stopped.get())!=FALSE,"IME receiver stopped signal failed"); return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1 && std::wstring(argv[1])==L"--workflow-v1") {
            tf_shell_test::require_hosted_ime_input();
            return tf_shell_test::run_workflow_fixture(tf_shell_test::workflow_options(argc,argv));
        }
        ime_check(argc==7 && std::wstring(argv[1])==L"--capture" && std::wstring(argv[3])==L"--events" && std::wstring(argv[5])==L"--generation","usage: shell_ime_client --capture new.bin --events owned-prefix --generation N");
        unsigned generation=0;
        for(const wchar_t* p=argv[6];*p;++p) { ime_check(*p>=L'0' && *p<=L'9' && generation<100000,"IME receiver generation invalid"); generation=generation*10+static_cast<unsigned>(*p-L'0'); }
        return run(argv[2],argv[4],generation);
    } catch(const std::exception& error) { std::fprintf(stderr,"TermForge IME receiver: %s\n",error.what()); return 18; }
}
