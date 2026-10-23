"""Bounded R1 reference run in its own window station (including clipboard).

Requires an existing REA CLI; only its public instrument-with-frida entry is used.
Never switches the user's input desktop or reads the interactive clipboard.
"""
from __future__ import annotations
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import hashlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.launch import EXPECTED_FILES, StartupInfo, ProcessInfo, prepare, isolated_environment, check_profile_scope, digest, OwnedJob


def require_hosted_environment(environment):
    if environment.get("GITHUB_ACTIONS") != "true" or environment.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise ValueError("Current-station clipboard is allowed only on an explicitly requested disposable GitHub-hosted VM")


def cli_command(rea):
    rea = rea.resolve()
    if rea.suffix.lower() != ".cmd":
        return [str(rea)]
    package = rea.parent.parent / "rea-agents"
    metadata = json.loads((package / "package.json").read_text(encoding="utf-8"))
    if metadata.get("name") != "rea-agents" or metadata.get("version") != "6.7.0":
        raise ValueError("Expected locked REA public CLI entry")
    node = shutil.which("node")
    if not node:
        raise ValueError("Node.js CLI is unavailable")
    return [node, str(package / "scripts/rea.mjs")]


def run_cli(command, out, job, timeout):
    """Create suspended, assign the whole future tree, then resume the public CLI."""
    import msvcrt
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD,
                                     c.c_void_p, w.LPCWSTR, c.POINTER(StartupInfo), c.POINTER(ProcessInfo)]
    kernel.CreateProcessW.restype = w.BOOL
    kernel.ResumeThread.argtypes = [w.HANDLE]
    kernel.ResumeThread.restype = w.DWORD
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.WaitForSingleObject.restype = w.DWORD
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.GetExitCodeProcess.restype = w.BOOL
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.TerminateProcess.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    info, phase = ProcessInfo(), "create"
    check = OwnedJob.check
    with (out / "evidence.json").open("wb") as stdout, (out / "stderr.txt").open("wb") as stderr, open(os.devnull, "rb") as stdin:
        handles = [msvcrt.get_osfhandle(f.fileno()) for f in (stdin, stdout, stderr)]
        try:
            for handle in handles:
                os.set_handle_inheritable(handle, True)
            start = StartupInfo()
            start.cb, start.flags = c.sizeof(start), 0x100  # STARTF_USESTDHANDLES
            start.input, start.output, start.error = handles
            arguments = c.create_unicode_buffer(subprocess.list2cmdline([str(arg) for arg in command]))
            # CREATE_SUSPENDED | CREATE_NO_WINDOW: no target code can spawn yet.
            check(kernel.CreateProcessW(str(command[0]), arguments, None, None, True, 4 | 0x08000000,
                                        None, None, c.byref(start), c.byref(info)))
            phase = "job-assignment"
            job.assign(info.pid)
            phase = "resume"
            if kernel.ResumeThread(info.thread) == 0xffffffff:
                raise c.WinError(c.get_last_error())
            phase = "wait"
            wait = kernel.WaitForSingleObject(info.process, max(1, int(timeout * 1000)))
            if wait == 258:
                phase = "timeout"
                raise RuntimeError("REA observation timed out; owned Job terminated")
            if wait != 0:
                raise c.WinError(c.get_last_error())
            code = w.DWORD()
            check(kernel.GetExitCodeProcess(info.process, c.byref(code)))
            if code.value:
                (out / "launcher-failure.json").write_text(json.dumps({"reason": "rea-exit-failed"}), encoding="utf-8")
            return code.value
        except BaseException:
            reason = "rea-timeout" if phase == "timeout" else "rea-" + phase + "-failed"
            try:
                (out / "launcher-failure.json").write_text(json.dumps({"reason": reason}), encoding="utf-8")
            finally:
                # Recording failures must never bypass owned-process cleanup.
                try:
                    job.terminate()
                finally:
                    # Assignment failure leaves this owned suspended handle outside Job.
                    if info.process and kernel.WaitForSingleObject(info.process, 0) == 258:
                        terminated = kernel.TerminateProcess(info.process, 99)
                        terminate_error = c.get_last_error()
                        # Job termination may already be completing (ERROR_ACCESS_DENIED).
                        # Only a signaled owned handle can make that race acceptable.
                        if not terminated and kernel.WaitForSingleObject(info.process, 5000) != 0:
                            raise c.WinError(terminate_error)
                    if info.process and kernel.WaitForSingleObject(info.process, 5000) != 0:
                        raise RuntimeError("Owned CLI process did not terminate")
                if job.drain()["active"]:
                    raise RuntimeError("Owned CLI Job did not drain")
            raise
        finally:
            if info.thread:
                kernel.CloseHandle(info.thread)
            if info.process:
                kernel.CloseHandle(info.process)
            for handle in handles:
                os.set_handle_inheritable(handle, False)


