// Data-only, event-controlled r108 console fixture shared by two test clients.
// No clipboard, input injection, language setup, or hosted-environment bypass.
#pragma once
#include "raw_console_receiver.hpp"
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <iomanip>

namespace tf_shell_test {
inline void workflow_check(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
inline std::string workflow_quote(const std::string& value) {
    std::string out="\""; constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value) {
        if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else { if(c=='"' || c=='\\') out+='\\'; out+=static_cast<char>(c); }
    }
    return out+'"';
}
class WorkflowSha256 {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
    BCRYPT_HASH_HANDLE hash_=nullptr;
public:
    WorkflowSha256() {
        workflow_check(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"workflow SHA256 provider failed");
        if(BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm_,0); algorithm_=nullptr;
            throw std::runtime_error("workflow SHA256 create failed");
        }
    }
    ~WorkflowSha256() { if(hash_) BCryptDestroyHash(hash_); if(algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0); }
    WorkflowSha256(const WorkflowSha256&)=delete;
    WorkflowSha256& operator=(const WorkflowSha256&)=delete;
    void update(std::string_view bytes) {
        workflow_check(bytes.size()<=4096,"workflow SHA256 update bound failed");
        workflow_check(BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)>=0,"workflow SHA256 update failed");
    }
    std::string digest() const {
        BCRYPT_HASH_HANDLE copy=nullptr;
        workflow_check(BCryptDuplicateHash(hash_,&copy,nullptr,0,0)>=0,"workflow SHA256 snapshot failed");
        unsigned char bytes[32]{}; const auto result=BCryptFinishHash(copy,bytes,sizeof(bytes),0); BCryptDestroyHash(copy);
        workflow_check(result>=0,"workflow SHA256 finish failed");
        constexpr char hex[]="0123456789abcdef"; std::string out;
        for(unsigned char byte:bytes) { out+=hex[byte>>4]; out+=hex[byte&15]; } return out;
    }
};
inline unsigned long long workflow_decimal(std::wstring_view text,unsigned long long maximum) {
    workflow_check(!text.empty() && text.front()!=L'0',"workflow decimal identity invalid");
    unsigned long long value=0;
    for(wchar_t c:text) {
        workflow_check(c>=L'0' && c<=L'9' && value<=(maximum-static_cast<unsigned>(c-L'0'))/10,"workflow decimal identity invalid");
        value=value*10+static_cast<unsigned>(c-L'0');
    }
    return value;
}
struct WorkflowOptions {
    std::filesystem::path capture;
    std::wstring prefix,role;
    unsigned generation=0;
    DWORD controller_pid=0;
};
inline WorkflowOptions workflow_options(int argc,wchar_t** argv) {
    workflow_check(argc==10 && std::wstring(argv[1])==L"--workflow-v1" && std::wstring(argv[2])==L"--capture" &&
        std::wstring(argv[4])==L"--events" && std::wstring(argv[6])==L"--generation" && std::wstring(argv[8])==L"--role",
        "usage: client --workflow-v1 --capture NEW --events owned-prefix --generation N --role A|B|F1..F5");
    WorkflowOptions value; value.capture=argv[3]; value.prefix=argv[5]; value.role=argv[9];
    value.generation=static_cast<unsigned>(workflow_decimal(argv[7],999999));
    workflow_check(!value.capture.empty() && (value.role==L"A" || value.role==L"B" ||
        (value.role.size()==2 && value.role[0]==L'F' && value.role[1]>=L'1' && value.role[1]<=L'5')),"workflow role or capture invalid");
    const std::wstring start=L"Local\\TermForgeWorkflow-";
    const std::wstring end=L"-"+std::to_wstring(value.generation)+L"-"+value.role;
    workflow_check(value.prefix.size()<180 && value.prefix.rfind(start,0)==0 && value.prefix.size()>start.size()+end.size() &&
        value.prefix.compare(value.prefix.size()-end.size(),end.size(),end)==0,"workflow event generation/role mismatch");
    const auto middle=value.prefix.substr(start.size(),value.prefix.size()-start.size()-end.size());
    const auto separator=middle.find(L'-');
    workflow_check(separator!=std::wstring::npos && middle.find(L'-',separator+1)==std::wstring::npos,"workflow event owner identity invalid");
    value.controller_pid=static_cast<DWORD>(workflow_decimal(std::wstring_view(middle).substr(0,separator),MAXDWORD));
    workflow_decimal(std::wstring_view(middle).substr(separator+1),(std::numeric_limits<unsigned long long>::max)());
    return value;
}
inline HANDLE workflow_new_file(const std::filesystem::path& path) {
    const auto value=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    workflow_check(value!=INVALID_HANDLE_VALUE,"workflow evidence must be new"); return value;
}
inline void workflow_save(HANDLE file,std::string_view bytes) {
    DWORD written=0;
    workflow_check(bytes.size()<=MAXDWORD && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size(),"workflow evidence write failed");
}
inline std::string workflow_file_sha256(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary); workflow_check(file.good(),"workflow capture hash open failed");
    WorkflowSha256 hash; char bytes[4096];
    while(file) { file.read(bytes,sizeof(bytes)); if(file.gcount()) hash.update(std::string_view(bytes,static_cast<size_t>(file.gcount()))); }
    workflow_check(file.eof(),"workflow capture hash read failed"); return hash.digest();
}
struct WorkflowStage {
    std::string name;
    unsigned first=0,requested_records=0,records=0,late_blocks=0;
    unsigned long long bytes=0,source_begin=0,source_end=0,begin_ms=0,end_ms=0,write_ms=0,max_write_ms=0;
    std::string source_sha256;
    bool complete=false;
};
class WorkflowOutput {
    tf_conpty::Handle source_,progress_;
    WorkflowSha256 hash_;
    mutable std::mutex mutex_;
    unsigned long long bytes_=0,records_=0;
    unsigned sequence_=0;
    ULONGLONG begun_=GetTickCount64();
public:
    explicit WorkflowOutput(const std::filesystem::path& capture):source_(workflow_new_file(capture.wstring()+L".source.bin")),
        progress_(workflow_new_file(capture.wstring()+L".progress.jsonl")) {}
    unsigned long long elapsed() const { return GetTickCount64()-begun_; }
    // Console I/O is outside the snapshot mutex: stop can record and cancel a blocked writer.
    unsigned long long write(std::string_view value,bool payload,WorkflowSha256* stage_hash=nullptr) {
        workflow_check(value.size()<=4096,"workflow output block bound failed");
        DWORD written=0; const auto begun=GetTickCount64();
        const BOOL ok=WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),value.data(),static_cast<DWORD>(value.size()),&written,nullptr);
        const auto write_ms=GetTickCount64()-begun;
        workflow_check(written<=value.size(),"workflow console invalid write count");
        if(payload && written) {
            std::lock_guard<std::mutex> lock(mutex_); const auto actual=value.substr(0,written);
            workflow_save(source_.get(),actual); hash_.update(actual); if(stage_hash) stage_hash->update(actual);
            bytes_+=written; records_+=static_cast<unsigned long long>(std::count(actual.begin(),actual.end(),'\n'));
        }
        workflow_check(ok && written==value.size(),"workflow console write incomplete");
        return write_ms;
    }
    std::pair<unsigned long long,unsigned long long> counts() const { std::lock_guard<std::mutex> lock(mutex_); return {bytes_,records_}; }
    std::string digest() const { std::lock_guard<std::mutex> lock(mutex_); return hash_.digest(); }
    void flush() { std::lock_guard<std::mutex> lock(mutex_); workflow_check(FlushFileBuffers(source_.get())!=FALSE,"workflow source flush failed"); }
    void progress(const char* event,const std::string& phase={}) {
        std::lock_guard<std::mutex> lock(mutex_); std::ostringstream out;
        out<<"{\"schema\":\"termforge-workflow-progress-v1\",\"seq\":"<<++sequence_<<",\"elapsed_ms\":"<<elapsed()
            <<",\"event\":"<<workflow_quote(event)<<",\"phase\":"<<(phase.empty()?"null":workflow_quote(phase))
            <<",\"source_bytes\":"<<bytes_<<",\"records\":"<<records_<<",\"source_sha256\":"<<workflow_quote(hash_.digest())<<"}\n";
        workflow_save(progress_.get(),out.str()); workflow_check(FlushFileBuffers(progress_.get())!=FALSE,"workflow progress flush failed");
    }
};
inline std::string workflow_a_line(unsigned number) {
    std::ostringstream out; out<<"TF_R108_A_"<<std::setw(4)<<std::setfill('0')<<number<<" \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81\r\n"; return out.str();
}
inline std::string workflow_f_block(const std::string& role,unsigned block) {
    std::string bytes;
    for(unsigned row=0;row<64;++row) {
        std::ostringstream out; out<<"TF_R108_"<<role<<'_'<<std::setw(5)<<std::setfill('0')<<(block*64+row)<<" \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81";
        auto line=out.str(); workflow_check(line.size()<62,"workflow F record overflow"); line.resize(62,'.'); bytes+=line+"\r\n";
    }
    workflow_check(bytes.size()==4096,"workflow F block size invalid"); return bytes;
}
class WorkflowWorker {
    std::thread thread_;
    HANDLE stop_;
    std::exception_ptr error_;
    std::atomic<bool> done_{true};
public:
    explicit WorkflowWorker(HANDLE stop):stop_(stop) {}
    ~WorkflowWorker() { if(thread_.joinable()) { SetEvent(stop_); join(true); } }
    bool done() const { return done_; }
    void join(bool cancel) {
        if(!thread_.joinable()) return;
        const auto deadline=GetTickCount64()+3000;
        while(WaitForSingleObject(thread_.native_handle(),10)!=WAIT_OBJECT_0) {
            if(cancel) CancelSynchronousIo(thread_.native_handle());
            if(GetTickCount64()>=deadline) std::_Exit(21);
        }
        thread_.join();
    }
    void check() { if(done_) { join(false); if(error_) std::rethrow_exception(error_); } }
    void start(std::function<bool()> action,HANDLE completion) {
        check(); workflow_check(!thread_.joinable() && done_,"workflow output already running"); done_=false; error_=nullptr;
        thread_=std::thread([this,action=std::move(action),completion] {
            try {
                const bool complete=action(); done_=true;
                if(complete) workflow_check(SetEvent(completion)!=FALSE,"workflow completion signal failed");
            } catch(...) { error_=std::current_exception(); done_=true; }
        });
    }
};
inline void workflow_capture_console(const WorkflowOptions& options,const std::string& role) {
    const auto console=GetStdHandle(STD_OUTPUT_HANDLE);
    workflow_check(console && console!=INVALID_HANDLE_VALUE,"workflow console output handle unavailable");
    CONSOLE_SCREEN_BUFFER_INFO info{};
    workflow_check(GetConsoleScreenBufferInfo(console,&info)!=FALSE,"workflow console dimensions unavailable");
    const int columns=info.dwSize.X,rows=info.srWindow.Bottom-info.srWindow.Top+1;
    workflow_check(columns>0 && columns<=500 && rows>0 && rows<=200 && info.srWindow.Top>=0,"workflow console capture bounds invalid");
    static_assert(sizeof(CHAR_INFO)==4,"workflow console cell layout changed");
    std::wstring text;std::vector<DWORD> row_units;std::vector<CHAR_INFO> cells(static_cast<size_t>(columns)*rows);
    for(int row=0;row<rows;++row) {
        std::wstring line(static_cast<size_t>(columns),L'\0');DWORD read=0;
        const BOOL success=ReadConsoleOutputCharacterW(console,line.data(),columns,{0,static_cast<short>(info.srWindow.Top+row)},&read);
        const DWORD error=success?0:GetLastError();
        workflow_check(success,("workflow console row read failed: error="+std::to_string(error)+" row="+std::to_string(row)).c_str());
        workflow_check(read<=static_cast<DWORD>(columns),"workflow console row exceeded its width");
        line.resize(read);workflow_check(line.find(L'\0')==std::wstring::npos,"workflow console row contains an unmapped cell");
        text+=line;row_units.push_back(read);
        SMALL_RECT region{0,static_cast<short>(info.srWindow.Top+row),static_cast<short>(columns-1),static_cast<short>(info.srWindow.Top+row)};
        const BOOL cells_read=ReadConsoleOutputW(console,cells.data()+static_cast<size_t>(row)*columns,
            {static_cast<short>(columns),1},{0,0},&region);
        const DWORD cell_error=cells_read?0:GetLastError();
        workflow_check(cells_read && region.Left==0 && region.Top==info.srWindow.Top+row &&
            region.Right==columns-1 && region.Bottom==info.srWindow.Top+row,
            ("workflow console cell read failed="+std::to_string(cell_error)).c_str());
    }
    const auto path=options.capture.wstring();
    tf_conpty::Handle screen(workflow_new_file(path+L".console.bin"));
    workflow_save(screen.get(),std::string_view(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t)));
    workflow_check(FlushFileBuffers(screen.get())!=FALSE,"workflow console text flush failed");
    const auto digest=workflow_file_sha256(path+L".console.bin");
    tf_conpty::Handle cell_file(workflow_new_file(path+L".cells.bin"));
    workflow_save(cell_file.get(),std::string_view(reinterpret_cast<const char*>(cells.data()),cells.size()*sizeof(CHAR_INFO)));
    workflow_check(FlushFileBuffers(cell_file.get())!=FALSE,"workflow console cells flush failed");
    const auto cell_digest=workflow_file_sha256(path+L".cells.bin");
    std::ostringstream metadata;
    metadata<<"{\"schema\":\"termforge-console-window-v1\",\"role\":"<<workflow_quote(role)
        <<",\"generation\":"<<options.generation<<",\"columns\":"<<columns<<",\"rows\":"<<rows
        <<",\"top\":"<<info.srWindow.Top<<",\"cursor_column\":"<<info.dwCursorPosition.X
        <<",\"cursor_row\":"<<info.dwCursorPosition.Y<<",\"utf16_units\":"<<text.size()<<",\"row_units\":[";
    for(size_t i=0;i<row_units.size();++i) {if(i) metadata<<',';metadata<<row_units[i];}
    metadata<<"],\"text_sha256\":"<<workflow_quote(digest)<<",\"cell_count\":"<<cells.size()
        <<",\"cell_stride\":"<<sizeof(CHAR_INFO)<<",\"cell_sha256\":"<<workflow_quote(cell_digest)<<"}\n";
    tf_conpty::Handle record(workflow_new_file(path+L".console.json"));
    workflow_save(record.get(),metadata.str());
    workflow_check(FlushFileBuffers(record.get())!=FALSE,"workflow console metadata flush failed");
}
inline int run_workflow_fixture(const WorkflowOptions& options) {
    BOOL owned=FALSE; workflow_check(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)!=FALSE && owned,"workflow fixture requires an owned Job");
    const auto input=GetStdHandle(STD_INPUT_HANDLE);
    workflow_check(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) && SetConsoleMode(input,ENABLE_VIRTUAL_TERMINAL_INPUT),"workflow raw console mode failed");
    tf_conpty::Handle controller(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,options.controller_pid));
    workflow_check(controller.get() && WaitForSingleObject(controller.get(),0)==WAIT_TIMEOUT,"workflow controller is not alive");
    constexpr const wchar_t* suffixes[]{L"ready",L"output",L"output-done",L"append",L"append-done",L"evict",L"evict-done",L"stop",L"stopped"};
    std::array<tf_conpty::Handle,9> events;
    for(size_t i=0;i<events.size();++i) {
        events[i].reset(OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(options.prefix+L"-"+suffixes[i]).c_str()));
        workflow_check(events[i].get() && WaitForSingleObject(events[i].get(),0)==WAIT_TIMEOUT,"workflow event missing or stale");
    }
    tf_conpty::Handle manifest(workflow_new_file(options.capture.wstring()+L".workflow.json"));
    WorkflowOutput output(options.capture); RawConsoleReceiver receiver(options.capture); WorkflowWorker worker(events[7].get());
    tf_conpty::Handle identity(workflow_new_file(options.capture.wstring()+L".identity"));
    const DWORD data[]{GetCurrentProcessId(),options.generation};
    workflow_save(identity.get(),std::string_view(reinterpret_cast<const char*>(data),sizeof(data)));
    workflow_check(FlushFileBuffers(identity.get())!=FALSE,"workflow identity flush failed"); identity.reset();
    std::vector<WorkflowStage> stages; stages.reserve(3);
    std::string role; for(wchar_t c:options.role) role+=static_cast<char>(c); // validated ASCII roles only
    std::string failure; bool stop_seen=false; unsigned next=0;
    try {
        output.write("\x1b[2J\x1b[HTF_R108_"+role+"_READY\r\n",false); output.progress("ready");
        workflow_check(SetEvent(events[0].get())!=FALSE,"workflow ready signal failed");
        const auto deadline=GetTickCount64()+55000;
        for(;;) {
            worker.check();
            const HANDLE controls[]{events[7].get(),controller.get(),events[1].get(),events[3].get(),events[5].get()};
            const auto state=WaitForMultipleObjects(5,controls,FALSE,20);
            if(state==WAIT_OBJECT_0) { stop_seen=true; output.progress("stop"); break; }
            workflow_check(state!=WAIT_OBJECT_0+1,"workflow controller exited");
            if(state>=WAIT_OBJECT_0+2 && state<=WAIT_OBJECT_0+4) {
                const unsigned phase=state-WAIT_OBJECT_0-2,index=1+phase*2;
                workflow_check(ResetEvent(events[index].get())!=FALSE,"workflow command reset failed");
                const std::string name=phase==0?"output":phase==1?"append":"evict";
                if(!worker.done() || phase!=next || options.role==L"B" || (options.role!=L"A" && phase!=0)) {
                    output.progress("event-rejected",name); throw std::runtime_error("workflow command order invalid");
                }
                worker.check(); ++next;
                output.progress("event-start",name);
                worker.start([&,phase,index,name] {
                    WorkflowStage stage; stage.name=name; stage.first=phase==0?0:phase==1?160:180;
                    stage.requested_records=options.role==L"A"?(phase==0?160:phase==1?20:500):32000;
                    const auto initial=output.counts(); stage.source_begin=initial.first; stage.begin_ms=output.elapsed(); WorkflowSha256 hash;
                    try {
                        if(options.role==L"A") {
                            for(unsigned i=0;i<stage.requested_records;++i) {
                                if(WaitForSingleObject(events[7].get(),0)==WAIT_OBJECT_0) break;
                                output.write(workflow_a_line(stage.first+i),true,&hash);
                            }
                        } else {
                            const auto begun=GetTickCount64();
                            for(unsigned block=0;block<500;++block) {
                                const auto target=begun+static_cast<ULONGLONG>(block)*20;
                                const auto now=GetTickCount64();
                                if(now<target && WaitForSingleObject(events[7].get(),static_cast<DWORD>(target-now))==WAIT_OBJECT_0) break;
                                if(WaitForSingleObject(events[7].get(),0)==WAIT_OBJECT_0) break;
                                if(GetTickCount64()>target) ++stage.late_blocks;
                                const auto bytes=workflow_f_block(role,block); const auto elapsed=output.write(bytes,true,&hash);
                                stage.write_ms+=elapsed; stage.max_write_ms=(std::max)(stage.max_write_ms,elapsed);
                            }
                        }
                        const auto counts=output.counts(); stage.source_end=counts.first; stage.bytes=stage.source_end-stage.source_begin;
                        stage.records=static_cast<unsigned>(counts.second-initial.second);
                        stage.complete=stage.records==stage.requested_records;
                        if(stage.complete) {
                            const std::string count=options.role==L"A"?(phase==0?"0160":phase==1?"0020":"0500"):"0500";
                            std::string upper=name; for(char& c:upper) c=static_cast<char>(c-'a'+'A');
                            output.write("TF_R108_"+role+'_'+upper+"_DONE "+count+"\r\n",false);
                            output.flush(); stage.end_ms=output.elapsed(); output.progress("output-complete",name);
                        }
                    } catch(...) {
                        const auto counts=output.counts(); stage.source_end=counts.first; stage.bytes=stage.source_end-stage.source_begin;
                        stage.records=static_cast<unsigned>(counts.second-initial.second);
                        stage.complete=false; stage.end_ms=output.elapsed(); stage.source_sha256=hash.digest(); stages.push_back(std::move(stage)); throw;
                    }
                    if(!stage.complete) stage.end_ms=output.elapsed();
                    stage.source_sha256=hash.digest(); const bool complete=stage.complete; stages.push_back(std::move(stage)); return complete;
                },events[index+1].get());
            } else workflow_check(state==WAIT_TIMEOUT,"workflow control wait failed");
            workflow_check(GetTickCount64()<deadline,"workflow control deadline expired");
        }
    } catch(const std::exception& error) { failure=error.what(); }
    if(!stop_seen) SetEvent(events[7].get());
    worker.join(true);
    try { worker.check(); } catch(const std::exception& error) { if(failure.empty()) failure=error.what(); }
    try { receiver.finish(); } catch(const std::exception& error) { if(failure.empty()) failure=error.what(); }
    bool console_window=failure.empty() && role!="A" && role!="B" && stages.size()==1 && stages.front().complete;
    if(console_window) {
        try { workflow_capture_console(options,role); }
        catch(const std::exception& error) { failure=error.what();console_window=false; }
    }
    if(failure.empty()) output.write("TF_R108_"+role+"_STOPPED\r\n",false);
    output.flush(); output.progress("stopped");
    const auto received=receiver.status(); const auto counts=output.counts();
    std::ostringstream report;
    report<<"{\"schema\":\"termforge-workflow-fixture-v1\",\"fixture\":\"r108-console-v1\",\"role\":"<<workflow_quote(role)
        <<",\"generation\":"<<options.generation<<",\"status\":"<<workflow_quote(failure.empty()?"stopped":"failed")
        <<",\"failure\":"<<workflow_quote(failure)<<",\"source_bytes\":"<<counts.first<<",\"source_sha256\":"<<workflow_quote(output.digest())
        <<",\"records\":"<<counts.second<<",\"worker_joined\":true,\"console_window\":"<<(console_window?"true":"false")
        <<",\"receiver\":{\"units\":"<<received.received_units<<",\"bytes\":"<<received.saved_bytes
        <<",\"sha256\":"<<workflow_quote(workflow_file_sha256(options.capture))<<",\"joined\":"<<(received.worker_joined?"true":"false")<<",\"error\":"<<received.worker_error<<"},\"stages\":[";
    bool first=true;
    for(const auto& stage:stages) {
        if(!first) report<<','; first=false;
        report<<"{\"name\":"<<workflow_quote(stage.name)<<",\"first\":"<<stage.first<<",\"requested_records\":"<<stage.requested_records<<",\"records\":"<<stage.records
            <<",\"bytes\":"<<stage.bytes<<",\"source_begin\":"<<stage.source_begin<<",\"source_end\":"<<stage.source_end<<",\"source_sha256\":"<<workflow_quote(stage.source_sha256)
            <<",\"begin_ms\":"<<stage.begin_ms<<",\"end_ms\":"<<stage.end_ms<<",\"complete\":"<<(stage.complete?"true":"false")
            <<",\"target_block_ms\":"<<(options.role==L"A"?0:20)<<",\"late_blocks\":"<<stage.late_blocks<<",\"write_ms\":"<<stage.write_ms<<",\"max_write_ms\":"<<stage.max_write_ms<<'}';
    }
    report<<"]}\n"; workflow_save(manifest.get(),report.str()); workflow_check(FlushFileBuffers(manifest.get())!=FALSE,"workflow manifest flush failed");
    workflow_check(SetEvent(events[8].get())!=FALSE,"workflow stopped signal failed");
    workflow_check(failure.empty(),failure.c_str()); return 0;
}
} // namespace tf_shell_test
