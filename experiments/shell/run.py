"""Real cmd.exe integration on an owned non-input desktop; no user interactions."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary
from experiments.conpty.run import OwnedJob


def validate(row, mode):
    for field in ("valid", "cleanup_complete", "command_verified", "unicode_verified", "backspace_verified", "arrows_verified"):
        if row.get(field) is not True:
            raise ValueError(f"failed shell gate: {field}")
    if row["active_children_after"] != 0 or row["input_rejected"] != 0:
        raise ValueError("leftover child or rejected input")
    if not 0 < row["output_queue_high_water"] <= 256 * 1024 or not 0 < row["input_queue_high_water"] <= 16 * 1024:
        raise ValueError("queue budget violated")
    if row["frames"] < 2 or row["consumed_bytes"] <= 65536 or row["presented_done"] is not None:
        raise ValueError("missing output/frame or invented endpoint")
    if not 0 < row["max_consumed_per_tick"] <= 65536:
        raise ValueError("UI byte budget violated")
    if mode in ("resize", "resize-close"):
        if row.get("resize_verified") != (3 if mode == "resize" else 2) or (row.get("columns"), row.get("rows")) != (72, 18):
            raise ValueError("resize dimensions were not independently verified")
        if len(row["screen"]["lines"]) != 18:
            raise ValueError("resized screen height mismatch")
    if mode in ("normal", "resize"):
        if row["exit_code"] != 37 or not row["ctrl_c_verified"]:
            raise ValueError("shell interruption or normal exit not verified")
        if not 0 <= row["scripted_input_to_state_ms"] <= row["scripted_input_to_submit_ms"]:
            raise ValueError("incorrect state/submission timing")
    elif row["scripted_input_to_state_ms"] is not None or row["scripted_input_to_submit_ms"] is not None:
        raise ValueError("unobserved input endpoints must be null")


def desktop_gone(pid):
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.OpenDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenDesktopW.restype = wintypes.HANDLE
    api.CloseDesktop.argtypes = [wintypes.HANDLE]
    handle = api.OpenDesktopW(f"TermForgeShell-{pid}", 0, False, 1)
    if handle:
        api.CloseDesktop(handle)
        return False
    if ctypes.get_last_error() not in (2, 3):
        raise ctypes.WinError(ctypes.get_last_error())
    return True


def execute(exe, mode, out, timeout=40):
    out.mkdir(parents=True, exist_ok=False)
    job = OwnedJob()
    process = None
    record = dict(mode=mode, status="failed")
    try:
        process = subprocess.Popen([str(exe.resolve()), "--test", mode, str((out / "frame.bmp").resolve())],
                                   cwd=out, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=timeout)
            record["timed_out"] = False
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            record["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        record.update(exit_code=process.returncode, stdout=stdout.decode("utf-8", "replace"),
                      stderr=stderr.decode("utf-8", "replace"), job_after=job.drain())
        if record["job_after"]["active"]:
            raise ValueError("owned processes remained")
        record["desktop_gone"] = desktop_gone(process.pid)
        if not record["desktop_gone"]:
            raise ValueError("owned desktop remained")
        if mode != "watchdog" and (record["timed_out"] or record["exit_code"]):
            raise ValueError("shell probe failed: " + record["stderr"].strip())
        result = json.loads(record["stdout"])
        if mode == "watchdog":
            if not record["timed_out"] or result != {"watchdog_ready": True} or record["job_at_timeout"]["active"] < 2:
                raise ValueError("watchdog did not contain a live shell")
        else:
            if record["timed_out"] or record["exit_code"]:
                raise ValueError("shell probe execution failed")
            validate(result, mode)
            record["result"] = result
            record["frame_sha256"] = hashlib.sha256((out / "frame.bmp").read_bytes()).hexdigest()
        record["status"] = "passed"
        return record
    except BaseException as error:
        record["error"] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            job.terminate()
            process.communicate(timeout=10)
        job.close()
        (out / "sample.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--repeats", type=int, default=3)
    args = cli.parse_args()
    if not 1 <= args.repeats <= 10:
        cli.error("repeats must be 1..10")
    exe = binary("termforge_shell", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    files = ["experiments/shell/shell.cpp", "experiments/shell/queues.hpp", "experiments/shell/run.py",
             "experiments/shell/resize.hpp", "experiments/shell/size_client.cpp",
             "experiments/conpty/session.hpp", "experiments/presentation/render.hpp", "experiments/presentation/desktop.hpp",
             "experiments/core/terminal.hpp", "experiments/core/screen.hpp", "experiments/core/unicode_width.hpp"]
    meta = dict(status="running", platform=platform.platform(), started_utc=datetime.now(timezone.utc).isoformat(),
                exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                size_client_sha256=hashlib.sha256((exe.parent / "shell_size_client.exe").read_bytes()).hexdigest(),
                sources={f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
                input_endpoint="scripted Ctrl-C WM_CHAR acceptance to TF_AFTER state and GDI submission; not physical keyboard", presented_done=None)
    try:
        for repeat in range(args.repeats):
            for mode in ("normal", "close", "resize", "resize-close"):
                execute(exe, mode, args.out / f"{repeat}-{mode}")
                print(f"{repeat} {mode}: passed", flush=True)
        execute(exe, "watchdog", args.out / "watchdog", timeout=3)
        meta["status"] = "complete"
    except BaseException as error:
        meta.update(status="failed", error=str(error))
        raise
    finally:
        meta["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
