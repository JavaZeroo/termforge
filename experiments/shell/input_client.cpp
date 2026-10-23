// Independent console receiver for scripted HWND keyboard and paste acceptance.
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

static bool output(const char* value, DWORD size) {
    DWORD written=0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),value,size,&written,nullptr) && written==size;
}
static int keyboard_receiver(const wchar_t* path) {
    const HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
    if(!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8) ||
       !SetConsoleMode(input,ENABLE_VIRTUAL_TERMINAL_INPUT)) return 3;
    std::ofstream file(std::filesystem::path(path),std::ios::binary);
    if(!file || !output("TF_KEY_READY\r\n",14)) return 4;
    std::wstring received;
    for(;;) {
        wchar_t units[256]; DWORD count=0;
        if(!ReadConsoleW(input,units,256,&count,nullptr) || !count) return 5;
        for(DWORD i=0;i<count;++i) {
            if(units[i]==4) {
                if(i+1!=count) return 6;
                const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,received.data(),
                    static_cast<int>(received.size()),nullptr,0,nullptr,nullptr);
                if(bytes<=0) return 7;
                std::string encoded(static_cast<size_t>(bytes),0);
                if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,received.data(),static_cast<int>(received.size()),
                    encoded.data(),bytes,nullptr,nullptr)!=bytes) return 8;
                file.write(encoded.data(),bytes); file.close();
                return file && output("TF_KEY_DONE\r\n",13)?0:9;
            }
            received+=units[i];
            if(received.size()>1024*1024) return 10;
        }
    }
}

namespace {
constexpr DWORD gate_timeout_ms=15000;
constexpr DWORD shutdown_timeout_ms=3000;
constexpr size_t maximum_units=1024*1024;

class Handle {
    HANDLE value_=nullptr;
public:
    explicit Handle(HANDLE value):value_(value) {
        if(!value_ || value_==INVALID_HANDLE_VALUE)
            throw std::runtime_error("create/open handle failed: "+std::to_string(GetLastError()));
    }
    ~Handle() { CloseHandle(value_); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    HANDLE get() const { return value_; }
};

static void signal(HANDLE event) {
    if(!SetEvent(event)) throw std::runtime_error("SetEvent failed");
}
static void wait_gate(HANDLE event) {
    if(WaitForSingleObject(event,gate_timeout_ms)!=WAIT_OBJECT_0)
        throw std::runtime_error("paste gate timed out or failed");
}

// Worker state must outlive this member. Every owner declares it last, so an
// exception also cancels and joins the worker before destroying its buffers.
class Worker {
    Handle stop_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread thread_;
public:
    template<class F> void start(F function) {
        thread_=std::thread([this,function] { function(stop_.get()); });
    }
    void stop() noexcept {
        if(!thread_.joinable()) return;
        SetEvent(stop_.get());
        const auto deadline=GetTickCount64()+shutdown_timeout_ms;
        for(;;) {
            // Repeat cancellation: a worker may have passed its stop check just
            // before the main thread signalled it, then entered synchronous I/O.
            CancelSynchronousIo(thread_.native_handle());
            const DWORD state=WaitForSingleObject(thread_.native_handle(),10);
            if(state==WAIT_OBJECT_0) break;
            if(state==WAIT_FAILED || GetTickCount64()>=deadline) {
                // This is an owned test process. Never leave a worker referring
                // to destroyed state, or let an uncancellable console hang it.
                std::_Exit(11);
            }
        }
        try { thread_.join(); } catch(...) { std::_Exit(11); }
    }
    ~Worker() { stop(); }
};

class ConsoleReader {
    HANDLE input_;
    Handle request_{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    Handle completed_{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    std::array<wchar_t,256> units_{};
    DWORD requested_=0,count_=0,error_=0;
    Worker worker_;
public:
    explicit ConsoleReader(HANDLE input):input_(input) {
        worker_.start([this](HANDLE stop) {
            const HANDLE events[]{stop,request_.get()};
            for(;;) {
                const DWORD state=WaitForMultipleObjects(2,events,FALSE,100);
                if(state==WAIT_TIMEOUT) continue;
                if(state!=WAIT_OBJECT_0+1) return;
                count_=0;
                error_=ReadConsoleW(input_,units_.data(),requested_,&count_,nullptr)?ERROR_SUCCESS:GetLastError();
                if(!SetEvent(completed_.get())) return;
            }
        });
    }
    std::wstring read(DWORD maximum) {
        if(!maximum || maximum>units_.size()) throw std::runtime_error("invalid console read size");
        requested_=maximum;
        signal(request_.get());
        wait_gate(completed_.get());
        if(error_ || !count_) throw std::runtime_error("ReadConsoleW failed or returned no input");
        return {units_.data(),count_};
    }
    void stop() { worker_.stop(); }
};

class FloodOutput {
    std::atomic<bool> failed_{false};
    Worker worker_;
public:
    FloodOutput() {
        worker_.start([this](HANDLE stop) {
            try {
                unsigned long long sequence=0;
                while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT) {
                    std::string batch;
                    for(unsigned i=0;i<16;++i) {
                        char line[80];
                        const int length=std::snprintf(line,sizeof(line),"TF_PASTE_FLOOD %020llu\r\n",sequence++);
                        if(length<=0 || static_cast<size_t>(length)>=sizeof(line))
                            throw std::runtime_error("format flood output failed");
                        batch.append(line,static_cast<size_t>(length));
                    }
                    size_t offset=0;
                    while(offset<batch.size()) {
                        if(WaitForSingleObject(stop,0)!=WAIT_TIMEOUT) return;
                        DWORD written=0;
                        if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),batch.data()+offset,
                            static_cast<DWORD>(batch.size()-offset),&written,nullptr) || !written) {
                            if(WaitForSingleObject(stop,0)==WAIT_TIMEOUT) failed_=true;
                            return;
                        }
                        offset+=written;
                    }
                    // A cancellable scheduling point after each bounded batch.
                    if(WaitForSingleObject(stop,1)!=WAIT_TIMEOUT) return;
                }
            } catch(...) { failed_=true; }
        });
    }
    void stop() { worker_.stop(); }
    bool failed() const { return failed_.load(); }
};

