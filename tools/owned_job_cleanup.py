"""Read-only identity audit of one already-owned Job; never enumerate global processes.

API contract: https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-jobobject_basic_process_id_list
WSL role: https://github.com/microsoft/WSL/blob/master/doc/docs/technical-documentation/wslhost.exe.md
The WSL source explains background hosting, not the cause of any observed residue.
Accepting an audited host for this disposable fixture is an explicit test policy.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from pathlib import PureWindowsPath
import time


class CleanupAuditError(RuntimeError):
    pass


def image_role(image, system_directory, program_files):
    """Only exact first-party WSL host locations qualify for fixture cleanup."""
    image = PureWindowsPath(image)
    roots = (PureWindowsPath(system_directory), PureWindowsPath(program_files) / "WSL")
    if any(image == root / "wslhost.exe" for root in roots):
        return "windows-wsl-host", "system32" if image.parent == roots[0] else "program-files-wsl"
    # Name-only observations are diagnostic, never sufficient for acceptance.
    return ("unverified-init" if image.name.casefold() == "init" else "unknown"), "unverified"


class JobMembers:
    LIMIT = 256

    def __init__(self, job, root_pid):
        self.job, self.api, self.root_pid = job, job.api, root_pid
        self.handles, self.private, self.members = [], [], []
        self.api.IsProcessInJob.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
        self.api.IsProcessInJob.restype = wintypes.BOOL
        self.api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                       ctypes.POINTER(wintypes.DWORD)]
        self.api.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetExitCodeProcess.restype = wintypes.BOOL
        self.api.GetSystemDirectoryW.argtypes = [wintypes.LPWSTR, wintypes.UINT]
        self.api.GetSystemDirectoryW.restype = wintypes.UINT
        system = ctypes.create_unicode_buffer(32768)
        size = self.api.GetSystemDirectoryW(system, len(system))
        if not 0 < size < len(system):
            raise CleanupAuditError("system_directory")
        import os
        self.pids = self.process_ids()
        try:
            for index, pid in enumerate(self.pids):
                public = {"member": index, "role": "root-worker" if pid == root_pid else "unknown",
                          "exact_job_membership_verified": False, "image_scope": "unverified",
                          "query_error": 0, "exited_after_cleanup": None, "exit_code_after_cleanup": None}
                private = {"member": index, "pid": pid, "image": None}
                self.members.append(public); self.private.append(private)
                handle = self.api.OpenProcess(0x1000 | 0x00100000, False, pid)
                self.handles.append(handle)
                if not handle:
                    public["query_error"] = ctypes.get_last_error()
                    continue
                contained = wintypes.BOOL()
                if not self.api.IsProcessInJob(handle, job.handle, ctypes.byref(contained)) or not contained.value:
                    public["query_error"] = ctypes.get_last_error() or 1
                    continue
                public["exact_job_membership_verified"] = True
                image = ctypes.create_unicode_buffer(32768)
                count = wintypes.DWORD(len(image))
                if not self.api.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(count)):
                    public["query_error"] = ctypes.get_last_error()
                    continue
                private["image"] = image.value
                if pid != root_pid:
                    public["role"], public["image_scope"] = image_role(image.value, system.value, os.environ["ProgramFiles"])
            self.stable = self.pids == self.process_ids()
        except BaseException:
            self.close()
            raise

    def process_ids(self):
        class ProcessIds(ctypes.Structure):
            _fields_ = [("assigned", wintypes.DWORD), ("listed", wintypes.DWORD),
                        ("ids", ctypes.c_size_t * self.LIMIT)]
        result = ProcessIds()
        if not self.api.QueryInformationJobObject(self.job.handle, 3, ctypes.byref(result),
                                                  ctypes.sizeof(result), None):
            raise CleanupAuditError("job_process_ids")
        if result.listed != result.assigned or result.listed > self.LIMIT:
            raise CleanupAuditError("job_process_list_incomplete")
        values = sorted(result.ids[:result.listed])
        if len(values) != len(set(values)) or any(pid <= 0 for pid in values):
            raise CleanupAuditError("job_process_list_identity")
        return values

    def observe_exit(self, timeout=1):
        # Job active==0 and each process handle signaled are separate facts.
        # Use one shared bound, rather than multiplying it by member count.
        deadline = time.monotonic() + timeout
        for public, handle in zip(self.members, self.handles):
            if not handle:
                continue
            remaining = max(0, int((deadline-time.monotonic())*1000))
            state = self.api.WaitForSingleObject(handle, remaining)
            if state not in (0, 258):
                public["query_error"] = ctypes.get_last_error() or 1
                continue
            public["exited_after_cleanup"] = state == 0
            if state == 0:
                code = wintypes.DWORD()
                if self.api.GetExitCodeProcess(handle, ctypes.byref(code)):
                    public["exit_code_after_cleanup"] = code.value
                else:
                    public["query_error"] = ctypes.get_last_error() or 1

    def close(self):
        for handle in self.handles:
            if handle:
                self.api.CloseHandle(handle)
        self.handles = []


def helper_cleanup_accepted(step, successful_worker, cleanup):
    """Fixture-only policy; product shell normal-exit contracts are unaffected."""
    before, after = cleanup.get("job_before"), cleanup.get("job_after")
    members = cleanup.get("members", [])
    return (step in ("check-before", "check-after") and successful_worker and
            cleanup.get("root_natural_exit") is True and cleanup.get("root_reaped") is True and
            cleanup.get("root_exit_code") == 0 and cleanup.get("membership_stable") is True and
            isinstance(before, dict) and isinstance(after, dict) and before.get("active", 0) > 0 and
            before["active"] == len(members) and after.get("active") == 0 and
            cleanup.get("job_after_pid_count") == 0 and cleanup.get("job_termination_requests") == 1 and
            all(member.get("role") == "windows-wsl-host" and
                member.get("image_scope") in ("system32", "program-files-wsl") and
                member.get("exact_job_membership_verified") is True and member.get("query_error") == 0 and
                member.get("exited_after_cleanup") is True and
                type(member.get("exit_code_after_cleanup")) is int for member in members))
