// Test isolation for TSF/IMM: OS text-service threads can live until process
// exit, so an application UI thread is not a sufficient desktop ownership unit.
#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>

namespace tf_shell {
inline std::wstring ime_desktop_name(HANDLE desktop) {
    wchar_t value[256]{}; DWORD used=0;
    if(!GetUserObjectInformationW(desktop,UOI_NAME,value,sizeof(value),&used))
        throw std::runtime_error("IME desktop identity query failed");
    return value;
}
inline bool is_ime_desktop_child() {
    return ime_desktop_name(GetThreadDesktop(GetCurrentThreadId())).rfind(L"TermForgeImeChild-",0)==0;
}
inline int run_ime_desktop_child(DWORD timeout_ms=45000,
                                 const wchar_t* child_switch=L"--owned-desktop-child",bool own_station=false) {
    const auto name=std::wstring(L"TermForgeImeChild-")+std::to_wstring(GetCurrentProcessId());
    const auto station_name=std::wstring(L"TermForgeImeStation-")+std::to_wstring(GetCurrentProcessId());
    const auto previous_station=GetProcessWindowStation(); HWINSTA station=nullptr;
    if(own_station) {
        station=CreateWindowStationW(station_name.c_str(),0,WINSTA_ALL_ACCESS,nullptr);
        if(!station) throw std::runtime_error("IME private station creation failed");
        if(!SetProcessWindowStation(station)) { CloseWindowStation(station); throw std::runtime_error("IME station attachment failed"); }
    }
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(own_station && !SetProcessWindowStation(previous_station)) {
        if(desktop) CloseDesktop(desktop); CloseWindowStation(station);
        throw std::runtime_error("IME parent station restoration failed");
    }
    if(!desktop) { if(station) CloseWindowStation(station); throw std::runtime_error("IME desktop creation failed"); }
    HANDLE job=nullptr; PROCESS_INFORMATION process{};
    const auto cleanup=[&] {
        if(job) { TerminateJobObject(job,1); if(process.hProcess) WaitForSingleObject(process.hProcess,5000); }
        if(process.hThread) CloseHandle(process.hThread);
        if(process.hProcess) CloseHandle(process.hProcess);
        if(job) CloseHandle(job);
        if(desktop) CloseDesktop(desktop);
        if(station) CloseWindowStation(station);
    };
    try {
        job=CreateJobObjectW(nullptr,nullptr);
        if(!job) throw std::runtime_error("IME job creation failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            throw std::runtime_error("IME job configuration failed");
        wchar_t executable[32768]{};
        const auto length=GetModuleFileNameW(nullptr,executable,32768);
        if(!length || length==32768) throw std::runtime_error("IME test executable path failed");
        auto command=L"\""+std::wstring(executable,length)+L"\" "+child_switch;
        auto desktop_path=ime_desktop_name(own_station ? station : previous_station)+L"\\"+name;
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.lpDesktop=desktop_path.data();
        // The child inherits the test harness's output handles. No clipboard,
        // input-desktop switch, global keyboard injection or user setting edits.
        startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        if(!CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,
                           nullptr,nullptr,&startup,&process))
            throw std::runtime_error("IME desktop child creation failed");
        if(!AssignProcessToJobObject(job,process.hProcess)) {
            TerminateProcess(process.hProcess,1); throw std::runtime_error("IME child job assignment failed");
        }
        if(ResumeThread(process.hThread)==static_cast<DWORD>(-1)) throw std::runtime_error("IME child resume failed");
        if(WaitForSingleObject(process.hProcess,timeout_ms)!=WAIT_OBJECT_0)
            throw std::runtime_error("IME child exceeded bounded deadline");
        DWORD code=1;
        if(!GetExitCodeProcess(process.hProcess,&code)) throw std::runtime_error("IME child exit query failed");
        // The process handle may signal just before Job accounting observes
        // its final removal. Wait for the same strict zero-process condition.
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        const auto drain_deadline=GetTickCount64()+5000;
        for(;;) {
            if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))
                throw std::runtime_error("IME job accounting query failed");
            if(!accounting.ActiveProcesses) break;
            if(GetTickCount64()>=drain_deadline) throw std::runtime_error("IME job did not fully drain");
            Sleep(10);
        }
        CloseHandle(process.hThread); process.hThread=nullptr;
        CloseHandle(process.hProcess); process.hProcess=nullptr;
        CloseHandle(job); job=nullptr;
        if(!CloseDesktop(desktop)) throw std::runtime_error("IME desktop close failed after child exit");
        desktop=nullptr;
        // Opening again proves whether any process/handle kept the desktop alive.
        if(station && !SetProcessWindowStation(station))
            throw std::runtime_error("IME station inspection attachment failed");
        HDESK remaining=OpenDesktopW(name.c_str(),0,FALSE,DESKTOP_READOBJECTS);
        const auto reopen_error=GetLastError();
        const bool restored=!station || SetProcessWindowStation(previous_station)!=FALSE;
        if(remaining) { CloseDesktop(remaining); throw std::runtime_error("IME private desktop still exists after child exit"); }
        if(!restored) throw std::runtime_error("IME station inspection restoration failed");
        if(reopen_error!=ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("IME desktop deletion could not be confirmed");
        if(station) {
            if(!CloseWindowStation(station)) throw std::runtime_error("IME private station close failed");
            station=nullptr;
            HWINSTA remaining_station=OpenWindowStationW(station_name.c_str(),FALSE,WINSTA_READATTRIBUTES);
            if(remaining_station) {CloseWindowStation(remaining_station);throw std::runtime_error("IME private station still exists after child exit");}
            if(GetLastError()!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("IME station deletion could not be confirmed");
        }
        return static_cast<int>(code);
    } catch(...) { cleanup(); throw; }
}
} // namespace tf_shell
