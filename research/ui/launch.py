"""Run a pinned WindTerm copy on an owned, non-input Windows desktop.

Writes an attach-only Frida script; does not install or invoke Frida itself.
Never pass a user profile. Output must be a new directory under local/.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob

EXPECTED = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"
# Canonical path->SHA256 manifest of the 4,047 copied files, independently
# checked against the pinned official ZIP. Includes DLLs, plugins and themes.
EXPECTED_FILES = "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"


class StartupInfo(c.Structure):
    _fields_ = [("cb", w.DWORD), ("reserved", w.LPWSTR), ("desktop", w.LPWSTR),
                ("title", w.LPWSTR), ("x", w.DWORD), ("y", w.DWORD),
                ("cx", w.DWORD), ("cy", w.DWORD), ("chars_x", w.DWORD),
                ("chars_y", w.DWORD), ("fill", w.DWORD), ("flags", w.DWORD),
                ("show", w.WORD), ("reserved2", w.WORD), ("bytes", c.c_void_p),
                ("input", w.HANDLE), ("output", w.HANDLE), ("error", w.HANDLE)]


class ProcessInfo(c.Structure):
    _fields_ = [("process", w.HANDLE), ("thread", w.HANDLE),
                ("pid", w.DWORD), ("tid", w.DWORD)]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest_digest(manifest):
    canonical = json.dumps(manifest, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def prepare(source: Path, out: Path):
    source, out = source.resolve(), out.resolve()
    if digest(source / "WindTerm.exe") != EXPECTED:
        raise ValueError("Expected the pinned official 2.7.0 x64 executable")
    if not out.is_relative_to((ROOT / "local").resolve()) or out == (ROOT / "local").resolve():
        raise ValueError("Output must be a new child directory under local/")
    if out.is_relative_to(source) or source.is_relative_to(out):
        raise ValueError("Source and output must not overlap")
    out.mkdir(parents=True, exist_ok=False)
    app = out / "app"
    app.mkdir()
    # Explicit distribution subset: no profiles, dumps, history or vendor tools.
    files = [source / "WindTerm.exe", source / "winpty-agent.exe", source / "qt.conf", *source.glob("*.dll")]
    for directory in ("global", "plugins", "terminal"):
        files.extend(p for p in (source / directory).rglob("*") if p.is_file())
    manifest = {}
    for original in files:
        if original.is_symlink() or not original.resolve().is_relative_to(source):
            raise ValueError("Distribution file resolves outside the source tree")
        relative = original.relative_to(source)
        destination = app / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, destination)
        manifest[relative.as_posix()] = digest(destination)
    if manifest["WindTerm.exe"] != EXPECTED:
        raise ValueError("Copied executable changed identity")
    if manifest_digest(manifest) != EXPECTED_FILES:
        raise ValueError("Distribution DLL, plugin or resource manifest differs from the pinned package")
    # The pinned binary treats a missing .wind/profiles directory as first run
    # and imports host OpenSSH/PuTTY sessions. Precreate it and supply exactly
    # one synthetic local shell; never copy an existing user's profile.
    sessions = out / "profile" / ".wind" / "profiles" / "default.v10" / "terminal"
    sessions.mkdir(parents=True)
    (sessions / "user.sessions").write_text(json.dumps([{
        "session.group": "TermForge Lab", "session.label": "TF_UI_CMD",
        "session.protocol": "Shell", "session.system": "cmd",
        "session.target": (Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32" / "cmd.exe").as_posix(),
        "session.uuid": "00000000-0000-4000-8000-000000000001",
        "process.arguments": "/d /q", "process.workingDirectory": (out / "host-home").as_posix()
    }], indent=2), encoding="utf-8")
    (app / "profiles.config").write_text(json.dumps({
        "path": str(out / "profile").replace("\\", "/")
    }), encoding="utf-8")
    (out / "files.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return app


def isolated_environment(out: Path, scale_factor: str | None = None):
    """Redirect home/cache discovery for the owned copy; not a filesystem sandbox."""
    home = out / "host-home"
    roaming, local = home / "AppData" / "Roaming", home / "AppData" / "Local"
    for folder in (home / ".ssh", roaming, local):
        folder.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    for name in ("QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_AUTO_SCREEN_SCALE_FACTOR",
                 "QT_ENABLE_HIGHDPI_SCALING", "QT_FONT_DPI", "QT_DEVICE_PIXEL_RATIO"):
        env.pop(name, None)
    env.update(HOME=str(home), USERPROFILE=str(home), HOMEDRIVE=home.drive,
               HOMEPATH=str(home)[len(home.drive):], APPDATA=str(roaming), LOCALAPPDATA=str(local))
    if scale_factor is not None:
        if scale_factor not in ("1", "1.5"):
            raise ValueError("Only controlled scale factors 1 and 1.5 are supported")
        env["QT_SCALE_FACTOR"] = scale_factor
    return env


def check_profile_scope(out: Path):
    """Record only the gate result, never unexpected imported session details."""
    path = out / "profile/.wind/profiles/default.v10/terminal/user.sessions"
    rows = json.loads(path.read_text(encoding="utf-8"))
    valid = (isinstance(rows, list) and len(rows) == 1 and isinstance(rows[0], dict) and
             rows[0].get("session.protocol") == "Shell" and rows[0].get("session.label") == "TF_UI_CMD" and
             rows[0].get("session.uuid") == "00000000-0000-4000-8000-000000000001")
    result = {"only_synthetic_session": valid, "session_count": len(rows) if isinstance(rows, list) else None}
    (out / "profile-check.json").write_text(json.dumps(result), encoding="utf-8")
    if not valid:
        raise RuntimeError("Owned profile contains unexpected session metadata")
    return result


def launch(source: Path, out: Path, seconds: int, probe="widgets", scale_factor=None):
    app = prepare(source, out)
    out = out.resolve()
    environment = isolated_environment(out, scale_factor)
    environment_block = c.create_unicode_buffer("\0".join(f"{key}={value}" for key, value in sorted(environment.items())) + "\0")
    kernel = c.WinDLL("kernel32", use_last_error=True)
    user = c.WinDLL("user32", use_last_error=True)
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL,
                                     w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(StartupInfo), c.POINTER(ProcessInfo)]
    kernel.ResumeThread.argtypes = [w.HANDLE]
    kernel.ResumeThread.restype = w.DWORD
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.WaitForSingleObject.restype = w.DWORD
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    check = OwnedJob.check
    desktop = user.CreateDesktopW("TermForgeUI-" + str(os.getpid()), None, None, 0, 0x1ff, None)
    check(desktop)
    info, job = ProcessInfo(), None
    try:
        job = OwnedJob()
        start = StartupInfo()
        start.cb, start.desktop = c.sizeof(start), "TermForgeUI-" + str(os.getpid())
        binary = app / "WindTerm.exe"
        command = c.create_unicode_buffer('"' + str(binary) + '"')
        check(kernel.CreateProcessW(str(binary), command, None, None, False, 4 | 0x400, environment_block,
                                    str(app), c.byref(start), c.byref(info)))
        job.assign(info.pid)  # Suspended until containment succeeds.
        script = Path(__file__).with_name("qt_" + probe + ".js").read_text(encoding="utf-8")
        script = script.replace("__UI_THREAD_ID__", str(info.tid))
        (out / "probe.js").write_text(script, encoding="utf-8")
        if kernel.ResumeThread(info.thread) == 0xffffffff:
            raise c.WinError(c.get_last_error())
        identity = {"pid": info.pid, "tid": info.tid, "sha256": digest(binary), "files_sha256": EXPECTED_FILES,
                    "script_sha256": digest(out / "probe.js"), "seconds": seconds,
                    "home": "owned-empty", "qt_scale_factor": scale_factor, "probe_kind": probe,
                    "profile": "synthetic-local-only", "desktop": "owned-non-input", "probe": str(out / "probe.js")}
        (out / "run.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
        print(json.dumps(identity), flush=True)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not (out / "STOP").exists():
            status = kernel.WaitForSingleObject(info.process, 100)
            if status == 0:
                break
            if status != 258:
                raise c.WinError(c.get_last_error())
    finally:
        try:
            # Also covers assignment failure before the suspended process joins the Job.
            if info.process and kernel.WaitForSingleObject(info.process, 0) == 258:
                check(kernel.TerminateProcess(info.process, 99))
                kernel.WaitForSingleObject(info.process, 5000)
            if job:
                job.terminate()
                remaining = job.drain()
                (out / "cleanup.json").write_text(json.dumps(remaining), encoding="utf-8")
                print(json.dumps({"job_after": remaining}), flush=True)
                if remaining["active"]:
                    raise RuntimeError("Owned Job did not drain")
                check_profile_scope(out)
        finally:
            if job:
                job.close()
            if info.thread:
                kernel.CloseHandle(info.thread)
            if info.process:
                kernel.CloseHandle(info.process)
            check(user.CloseDesktop(desktop))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=int, choices=range(1, 301), default=120, metavar="1..300")
    parser.add_argument("--probe", choices=("widgets", "typography"), default="widgets")
    parser.add_argument("--scale-factor", choices=("1", "1.5"))
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows is required")
    launch(args.source, args.out, args.seconds, args.probe, args.scale_factor)
