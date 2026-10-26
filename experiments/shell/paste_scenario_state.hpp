// Owned evidence and event gates for the real-window paste scenario.
#pragma once
#include "../conpty/session.hpp"
#include "paste.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace tf_shell_test {
inline void paste_check(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
class PasteReceiverGate {
    tf_conpty::Handle ready_, read_, checkpoint_, release_;
    static void create(tf_conpty::Handle& handle,const std::wstring& name) {
        handle.reset(CreateEventW(nullptr,TRUE,FALSE,name.c_str()));
        paste_check(handle.get()!=nullptr && GetLastError()!=ERROR_ALREADY_EXISTS,"paste event creation failed or reused a name");
    }
    static bool signalled(const tf_conpty::Handle& handle) {
        const auto status=WaitForSingleObject(handle.get(),0);
        paste_check(status==WAIT_OBJECT_0 || status==WAIT_TIMEOUT,"paste event observation failed");
        return status==WAIT_OBJECT_0;
    }
public:
    std::wstring prefix;
    explicit PasteReceiverGate(unsigned serial) {
        prefix=L"Local\\TermForgePaste-"+std::to_wstring(GetCurrentProcessId())+L"-"+
               std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(serial);
        create(ready_,prefix+L"-ready"); create(read_,prefix+L"-read");
        create(checkpoint_,prefix+L"-checkpoint"); create(release_,prefix+L"-release");
    }
    bool ready() const { return signalled(ready_); }
    bool checkpoint() const { return signalled(checkpoint_); }
    void allow_read() { paste_check(SetEvent(read_.get())!=FALSE,"paste read release failed"); }
    void release() { paste_check(SetEvent(release_.get())!=FALSE,"paste checkpoint release failed"); }
};
struct PasteWindowResult {
    std::string name;
    tf_shell::PasteProgress before, after;
    size_t checkpoint_bytes=0, received=0;
    bool flood=false;
};
struct PasteScenarioState {
    static constexpr size_t large_units=1000000;
    std::unique_ptr<PasteReceiverGate> receiver;
    std::shared_ptr<const tf_shell::PasteTransfer> transfer;
    std::vector<PasteWindowResult> results;
    PasteWindowResult pending;
    HWND target=nullptr;
    bool default_enter=false, escape_cancel=false, tab_enter=false;
    std::array<BYTE,256> original_keys{};
    bool keys_saved=false;
    ~PasteScenarioState() { if(keys_saved) SetKeyboardState(original_keys.data()); }
    void key(HWND destination,WPARAM code) {
        DWORD process=0; GetWindowThreadProcessId(destination,&process);
        paste_check(destination && IsWindow(destination) && process==GetCurrentProcessId() && GetFocus()==destination,
                    "paste test key must target its own focused control");
        if(!keys_saved) { paste_check(GetKeyboardState(original_keys.data())!=FALSE,"save paste keyboard state"); keys_saved=true; }
        BYTE clear[256]{}; paste_check(SetKeyboardState(clear)!=FALSE,"clear paste test modifiers");
        const auto scan=MapVirtualKeyW(static_cast<UINT>(code),MAPVK_VK_TO_VSC);
        const LPARAM down=1|(static_cast<LPARAM>(scan)<<16);
        paste_check(PostMessageW(destination,WM_KEYDOWN,code,down)!=FALSE &&
                    PostMessageW(destination,WM_KEYUP,code,down|(LPARAM(3)<<30))!=FALSE,"queue paste keyboard message");
    }
    void start_receiver(const std::filesystem::path& capture,const std::string& name) {
        receiver=std::make_unique<PasteReceiverGate>(static_cast<unsigned>(results.size()));
        transfer.reset(); pending={}; pending.name=name;
        paste_check(!std::filesystem::exists(capture),"paste receiver capture must be new");
    }
    std::wstring receiver_command(const std::filesystem::path& capture) const {
        wchar_t executable[32768]{}; const auto count=GetModuleFileNameW(nullptr,executable,32768);
        paste_check(count && count<32768,"paste receiver location failed");
        const auto client=std::filesystem::path(executable).parent_path()/L"shell_input_client.exe";
        return L"\""+client.wstring()+L"\" --paste-gated \""+capture.wstring()+L"\" \""+receiver->prefix+L"\"\r";
    }
    static std::string read(const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary); paste_check(file.good(),"paste evidence capture missing");
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
    void before_action(const std::filesystem::path& capture,bool flood) {
        paste_check(transfer!=nullptr && receiver->checkpoint(),"paste action requires a live transfer and receiver checkpoint");
        pending.before=transfer->progress(); pending.flood=flood;
        pending.checkpoint_bytes=read(capture).size();
        if (!(pending.checkpoint_bytes==768 && flood && pending.before.sent>=16384 &&
              pending.before.accepted<pending.before.total && !tf_shell::paste_terminal(pending.before.state))) {
            std::ostringstream details;
            details << "paste action did not occur during a partial transfer with consumed bytes and output flood"
                    << "; checkpoint=" << pending.checkpoint_bytes << " flood=" << flood
                    << " sent=" << pending.before.sent << " accepted=" << pending.before.accepted
                    << " total=" << pending.before.total << " state=" << static_cast<unsigned>(pending.before.state);
            throw std::runtime_error(details.str());
        }
    }
    void finish(const std::filesystem::path& capture,bool cancelled) {
        pending.after=transfer->progress(); const auto bytes=read(capture); pending.received=bytes.size();
        const auto& progress=pending.after;
        paste_check(progress.total==large_units*3 && progress.accepted<progress.total && progress.sent<=progress.accepted &&
                    progress.in_flight==0 && progress.retained_capacity<=32 &&
                    tf_shell::paste_terminal(progress.state) && progress.state!=tf_shell::PasteState::completed,
                    "paste cancellation/closure did not preserve bounded terminal progress");
        paste_check(!bytes.empty() && bytes.size()%3==0 && bytes.size()<=progress.sent,"paste consumed an invalid UTF8 prefix length");
        for(size_t i=0;i<bytes.size();i+=3)
            paste_check(bytes.compare(i,3,"\xe6\xb1\x89")==0,"paste receiver content differs from independent Han prefix");
        if(cancelled) paste_check(progress.state==tf_shell::PasteState::cancelled && progress.sent==progress.accepted &&
                                  bytes.size()==progress.sent,"normal window cancellation left an unaccounted tail");
        else paste_check(bytes.size()==768,"closed gated receiver consumed past its checkpoint");
        results.push_back(pending);
    }
    void save(const std::filesystem::path& path) const {
        paste_check(default_enter && escape_cancel && tab_enter && results.size()==2,"paste keyboard/transfer scenarios incomplete");
        std::ofstream out(path,std::ios::binary);
        out<<"{\"schema\":\"termforge-window-paste-v2\",\"keyboard\":{\"default_enter_accept\":true,"
             "\"default_escape_cancel\":true,\"tab_enter_accept\":true},\"cases\":[";
        for(size_t i=0;i<results.size();++i) {
            if(i) out<<',';
            const auto& row=results[i]; const auto& progress=row.after;
            const char* state=progress.state==tf_shell::PasteState::cancelled?"cancelled":"failed";
            out<<"{\"name\":\""<<row.name<<"\",\"target\":"<<progress.target<<",\"total\":"<<progress.total
               <<",\"before_sent\":"<<row.before.sent<<",\"before_accepted\":"<<row.before.accepted
               <<",\"accepted\":"<<progress.accepted<<",\"sent\":"<<progress.sent<<",\"in_flight\":"<<progress.in_flight
               <<",\"retained_capacity\":"<<progress.retained_capacity<<",\"state\":\""<<state
               <<"\",\"checkpoint_bytes\":"<<row.checkpoint_bytes<<",\"received\":"<<row.received
               <<",\"flood_observed\":"<<(row.flood?"true":"false")<<'}';
        }
        out<<"]}\n"; out.close(); paste_check(out.good(),"paste manifest write failed");
    }
};
} // namespace tf_shell_test
