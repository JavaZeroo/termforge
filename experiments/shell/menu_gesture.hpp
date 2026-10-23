// Bounded, same-thread automation of an actual product TrackPopupMenu call.
// Ordinary launches have no observer, timer, hooks or synthetic input.
#pragma once
#include <windows.h>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf_shell_test {
class PopupMenuGesture {
    inline static thread_local PopupMenuGesture* observing_=nullptr;
    inline static thread_local bool cleanup_ok_=true;
    HWND owner_=nullptr, timer_=nullptr, popup_=nullptr;
    HMENU menu_=nullptr;
    UINT target_=0;
    DWORD thread_=0;
    int target_index_=-1, returned_command_=-1;
    unsigned navigation_keys_=0;
    UINT initial_state_=0, highlighted_state_=0, last_highlighted_command_=0;
    std::vector<UINT> enabled_commands_;
    RECT popup_rect_{}, item_rect_{};
    ULONGLONG deadline_=0, escape_deadline_=0;
    bool registered_=false, timer_armed_=false, queued_=false, highlighted_=false, returned_=false, done_=false;
    bool visible_=false, escaped_=false, ended_=false, timed_out_=false;
    const char* failure_=nullptr;
    static constexpr wchar_t class_name_[]=L"TermForgeOwnedPopupGesture";
    static void require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
    static std::wstring desktop_name(HDESK desktop) {
        wchar_t name[256]{}; DWORD used=0;
        require(GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&used)!=FALSE,"popup desktop identity query failed");
        return name;
    }
    bool key(UINT value) noexcept {
        const LPARAM flags=1|(static_cast<LPARAM>(MapVirtualKeyW(value,MAPVK_VK_TO_VSC))<<16);
        const HWND target=popup_?popup_:owner_;
        return PostMessageW(target,WM_KEYDOWN,value,flags)!=FALSE &&
               PostMessageW(target,WM_KEYUP,value,flags|(static_cast<LPARAM>(3)<<30))!=FALSE;
    }
    void fail(const char* why) noexcept {
        if(!failure_) failure_=why;
        timed_out_=true;
        if(!escaped_) { escaped_=true; escape_deadline_=GetTickCount64()+300; key(VK_ESCAPE); }
    }
    bool owned_menu_active() const noexcept {
        GUITHREADINFO info{}; info.cbSize=sizeof(info);
        return menu_ && IsMenu(menu_) && GetGUIThreadInfo(thread_,&info) && info.hwndMenuOwner==owner_;
    }
    static BOOL CALLBACK find_popup(HWND window,LPARAM parameter) {
        auto& self=*reinterpret_cast<PopupMenuGesture*>(parameter);
        wchar_t name[32]{}; DWORD process=0;
        if(!IsWindowVisible(window) || GetWindowThreadProcessId(window,&process)!=self.thread_ || process!=GetCurrentProcessId() ||
           !GetClassNameW(window,name,32) || lstrcmpW(name,L"#32768")!=0) return TRUE;
        RECT popup{}, item{};
        if(!GetWindowRect(window,&popup) || !GetMenuItemRect(self.owner_,self.menu_,static_cast<UINT>(self.target_index_),&item)) return TRUE;
        if(item.left<popup.left || item.top<popup.top || item.right>popup.right || item.bottom>popup.bottom ||
           item.right<=item.left || item.bottom<=item.top) return TRUE;
        self.popup_=window; self.popup_rect_=popup; self.item_rect_=item; self.visible_=true; return FALSE;
    }
    void poll() noexcept {
        if(done_) return;
        if(GetTickCount64()>=deadline_ && !timed_out_) fail("owned popup exceeded its bounded selection deadline");
        if(timed_out_) {
            if(GetTickCount64()>=escape_deadline_) {
                if(owned_menu_active()) { ended_=true; EndMenu(); }
                else done_=true;
            }
            return;
        }
        if(!menu_ || target_index_<0 || !owned_menu_active()) return;
        if(!popup_) EnumThreadWindows(thread_,find_popup,reinterpret_cast<LPARAM>(this));
        if(!popup_) return;
        if(!highlighted_) {
            for(const auto command:enabled_commands_) {
                MENUITEMINFOW observed{}; observed.cbSize=sizeof(observed); observed.fMask=MIIM_STATE;
                if(GetMenuItemInfoW(menu_,command,FALSE,&observed) && (observed.fState&MFS_HILITE)) last_highlighted_command_=command;
            }
            MENUITEMINFOW item{}; item.cbSize=sizeof(item); item.fMask=MIIM_STATE|MIIM_ID;
            if(!GetMenuItemInfoW(menu_,target_,FALSE,&item)) { fail("owned popup target state disappeared"); return; }
            if(item.wID==target_ && (item.fState&MFS_HILITE) && !(item.fState&(MFS_DISABLED|MFS_GRAYED))) {
                highlighted_=true; highlighted_state_=item.fState;
                if(!key(VK_RETURN)) fail("owned popup acceptance key could not be queued");
            } else {
                // Observe each native highlight before queuing the next key.
                // A posted Home was not a usable initial-selection contract
                // on the measured popup; never infer the highlighted item.
                if(navigation_keys_>=enabled_commands_.size()+1) { fail("owned popup navigation did not reach its visible target"); return; }
                ++navigation_keys_; queued_=true;
                if(!key(VK_DOWN)) fail("owned popup navigation key could not be queued");
            }
        }
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<PopupMenuGesture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<PopupMenuGesture*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && message==WM_TIMER) { self->poll(); return 0; }
        return DefWindowProcW(window,message,wp,lp);
    }
    void release() noexcept {
        if(owned_menu_active()) EndMenu();
        if(observing_==this) observing_=nullptr;
        if(timer_) {
            if(timer_armed_ && !KillTimer(timer_,1)) cleanup_ok_=false;
            timer_armed_=false;
            SetWindowLongPtrW(timer_,GWLP_USERDATA,0);
            if(!DestroyWindow(timer_)) cleanup_ok_=false;
            timer_=nullptr;
        }
        if(registered_) {
            if(!UnregisterClassW(class_name_,GetModuleHandleW(nullptr))) cleanup_ok_=false;
            registered_=false;
        }
    }
