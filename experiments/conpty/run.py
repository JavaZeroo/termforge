"""Unattended, owned-job ConPTY lifecycle checks (Windows 10 1809+)."""
from __future__ import annotations

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
import time

ROOT = Path(__file__).resolve().parents[2]


class BasicLimits(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                ("flags", wintypes.DWORD), ("min_ws", ctypes.c_size_t),
                ("max_ws", ctypes.c_size_t), ("active_limit", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling", wintypes.DWORD)]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [("basic", BasicLimits), ("io", ctypes.c_uint64 * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]


class Accounting(ctypes.Structure):
    _fields_ = [("times", ctypes.c_int64 * 4), ("page_faults", wintypes.DWORD),
                ("total", wintypes.DWORD), ("active", wintypes.DWORD),
                ("terminated", wintypes.DWORD)]


class OwnedJob:
    """Contain only a gated probe host and processes it subsequently creates."""
    def __init__(self):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.api.CreateJobObjectW.restype = wintypes.HANDLE
        self.api.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                     ctypes.c_void_p, wintypes.DWORD]
        self.api.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                       ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
        self.api.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.api.CreateJobObjectW(None, None)
        self.check(self.handle)
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        try:
            self.check(self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
        except BaseException:
            self.close()
            raise

    @staticmethod
    def check(value):
        if not value:
            raise ctypes.WinError(ctypes.get_last_error())

    def assign(self, pid):
        process = self.api.OpenProcess(0x0100 | 0x0001, False, pid)  # SET_QUOTA | TERMINATE
        self.check(process)
        try:
            self.check(self.api.AssignProcessToJobObject(self.handle, process))
        finally:
            self.api.CloseHandle(process)

    def accounting(self):
        result = Accounting()
        self.check(self.api.QueryInformationJobObject(self.handle, 1, ctypes.byref(result),
                                                     ctypes.sizeof(result), None))
        return {"active": result.active, "total": result.total, "terminated": result.terminated}

    def terminate(self):
        self.check(self.api.TerminateJobObject(self.handle, 99))

    def drain(self, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            result = self.accounting()
            if not result["active"] or time.monotonic() >= deadline:
                return result
            time.sleep(0.01)

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def binary_path() -> Path:
    directory = os.environ.get("TERMFORGE_BIN_DIR")
    if directory:
        return Path(directory) / "termforge_conpty_probe.exe"
    return ROOT / "build/conpty/Release/termforge_conpty_probe.exe"


# #21: Server 2022 duplicates one Job handle per ConPTY session inside the host
# process, so a non-startup sample can never be "steady" there. With this flag
# the batch records the counts instead of gating on them; every other contract
# still applies.
RECORD_HANDLE_GROWTH = os.environ.get("TERMFORGE_CONPTY_RECORD_HANDLES") == "1"


def check_sample(row):
    """Validate telemetry independently of the native boolean result."""
    if RECORD_HANDLE_GROWTH:
        assert row["error"] == "", row
    else:
        assert row["valid"] is True, row
    assert row["ready"] and row["echo"] and row["resized"]
    assert row["child_signaled"] and row["active_children_after"] == 0
    if not row["startup"] and not RECORD_HANDLE_GROWTH:
        assert row["steady_resources"]
        assert row["handles_after"] == row["handles_before"]
        assert row["threads_after"] == row["threads_before"]
    assert not row["capture_overflow"] and not row["forced_reader_cancel"] and not row["forced_process_cancel"]
    assert row["read_error"] in (0, 109) and row["output_bytes"] > 0
    if row["mode"] in ("normal", "large"):
        assert row["exit_code"] == 37
    elif row["mode"] == "cancel":
        assert row["exit_code"] == 91 and row["descendant_created"]
    elif row["mode"] == "close":
        assert row["exit_code"] != 259 and row["descendant_created"]
    else:
        raise AssertionError("Unexpected scenario")
    assert row["validated_records"] == (16384 if row["mode"] == "large" else 0)


def execute(binary: Path, mode: str, repeats=1, timeout=90):
    job = OwnedJob()
    process = None
    result = {"mode": mode, "status": "failed", "expected_timeout": mode == "watchdog"}
    try:
        process = subprocess.Popen([str(binary.resolve()), "--host", mode, str(repeats)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        # Native host waits for GO; no child/ConPTY exists before containment.
        job.assign(process.pid)
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=timeout)
            result["timed_out"] = False
        except subprocess.TimeoutExpired:
            result["timed_out"] = True
            result["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        result.update(returncode=process.returncode, stdout=stdout.decode("utf-8", "replace"),
                      stderr=stderr.decode("utf-8", "replace"), job_after=job.drain())
        if result["job_after"]["active"]:
            result["forced_job_cleanup"] = True
            job.terminate()
            result["job_after_forced_cleanup"] = job.drain()
        records = [json.loads(line) for line in result["stdout"].splitlines()]
        result["startup"] = records[0]
        assert result["startup"]["startup"]
        check_sample(result["startup"])
        if mode == "watchdog":
            assert result["timed_out"] and '"watchdog_ready":true' in result["stdout"], result
            assert result["job_at_timeout"]["active"] >= 3, result
            assert len(records) == 2 and records[1] == {"watchdog_ready": True}, result
        else:
            assert not result["timed_out"] and result["returncode"] == 0, result
            result["samples"] = records[1:]
            assert len(result["samples"]) == repeats
            for row in result["samples"]:
                check_sample(row)
        assert result["job_after"]["active"] == 0, result
        result["status"] = "passed"
        return result
    except BaseException as error:
        result["error"] = str(error)
        # Attach all captured evidence to the exception for CLI failure artifacts.
        if isinstance(error, Exception):
            error.conpty_result = result
        raise
    finally:
        if process is not None and process.poll() is None:
            # This fallback is only needed if assignment failed before GO.
            job.terminate()
            process.kill()
            process.communicate(timeout=10)
        job.close()
        artifacts = os.environ.get("TERMFORGE_TEST_ARTIFACTS")
        if artifacts:
            destination = Path(artifacts) / "conpty"
            destination.mkdir(parents=True, exist_ok=True)
            (destination / f"{mode}-{time.time_ns()}.json").write_text(
                json.dumps(result, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=binary_path())
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("ConPTY requires Windows")
    if not 1 <= args.repeats <= 20 or not args.binary.is_file():
        parser.error("Build the probe and use 1..20 repeats")
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": "termforge-conpty-lifecycle-v1", "status": "running",
                "started_utc": datetime.now(timezone.utc).isoformat(),
                "platform": platform.platform(), "python": platform.python_version(),
                "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(Path(__file__).parent.iterdir())
                                  if p.suffix in (".cpp", ".hpp", ".py") or p.name == "CMakeLists.txt"},
                "repeats": args.repeats, "performance_claim": False}
    try:
        for mode in ("normal", "large", "cancel", "close", "watchdog"):
            result = execute(args.binary, mode, args.repeats if mode != "watchdog" else 1,
                             timeout=8 if mode == "watchdog" else 90)
            (args.out / f"{mode}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            print(f"{mode}: passed", flush=True)
        manifest.update(status="passed", checked_sessions=args.repeats * 4,
                        checked_startup_sessions=5, checked_watchdogs=1)
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        (args.out / "failure.json").write_text(json.dumps(getattr(error, "conpty_result", {}), indent=2) + "\n")
        raise
    finally:
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
