// Independent raw console receiver. Test control uses named events, never keys.
#include "hosted_ime_input.hpp"
#include <atomic>
#include <thread>

namespace {
using tf_conpty::Handle;
using tf_shell_test::ime_check;
constexpr size_t maximum_units=4096;
class Receiver {
    Handle file_;
    std::atomic<DWORD> error_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
    static std::string encode(const std::wstring& value) { return tf_shell_test::ime_utf8(value); }
    void read() noexcept {
        try {
            size_t total=0; wchar_t pending=0;
            while(!stop_) {
                wchar_t units[256]{}; DWORD count=0;
                if(!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE),units,256,&count,nullptr)) {
                    const DWORD error=GetLastError();
                    if(!stop_ || error!=ERROR_OPERATION_ABORTED) error_=error?error:ERROR_READ_FAULT;
                    break;
                }
                ime_check(count!=0,"IME receiver empty console read");
                ime_check(count<=maximum_units-total,"IME receiver UTF-16 bound exceeded"); total+=count;
                std::wstring complete; if(pending) complete+=pending; complete.append(units,count); pending=0;
                if(!complete.empty() && complete.back()>=0xd800 && complete.back()<=0xdbff) { pending=complete.back(); complete.pop_back(); }
                if(!complete.empty()) {
                    const auto bytes=encode(complete); DWORD written=0;
                    ime_check(WriteFile(file_.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)!=FALSE && written==bytes.size(),"IME receiver capture write failed");
                    ime_check(FlushFileBuffers(file_.get())!=FALSE,"IME receiver capture flush failed");
                }
            }
            ime_check(!pending,"IME receiver stopped with an incomplete surrogate");
        } catch(...) { error_=ERROR_INVALID_DATA; }
    }
public:
    explicit Receiver(const std::filesystem::path& path) {
        file_.reset(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        ime_check(file_.get()!=INVALID_HANDLE_VALUE,"IME receiver capture must be new");
        thread_=std::thread([this] { read(); });
    }
    void finish() {
        if(thread_.joinable()) {
            stop_=true;
            const auto deadline=GetTickCount64()+3000;
            do {
                CancelSynchronousIo(thread_.native_handle());
                const auto state=WaitForSingleObject(thread_.native_handle(),10);
                if(state==WAIT_OBJECT_0) { thread_.join(); break; }
                if(state==WAIT_FAILED || GetTickCount64()>=deadline) std::_Exit(19);
            } while(true);
        }
        ime_check(error_==0,"IME receiver console worker failed");
        ime_check(FlushFileBuffers(file_.get())!=FALSE,"IME receiver final flush failed"); file_.reset();
    }
    ~Receiver() { if(thread_.joinable()) { try { finish(); } catch(...) { std::_Exit(19); } } }
};
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
    Receiver receiver(path);
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
        ime_check(argc==7 && std::wstring(argv[1])==L"--capture" && std::wstring(argv[3])==L"--events" && std::wstring(argv[5])==L"--generation","usage: shell_ime_client --capture new.bin --events owned-prefix --generation N");
        unsigned generation=0;
        for(const wchar_t* p=argv[6];*p;++p) { ime_check(*p>=L'0' && *p<=L'9' && generation<100000,"IME receiver generation invalid"); generation=generation*10+static_cast<unsigned>(*p-L'0'); }
        return run(argv[2],argv[4],generation);
    } catch(const std::exception& error) { std::fprintf(stderr,"TermForge IME receiver: %s\n",error.what()); return 18; }
}