class Capture {
    Handle file_;
    wchar_t pending_high_=0;
    size_t received_units_=0;
    unsigned long long saved_bytes_=0;
public:
    explicit Capture(const wchar_t* path):file_(CreateFileW(path,GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)) {}
    // Only complete Unicode scalars reach the file. A high surrogate at the
    // end of a read is held until its matching low surrogate arrives.
    bool append(const std::wstring& units) {
        const size_t eot=units.find(static_cast<wchar_t>(4));
        const bool done=eot!=std::wstring::npos;
        if(done && eot+1!=units.size()) throw std::runtime_error("input after EOT");
        const size_t count=done?eot:units.size();
        if(count>maximum_units-received_units_) throw std::runtime_error("input exceeds UTF-16 capacity");
        received_units_+=count;
        std::wstring complete;
        if(pending_high_) complete+=pending_high_;
        complete.append(units.data(),count);
        pending_high_=0;
        if(!complete.empty() && complete.back()>=0xd800 && complete.back()<=0xdbff) {
            pending_high_=complete.back(); complete.pop_back();
        }
        if(done && pending_high_) throw std::runtime_error("unpaired surrogate before EOT");
        if(!complete.empty()) {
            const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,complete.data(),
                static_cast<int>(complete.size()),nullptr,0,nullptr,nullptr);
            if(bytes<=0) throw std::runtime_error("invalid UTF-16 console input");
            std::string encoded(static_cast<size_t>(bytes),0);
            if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,complete.data(),static_cast<int>(complete.size()),
                encoded.data(),bytes,nullptr,nullptr)!=bytes) throw std::runtime_error("UTF-8 conversion failed");
            DWORD written=0;
            if(!WriteFile(file_.get(),encoded.data(),static_cast<DWORD>(encoded.size()),&written,nullptr) ||
               written!=encoded.size()) {
                // Roll back a failed partial write to the last complete batch.
                LARGE_INTEGER position{}; position.QuadPart=static_cast<LONGLONG>(saved_bytes_);
                if(SetFilePointerEx(file_.get(),position,nullptr,FILE_BEGIN)) SetEndOfFile(file_.get());
                throw std::runtime_error("capture write failed");
            }
            saved_bytes_+=written;
        }
        if(!FlushFileBuffers(file_.get())) throw std::runtime_error("capture flush failed");
        return done;
    }
    unsigned long long saved_bytes() const { return saved_bytes_; }
};

static int paste_receiver(const wchar_t* path,const wchar_t* prefix) {
    const HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
    if(!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8) ||
       !SetConsoleMode(input,ENABLE_VIRTUAL_TERMINAL_INPUT)) return 3;
    const std::wstring name(prefix);
    const DWORD access=SYNCHRONIZE|EVENT_MODIFY_STATE;
    Handle ready(OpenEventW(access,FALSE,(name+L"-ready").c_str()));
    Handle read(OpenEventW(access,FALSE,(name+L"-read").c_str()));
    Handle checkpoint(OpenEventW(access,FALSE,(name+L"-checkpoint").c_str()));
    Handle release(OpenEventW(access,FALSE,(name+L"-release").c_str()));
    Capture capture(path);
    if(!output("TF_KEY_READY\r\n",14)) return 4;
    ConsoleReader reader(input);
    FloodOutput flood;
    signal(ready.get());
    wait_gate(read.get());
    DWORD first_units=0;
    while(first_units<256) {
        const auto units=reader.read(256-first_units);
        if(capture.append(units)) throw std::runtime_error("EOT before paste checkpoint");
        first_units+=static_cast<DWORD>(units.size());
    }
    if(!capture.saved_bytes()) throw std::runtime_error("paste checkpoint has no complete Unicode scalar");
    signal(checkpoint.get());
    wait_gate(release.get());
    for(;;) {
        if(flood.failed()) throw std::runtime_error("flood output failed");
        if(capture.append(reader.read(256))) break;
    }
    reader.stop();
    flood.stop();
    if(flood.failed()) throw std::runtime_error("flood output failed");
    // Cancellation may interrupt a flood write within a line. Start the final
    // marker on its own line after the output worker has stopped.
    return output("\r\nTF_KEY_DONE\r\n",15)?0:9;
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2) return keyboard_receiver(argv[1]);
    if(argc!=4 || std::wstring(argv[1])!=L"--paste-gated") return 2;
    try { return paste_receiver(argv[2],argv[3]); }
    catch(const std::exception& error) {
        std::fprintf(stderr,"TermForge paste receiver: %s\n",error.what());
        return 11;
    }
}
