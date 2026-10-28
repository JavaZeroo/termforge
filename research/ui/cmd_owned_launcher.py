"""Small owned-native boundary for independently identified cmd diagnostics."""
from __future__ import annotations
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import platform
import re
import sys
import time

from research.ui.launch import StartupInfo,ProcessInfo,OwnedJob,isolated_environment,digest
from research.ui.r1_launch import cli_command,run_cli
from benchmarks.tools.windterm_cmd import cleanup_owned,write_json,require


def render_template(template,out,run_id,thread_id,desktop_name):
    require(re.fullmatch('[0-9a-f]{32}',run_id) and type(thread_id) is int and thread_id>0,'Invalid owned script identity')
    replacements={'__UI_THREAD_ID__':str(thread_id),'__RUN_ID_JSON__':json.dumps(run_id),'__RUN_DIRECTORY_JSON__':json.dumps(out.as_posix()),'__DESKTOP_JSON__':json.dumps(desktop_name),'__TIMEOUT_MS__':'27000'}
    for key,value in replacements.items():
        require(template.count(key)==1,'Fixed script placeholder differs')
        template=template.replace(key,value)
    return template


def launch_owned(app,out,rea,run_id,template,identity):
    """Contain before resume, wait for CLI completion, and always drain owned Job."""
    require(sys.platform=='win32','Windows is required')
    cli=cli_command(Path(rea));require(Path(cli[0]).is_file(),'Existing REA CLI unavailable')
    environment=isolated_environment(out)
    block=c.create_unicode_buffer('\0'.join(f'{key}={value}' for key,value in sorted(environment.items()))+'\0')
    user,kernel=c.WinDLL('user32',use_last_error=True),c.WinDLL('kernel32',use_last_error=True)
    user.CreateDesktopW.argtypes,user.CreateDesktopW.restype=[w.LPCWSTR,w.LPCWSTR,c.c_void_p,w.DWORD,w.DWORD,c.c_void_p],w.HANDLE
    user.CloseDesktop.argtypes=[w.HANDLE]
    kernel.CreateProcessW.argtypes=[w.LPCWSTR,w.LPWSTR,c.c_void_p,c.c_void_p,w.BOOL,w.DWORD,c.c_void_p,w.LPCWSTR,c.POINTER(StartupInfo),c.POINTER(ProcessInfo)]
    kernel.ResumeThread.argtypes,kernel.ResumeThread.restype=[w.HANDLE],w.DWORD
    kernel.WaitForSingleObject.argtypes,kernel.WaitForSingleObject.restype=[w.HANDLE,w.DWORD],w.DWORD
    kernel.TerminateProcess.argtypes,kernel.CloseHandle.argtypes=[w.HANDLE,w.UINT],[w.HANDLE]
    check,info,job=OwnedJob.check,ProcessInfo(),None
    name='TermForgeCmdSettings-'+run_id;desktop=user.CreateDesktopW(name,None,None,0,0x1ff,None);check(desktop)
    try:
        job=OwnedJob();start=StartupInfo();start.cb,start.desktop=c.sizeof(start),name
        binary=app/'WindTerm.exe';command=c.create_unicode_buffer('"'+str(binary)+'"')
        check(kernel.CreateProcessW(str(binary),command,None,None,False,4|0x400,block,str(app),c.byref(start),c.byref(info)))
        job.assign(info.pid)
        script=render_template(template,out,run_id,info.tid,name)
        (out/'probe.js').write_text(script,encoding='utf-8',newline='\n')
        write_json(out/'run.json',{**identity,'run_id':run_id,'pid':info.pid,'tid':info.tid,'sha256':digest(binary),'script_sha256':digest(out/'probe.js'),
                   'profile':'synthetic-local-only','desktop':'owned-non-input','rea_capture_seconds':30,'cli_wait_timeout_seconds':45,'whole_lifecycle_bound_seconds':None,
                   'environment':{'os':platform.platform(),'python':platform.python_version()},'consumer_done':None,'presented_done':None})
        if kernel.ResumeThread(info.thread)==0xffffffff:raise c.WinError(c.get_last_error())
        time.sleep(2)
        code=run_cli(cli+['instrument-with-frida','attach',str(info.pid),'--script-file',str(out/'probe.js'),'--duration-ms','30000','--format','json','--full-output'],out,job,45)
        require(code==0,'REA invocation failed')
    except BaseException as error:
        if not (out/'launcher-failure.json').exists():write_json(out/'launcher-failure.json',{'reason':'settings-'+type(error).__name__})
        raise
    finally:
        try:cleanup_owned(kernel,info,job,out,run_id)
        finally:
            if job:job.close()
            if info.thread:kernel.CloseHandle(info.thread)
            if info.process:kernel.CloseHandle(info.process)
            check(user.CloseDesktop(desktop))