def run(source: Path, out: Path, rea: Path, seconds: int, hosted_ci: bool = False, scenario: str = "text"):
    if scenario == "action-context" and not hosted_ci:
        raise ValueError("Action context requires disposable hosted VM")
    if hosted_ci:
        require_hosted_environment(os.environ)
    out = out.resolve()
    app = prepare(source, out)
    environment = isolated_environment(out)
    session = out / "profile/.wind/profiles/default.v10/terminal/user.sessions"
    rows = json.loads(session.read_text(encoding="utf-8"))
    fixture = Path(__file__).with_name("r1_fixture.py").resolve()
    peer = None
    if scenario in ("menu-wrap", "action-context"):
        from research.ui.r1_vt_fixture import Peer
        fixture = Path(__file__).with_name("r1_vt_fixture.py").resolve()
    else:
        rows[0]["process.arguments"] = f'/d /q /c ""{sys.executable}" -X utf8 "{fixture}" "{out}""'
    session.write_text(json.dumps(rows), encoding="utf-8")
    block = c.create_unicode_buffer("\0".join(f"{k}={v}" for k, v in sorted(environment.items())) + "\0")
    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    user.CreateWindowStationW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateWindowStationW.restype = w.HANDLE
    user.GetProcessWindowStation.restype = w.HANDLE
    user.SetProcessWindowStation.argtypes = [w.HANDLE]
    user.CloseWindowStation.argtypes = [w.HANDLE]
    user.GetUserObjectInformationW.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(StartupInfo), c.POINTER(ProcessInfo)]
    kernel.ResumeThread.argtypes = [w.HANDLE]
    kernel.ResumeThread.restype = w.DWORD
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.WaitForSingleObject.restype = w.DWORD
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    check = OwnedJob.check
    name = "TermForgeR1-" + str(kernel.GetCurrentProcessId())
    original = user.GetProcessWindowStation()
    station = original if hosted_ci else user.CreateWindowStationW(name, 0, 0xf037f, None)
    check(station)
    station_name, required = c.create_unicode_buffer(256), w.DWORD()
    desktop_name = name if hosted_ci else "Default"
    desktop, info, job = None, ProcessInfo(), None
    try:
        check(user.GetUserObjectInformationW(station, 2, station_name, c.sizeof(station_name), c.byref(required)))
        check(user.SetProcessWindowStation(station))
        desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
        check(desktop)
        job = OwnedJob()
        if scenario in ("menu-wrap", "action-context"):
            peer = Peer(out)
            rows = [{"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK",
                     "session.protocol": "Telnet", "session.target": "127.0.0.1", "session.port": peer.port,
                     "session.uuid": "00000000-0000-4000-8000-000000000001"}]
            session.write_text(json.dumps(rows), encoding="utf-8")
        start = StartupInfo()
        start.cb, start.desktop = c.sizeof(start), station_name.value + "\\" + desktop_name
        binary = app / "WindTerm.exe"
        command = c.create_unicode_buffer('"' + str(binary) + '"')
        check(kernel.CreateProcessW(str(binary), command, None, None, False, 4 | 0x400, block, str(app), c.byref(start), c.byref(info)))
        job.assign(info.pid)
        check(user.SetProcessWindowStation(original))
        script = Path(__file__).with_name("qt_r1_text.js").read_text(encoding="utf-8")
        if scenario in ("menu-wrap", "action-context"):
            script = script.split("// Scenario driver", 1)[0] + Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")
        if scenario == "action-context":
            script = script.split("send({kind:'probe_ready'", 1)[0] + Path(__file__).with_name("r1_action_context.js").read_text(encoding="utf-8")
        template_sha256 = hashlib.sha256(script.encode("utf-8")).hexdigest()
        script = script.replace("__UI_THREAD_ID__", str(info.tid)).replace("__OWNED_STATION__", station_name.value)
        script = script.replace("__RUN_DIRECTORY_JSON__", json.dumps(out.as_posix()))
        script = script.replace("__HOSTED_CI__", "true" if hosted_ci else "false")
        script = script.replace("__SCENARIO_TIMEOUT_MS__", str(max(1000, seconds * 1000 - 2000)))
        (out / "probe.js").write_text(script, encoding="utf-8")
        if kernel.ResumeThread(info.thread) == 0xffffffff:
            raise c.WinError(c.get_last_error())
        identity = {"pid": info.pid, "tid": info.tid, "sha256": digest(binary), "files_sha256": EXPECTED_FILES,
                    "script_sha256": digest(out / "probe.js"), "fixture_sha256": digest(fixture),
                    "template_sha256": template_sha256, "scenario": scenario,
                    "profile": "synthetic-local-only", "window_station": station_name.value,
                    "clipboard": "hosted-disposable-vm" if hosted_ci else "owned-window-station"}
        (out / "run.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
        print(json.dumps(identity), flush=True)
        time.sleep(2)
        code = run_cli(cli_command(rea) + ["instrument-with-frida", "attach", str(info.pid), "--script-file", str(out / "probe.js"),
                                          "--duration-ms", str(seconds * 1000), "--format", "json", "--full-output"], out, job, seconds + 15)
        print(json.dumps({"rea_exit": code, "evidence_bytes": (out / "evidence.json").stat().st_size}), flush=True)
        if code:
            raise RuntimeError("REA invocation failed; inspect local stderr")
    finally:
        restored = user.SetProcessWindowStation(original)
        try:
            if peer:
                peer.close()
            # A failed Job assignment leaves a suspended process outside the Job.
            # Its explicit owned handle must still be terminated and waited out.
            if info.process and kernel.WaitForSingleObject(info.process, 0) == 258:
                check(kernel.TerminateProcess(info.process, 99))
                if kernel.WaitForSingleObject(info.process, 5000) != 0:
                    raise RuntimeError("Owned process failed to terminate")
            if job:
                job.terminate()
                remaining = job.drain()
                (out / "cleanup.json").write_text(json.dumps(remaining), encoding="utf-8")
                print(json.dumps({"job_after": remaining}), flush=True)
                if remaining["active"]:
                    raise RuntimeError("Owned Job did not drain")
                if peer:
                    saved = json.loads(session.read_text(encoding="utf-8"))
                    valid = len(saved) == 1 and all(saved[0].get(k) == v for k, v in rows[0].items())
                    (out / "profile-check.json").write_text(json.dumps({"only_synthetic_session": valid, "session_count": len(saved)}), encoding="utf-8")
                    if not valid:
                        raise RuntimeError("Owned loopback profile changed scope")
                else:
                    check_profile_scope(out)
        finally:
            if job: job.close()
            if info.thread: kernel.CloseHandle(info.thread)
            if info.process: kernel.CloseHandle(info.process)
            if desktop: check(user.CloseDesktop(desktop))
            if not hosted_ci: check(user.CloseWindowStation(station))
            check(restored)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rea", type=Path, required=True)
    parser.add_argument("--seconds", type=int, choices=range(1, 181), default=35)
    parser.add_argument("--hosted-ci", action="store_true", help="Use the disposable GitHub-hosted VM clipboard; forbidden on normal hosts")
    parser.add_argument("--scenario", choices=("text", "menu-wrap", "action-context"), default="text")
    args = parser.parse_args()
    run(args.source, args.out, args.rea, args.seconds, args.hosted_ci, args.scenario)
