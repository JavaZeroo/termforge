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
# Canonical path->SHA256 manifest of the 4,046 copied files, independently
# checked against the pinned official ZIP. Includes DLLs, plugins and themes.
EXPECTED_FILES = "b5b18ab1ca7f9843d58a6496e5ce191f1cec48d16f5c697e62a4dcaf5f5fe3e8"


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
    files = [source / "WindTerm.exe", source / "qt.conf", *source.glob("*.dll")]
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
    (out / "profile").mkdir()
    (app / "profiles.config").write_text(json.dumps({
        "path": str(out / "profile").replace("\\", "/")
    }), encoding="utf-8")
    (out / "files.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return app


def launch(source: Path, out: Path, seconds: int):
    app = prepare(source, out)
    out = out.resolve()
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
        check(kernel.CreateProcessW(str(binary), command, None, None, False, 4, None,
                                    str(app), c.byref(start), c.byref(info)))
        job.assign(info.pid)  # Suspended until containment succeeds.
        script = Path(__file__).with_name("qt_widgets.js").read_text(encoding="utf-8")
        script = script.replace("__UI_THREAD_ID__", str(info.tid))
        (out / "probe.js").write_text(script, encoding="utf-8")
        if kernel.ResumeThread(info.thread) == 0xffffffff:
            raise c.WinError(c.get_last_error())
        identity = {"pid": info.pid, "tid": info.tid, "sha256": digest(binary), "files_sha256": EXPECTED_FILES,
                    "script_sha256": digest(out / "probe.js"), "seconds": seconds,
                    "profile": "fresh", "desktop": "owned-non-input", "probe": str(out / "probe.js")}
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
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows is required")
    launch(args.source, args.out, args.seconds)