public:
    PopupMenuGesture(HWND owner,POINT open,UINT target,DWORD timeout_ms=2000):owner_(owner),target_(target),thread_(GetCurrentThreadId()) {
        DWORD process=0;
        require(!observing_ && IsWindow(owner_) && GetWindowThreadProcessId(owner_,&process)==thread_ && process==GetCurrentProcessId(),
                "popup gesture requires one same-thread owned window");
        const auto name=desktop_name(GetThreadDesktop(thread_));
        require(name.rfind(L"TermForgeShell-",0)==0,"popup gesture requires its owned non-input test desktop");
        USEROBJECTFLAGS station{}; DWORD used=0;
        require(GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&station,sizeof(station),&used)!=FALSE,
                "popup station visibility query failed");
        HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        require(input || !(station.dwFlags&WSF_VISIBLE),"popup gesture cannot verify its separation from the input desktop");
        if(input) {
            std::wstring input_name;
            try { input_name=desktop_name(input); } catch(...) { CloseDesktop(input); throw; }
            CloseDesktop(input);
            require(input_name!=name,"popup gesture must not drive the input desktop");
        }
        require(timeout_ms>=100 && timeout_ms<=5000,"popup gesture deadline outside bounded range");
        deadline_=GetTickCount64()+timeout_ms;
        try {
            cleanup_ok_=true;
            enabled_commands_.reserve(32);
            WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=procedure; cls.lpszClassName=class_name_;
            require(RegisterClassW(&cls)!=0,"popup observer class creation failed"); registered_=true;
            timer_=CreateWindowExW(0,class_name_,L"owned popup timer",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);
            require(timer_ && SetTimer(timer_,1,10,nullptr),"popup observer timer creation failed"); timer_armed_=true; observing_=this;
            require(PostMessageW(owner_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(open.x,open.y))!=FALSE &&
                    PostMessageW(owner_,WM_LBUTTONUP,0,MAKELPARAM(open.x,open.y))!=FALSE,
                    "product popup opening mouse messages could not be queued");
        } catch(...) { release(); throw; }
    }
    ~PopupMenuGesture() { release(); }
    PopupMenuGesture(const PopupMenuGesture&)=delete;
    PopupMenuGesture& operator=(const PopupMenuGesture&)=delete;
    static void opened(HWND owner,HMENU menu) noexcept {
        auto* self=observing_; if(!self || self->owner_!=owner || self->menu_) return;
        self->menu_=menu;
        const int count=GetMenuItemCount(menu);
        if(count<=0 || count>32) { self->fail("actual popup item count is outside its bounded scope"); return; }
        for(int index=0;index<count;++index) {
            MENUITEMINFOW item{}; item.cbSize=sizeof(item); item.fMask=MIIM_STATE|MIIM_ID|MIIM_FTYPE;
            if(!GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&item)) { self->fail("actual popup item observation failed"); return; }
            if(item.wID==self->target_) {
                self->target_index_=index; self->initial_state_=item.fState;
                if(item.fType&MFT_SEPARATOR || item.fState&(MFS_DISABLED|MFS_GRAYED)) self->fail("actual popup target is disabled");
            }
            if(!(item.fType&MFT_SEPARATOR) && !(item.fState&(MFS_DISABLED|MFS_GRAYED))) self->enabled_commands_.push_back(item.wID);
        }
        if(self->target_index_<0) self->fail("actual popup does not contain the requested command");
    }
    static void returned(HWND owner,HMENU menu,int command) noexcept {
        auto* self=observing_; if(!self || self->owner_!=owner || self->menu_!=menu) return;
        self->returned_=self->done_=true; self->returned_command_=command;
        if(self->timer_ && self->timer_armed_) {
            if(!KillTimer(self->timer_,1)) cleanup_ok_=false;
            else self->timer_armed_=false;
        }
    }
    bool complete() const noexcept { return done_; }
    static bool observer_idle() noexcept { return observing_==nullptr && cleanup_ok_; }
    void validate() const {
        require(done_ && returned_ && !failure_ && !timed_out_ && popup_ && visible_ && queued_ && highlighted_ &&
                returned_command_==static_cast<int>(target_) && !IsWindowVisible(popup_),
                "actual popup did not visibly highlight and return its requested command with bounded cleanup");
    }
    std::string json() const {
        std::ostringstream out;
        out<<"{\"schema\":\"termforge-owned-popup-menu-v1\",\"input_path\":\"queued-owner-mouse-and-popup-keys\","
           <<"\"owned_non_input_desktop\":true,\"target_command\":"<<target_<<",\"target_index\":"<<target_index_
           <<",\"initial_state\":"<<initial_state_<<",\"highlighted_state\":"<<highlighted_state_
           <<",\"last_highlighted_command\":"<<last_highlighted_command_
           <<",\"navigation_keys\":"<<navigation_keys_
           <<",\"actual_return_command\":"<<returned_command_<<",\"popup_hwnd\":"<<reinterpret_cast<uintptr_t>(popup_)
           <<",\"popup_class\":\"#32768\",\"popup_visible_observed\":"<<(visible_?"true":"false")
           <<",\"highlighted\":"<<(highlighted_?"true":"false")<<",\"returned\":"<<(returned_?"true":"false")
           <<",\"timed_out\":"<<(timed_out_?"true":"false")<<",\"escape_cleanup\":"<<(escaped_?"true":"false")
           <<",\"endmenu_cleanup\":"<<(ended_?"true":"false")<<",\"popup_closed\":"<<(!IsWindowVisible(popup_)?"true":"false")
           <<",\"failure\":";
        if(failure_) out<<'"'<<failure_<<'"'; else out<<"null";
        out
           <<",\"popup_rect\":["<<popup_rect_.left<<','<<popup_rect_.top<<','<<popup_rect_.right<<','<<popup_rect_.bottom
           <<"],\"item_rect\":["<<item_rect_.left<<','<<item_rect_.top<<','<<item_rect_.right<<','<<item_rect_.bottom
           <<"],\"enabled_commands\":[";
        for(size_t index=0;index<enabled_commands_.size();++index) { if(index) out<<','; out<<enabled_commands_[index]; }
        out<<"]}"; return out.str();
    }
};
inline void observe_popup_open(HWND owner,HMENU menu) noexcept { PopupMenuGesture::opened(owner,menu); }
inline void observe_popup_result(HWND owner,HMENU menu,int command) noexcept { PopupMenuGesture::returned(owner,menu,command); }
} // namespace tf_shell_test
