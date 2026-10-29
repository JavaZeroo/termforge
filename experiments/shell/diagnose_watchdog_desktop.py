"""Opt-in #160 desktop lifetime observation; never a product acceptance test."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.shell import run as shell

BASE = "6a10e5322a1bddeb038670e50f32ff6deac145cf"
SOURCE_FILES = (
    "experiments/shell/diagnose_watchdog_desktop.py",
    "experiments/shell/run.py", "experiments/conpty/run.py",
    "experiments/shell/test_launcher.cpp", "experiments/shell/shell.cpp",
    "experiments/shell/shell_launch.cpp",
)
OFFSETS = (0, 50, 250, 1000)
FIXED = {
    "schema": "termforge-watchdog-desktop-plan-v2",
    "source_base_commit": BASE,
    "cases": ["retained-second-handle", "original-watchdog"],
    "maximum_attempts_per_case": 1,
    "watchdog_timeout_seconds": 3,
    "followup_offsets_ms": list(OFFSETS),
    "followup_anchor": {"retained-second-handle": "second-handle-close-completed",
                        "original-watchdog": "original-first-probe-completed"},
    "maximum_windows_per_probe": 32,
    "whole_lifecycle_bound_seconds": None,
    "baseline_archive_sha256": "05392812a79a0b38d48384b47905c85c9f46285cbedcf0b4afa2275550c9efbb",
}
VARIABLE = {"execution_authorized", "execution_source_commit", "windows_version",
            "python_version", "source_sha256", "actual_compiled_executable",
            "runtime_inventory", "build_evidence", "baseline_archive", "output_directory"}


def identity(path):
    data = Path(path).read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def absolute(value):
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise ValueError("An actual absolute path is required")
    return Path(value).resolve(strict=True)


def bound_file(value):
    if not isinstance(value, dict) or set(value) != {"path", "bytes", "sha256"}:
        raise ValueError("A frozen file identity is required")
    path = absolute(value["path"])
    if type(value["bytes"]) is not int or identity(path) != {k: value[k] for k in ("bytes", "sha256")}:
        raise ValueError("Frozen file bytes differ")
    return path


def output_directory(value, runtime):
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise ValueError("A new absolute output directory is required")
    output, runtime = Path(value).resolve(), runtime.resolve(strict=True)
    if output == runtime or runtime in output.parents:
        raise ValueError("Diagnostic output must stay outside the frozen runtime")
    if output.exists():
        raise ValueError("Diagnostic output must be new")
    return output


def runtime_files(runtime):
    result = {}
    for member in runtime.rglob("*"):
        if member.is_symlink():
            raise ValueError("Runtime symlinks are unsupported")
        if member.is_file():
            result[member.relative_to(runtime).as_posix()] = identity(member)
    return result


def git_head():
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def verify_inputs(frozen):
    if git_head() != frozen["source_commit"]:
        raise ValueError("Execution source commit changed")
    for name, expected in frozen["files"].items():
        if identity(Path(name)) != expected:
            raise ValueError("A frozen input changed: " + name)
    if runtime_files(Path(frozen["runtime_directory"])) != frozen["runtime_files"]:
        raise ValueError("Complete frozen runtime inventory changed")


def finalize_batch(batch, frozen):
    # Success is assigned only after the postcheck, including file additions.
    batch["diagnostic_complete"] = False
    try:
        verify_inputs(frozen)
        batch["postcheck_inputs_unchanged"] = True
    except Exception as error:
        batch["postcheck_inputs_unchanged"] = False
        batch["postcheck_error"] = str(error)
        return
    batch["diagnostic_complete"] = len(batch["cases"]) == 2 and all(
        case["status"] == "observations_complete" for case in batch["cases"])


def require_observer_cleanup(record):
    if record.get("deferred_observer_errors"):
        raise OSError("Observer failed after the original sample was saved")


def load_plan(path):
    plan_bytes = path.read_bytes()
    plan = json.loads(plan_bytes.decode("utf-8-sig"))
    if not isinstance(plan, dict) or set(plan) != set(FIXED) | VARIABLE:
        raise ValueError("Unexpected plan fields")
    for key, expected in FIXED.items():
        if json.dumps(plan[key], sort_keys=True, allow_nan=False) != json.dumps(expected, sort_keys=True):
            raise ValueError("Unreviewed fixed plan contract: " + key)
    if type(plan["execution_authorized"]) is not bool or not plan["execution_authorized"]:
        raise ValueError("Execution is not authorized; no attempt/output created")
    if os.name != "nt" or struct.calcsize("P") != 8:
        raise ValueError("Windows x64 is required")
    if plan["windows_version"] != platform.version() or plan["python_version"] != sys.version:
        raise ValueError("Actual Windows/Python identity differs")
    head = git_head()
    if plan["execution_source_commit"] != head:
        raise ValueError("Execution source commit differs")
    hashes = plan["source_sha256"]
    if not isinstance(hashes, dict) or set(hashes) != set(SOURCE_FILES):
        raise ValueError("Complete source bindings are required")
    sources = {str(ROOT / name): identity(ROOT / name) for name in SOURCE_FILES}
    if any(sources[str(ROOT / name)]["sha256"] != hashes[name] for name in SOURCE_FILES):
        raise ValueError("Source bytes differ from the frozen plan")
    archive = absolute(plan["baseline_archive"])
    archive_identity = identity(archive)
    if archive_identity["sha256"] != FIXED["baseline_archive_sha256"]:
        raise ValueError("Original archive identity differs")
    exe = bound_file(plan["actual_compiled_executable"])
    if exe.name != "termforge_shell.exe":
        raise ValueError("Expected the original Win32 shell entry")
    inventory_path = bound_file(plan["runtime_inventory"])
    inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
    if not isinstance(inventory, dict) or not inventory:
        raise ValueError("Frozen runtime inventory is required")
    actual = runtime_files(exe.parent)
    if inventory != actual or not {"termforge_shell.exe", "shell_test_launcher.exe"} <= set(actual):
        raise ValueError("Complete runtime bytes differ")
    build_evidence = bound_file(plan["build_evidence"])
    output = output_directory(plan["output_directory"], exe.parent)
    files = {str(path): {"bytes": len(plan_bytes), "sha256": hashlib.sha256(plan_bytes).hexdigest()},
             str(archive): archive_identity, **sources}
    for key, bound in (("actual_compiled_executable", exe), ("runtime_inventory", inventory_path),
                       ("build_evidence", build_evidence)):
        files[str(bound)] = {field: plan[key][field] for field in ("bytes", "sha256")}
    frozen = {"files": files, "source_commit": head, "runtime_directory": str(exe.parent),
              "runtime_files": inventory}
    verify_inputs(frozen)
    return plan, exe, output, frozen


class Native:
    """Only owned handles/desktops and threads already known to this case."""
    def __init__(self, record):
        self.record = record
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.user.OpenDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.user.OpenDesktopW.restype = wintypes.HANDLE
        self.user.CloseDesktop.argtypes = [wintypes.HANDLE]
        self.user.CloseDesktop.restype = wintypes.BOOL
        self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumDesktopWindows.argtypes = [wintypes.HANDLE, self.callback, wintypes.LPARAM]
        self.user.EnumDesktopWindows.restype = wintypes.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        self.user.GetThreadDesktop.argtypes = [wintypes.DWORD]
        self.user.GetThreadDesktop.restype = wintypes.HANDLE
        self.user.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                       wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetUserObjectInformationW.restype = wintypes.BOOL
        self.kernel.GetCurrentProcess.restype = wintypes.HANDLE
        self.kernel.GetCurrentThreadId.restype = wintypes.DWORD
        self.kernel.DuplicateHandle.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE,
            ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.DuplicateHandle.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.kernel.IsProcessInJob.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
        self.kernel.IsProcessInJob.restype = wintypes.BOOL
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.GetProcessId.argtypes = [wintypes.HANDLE]
        self.kernel.GetProcessId.restype = wintypes.DWORD
        self.kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
            wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.kernel.GetProcessTimes.restype = wintypes.BOOL
        self.kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.GetExitCodeProcess.restype = wintypes.BOOL
        self.root = None
        self.root_identity = None
        self.desktop = None
        self.first = None
        self.last_job = None
        self.process = None
        self.root_observation_error = None
        self.followup_anchor = None

    def event(self, kind, **fields):
        row = {"kind": kind, "monotonic_ns": time.monotonic_ns(),
               "utc": datetime.now(timezone.utc).isoformat(), **fields}
        self.record["events"].append(row)
        return row

    def defer_observer_error(self, stage, error, **fields):
        self.record.setdefault("deferred_observer_errors", []).append(
            {"stage": stage, "error": str(error), "type": type(error).__name__, **fields})

    def own(self, value, kind):
        row = {"value": int(value), "kind": kind, "ownership": "owned", "close_attempted": False,
               "closed": False, "acquired_monotonic_ns": time.monotonic_ns()}
        self.record["handle_ledger"].append(row)
        return row

    def close(self, row, desktop=False):
        if row["close_attempted"]:
            raise OSError("A failed close is not retried")
        row["close_attempted"] = True
        ctypes.set_last_error(0)
        ok = (self.user.CloseDesktop if desktop else self.kernel.CloseHandle)(row["value"])
        row.update(close_return=bool(ok), close_error=0 if ok else ctypes.get_last_error(),
                   close_monotonic_ns=time.monotonic_ns(), closed=bool(ok))
        if not ok:
            raise ctypes.WinError(row["close_error"])

    def known_thread(self, tid, reason):
        ctypes.set_last_error(0)
        handle = self.user.GetThreadDesktop(tid)
        error = 0 if handle else ctypes.get_last_error()
        row = {"thread_id": tid, "reason": reason, "handle": int(handle) if handle else None,
               "ownership": "borrowed; never CloseDesktop", "error": error}
        self.record["borrowed_thread_desktops"].append(row)
        if not handle:
            raise ctypes.WinError(error)
        if handle:
            text = ctypes.create_unicode_buffer(256)
            needed = wintypes.DWORD()
            if not self.user.GetUserObjectInformationW(handle, 2, text, ctypes.sizeof(text), ctypes.byref(needed)):
                raise ctypes.WinError(ctypes.get_last_error())
            row["desktop_name"] = text.value
        return row

    def windows(self, handle):
        rows, callback_errors = [], []
        def visit(hwnd, unused):
            try:
                if len(rows) >= 32:
                    raise ValueError("Owned-desktop window observation exceeded 32")
                pid = wintypes.DWORD()
                tid = self.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if not tid:
                    raise ctypes.WinError(ctypes.get_last_error())
                rows.append({"hwnd": int(hwnd), "pid": pid.value, "thread_id": tid,
                             "thread_desktop": self.known_thread(tid, "owned desktop window")})
                return True
            except Exception as error:
                callback_errors.append(str(error))
                return False
        callback = self.callback(visit)
        ctypes.set_last_error(0)
        ok = self.user.EnumDesktopWindows(handle, callback, 0)
        if callback_errors:
            raise OSError(callback_errors[0])
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())
        return rows

    def probe(self, name, label, enumerate_windows=False, offset=None, anchor=None):
        row = self.event("desktop_probe", label=label, requested_offset_ms=offset,
                         actual_offset_ms=None if anchor is None else (time.monotonic_ns() - anchor) / 1e6)
        ctypes.set_last_error(0)
        handle = self.user.OpenDesktopW(name, 0, False, 1)  # Original DESKTOP_READOBJECTS access.
        row.update(open_returned_handle=bool(handle), open_error=0 if handle else ctypes.get_last_error(),
                   open_completed_monotonic_ns=time.monotonic_ns())
        if not handle:
            if row["open_error"] not in (2, 3):
                raise ctypes.WinError(row["open_error"])
            row["desktop_gone_at_this_probe"] = True
            return row
        ledger = self.own(handle, "desktop existence probe")
        try:
            row["desktop_gone_at_this_probe"] = False
            row["windows"] = self.windows(handle) if enumerate_windows else None
        finally:
            try:
                self.close(ledger, desktop=True)
            finally:
                row["probe_handle_close"] = dict(ledger)
        return row

    def root_state(self, label, *, strict=True):
        if self.root is None:
            return None
        row = self.event("retained_root", label=label, strict_before_GO=strict,
                         retained_handle=self.root["value"], pid=None, image=None, wait=None,
                         exit_code=None, creation_filetime_100ns=None, exit_filetime_100ns=None,
                         identity_reference=None, observation_gaps=[], api_results={name: {
                             "attempted": False, "returned": None, "error": None} for name in (
                             "WaitForSingleObject", "GetProcessTimes", "GetExitCodeProcess",
                             "GetProcessId", "QueryFullProcessImageNameW")})
        def observed(name, returned, ok):
            error = 0 if ok else ctypes.get_last_error()
            row["api_results"][name].update(attempted=True, returned=returned, error=error)
            if not ok:
                raise ctypes.WinError(error)
        ctypes.set_last_error(0)
        wait = self.kernel.WaitForSingleObject(self.root["value"], 0)
        row["wait"] = wait
        observed("WaitForSingleObject", wait, wait in (0, 258))
        if strict and wait != 258:
            raise OSError("Original retained root is not live before GO")
        created, exited, kernel, user = [wintypes.FILETIME() for _ in range(4)]
        ctypes.set_last_error(0)
        ok = self.kernel.GetProcessTimes(self.root["value"], ctypes.byref(created), ctypes.byref(exited),
                                         ctypes.byref(kernel), ctypes.byref(user))
        observed("GetProcessTimes", bool(ok), bool(ok))
        value = lambda item: (item.dwHighDateTime << 32) | item.dwLowDateTime
        row["creation_filetime_100ns"] = value(created)
        row["exit_filetime_100ns"] = value(exited) if wait == 0 else None
        code = wintypes.DWORD()
        ctypes.set_last_error(0)
        ok = self.kernel.GetExitCodeProcess(self.root["value"], ctypes.byref(code))
        observed("GetExitCodeProcess", bool(ok), bool(ok))
        row["exit_code"] = code.value
        ctypes.set_last_error(0)
        pid = self.kernel.GetProcessId(self.root["value"])
        row["pid"] = pid
        observed("GetProcessId", pid, bool(pid))
        if not pid or pid != self.process.pid:
            raise OSError("Retained root PID differs from original Popen")
        if not row["creation_filetime_100ns"]:
            raise OSError("Retained root creation time is unknown")
        identity = self.root_identity
        if identity is not None:
            row["identity_reference"] = dict(identity)
            if (self.root["value"], pid, row["creation_filetime_100ns"]) != (
                    identity["retained_handle"], identity["pid"], identity["creation_filetime_100ns"]):
                raise OSError("Retained root handle/PID/creation differs from its pre-GO identity")
        image, size = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
        ctypes.set_last_error(0)
        ok = self.kernel.QueryFullProcessImageNameW(self.root["value"], 0, image, ctypes.byref(size))
        error = 0 if ok else ctypes.get_last_error()
        row["api_results"]["QueryFullProcessImageNameW"].update(attempted=True, returned=bool(ok), error=error)
        if not ok:
            if strict or wait != 0 or error != 31 or identity is None:
                raise ctypes.WinError(error)
            row["observation_gaps"].append("Exited retained handle image query failed31; current image unknown; identity reference was verified before GO")
            return row
        row["image"] = image.value
        if Path(image.value).resolve() != self.expected_root_image:
            raise OSError("Retained root image differs from original launcher")
        current = {"retained_handle": self.root["value"], "pid": pid, "image": str(Path(image.value).resolve()),
                   "creation_filetime_100ns": row["creation_filetime_100ns"]}
        if identity is None:
            if not strict:
                raise OSError("Missing strictly verified pre-GO retained root identity")
            self.root_identity = current
            self.record["pre_GO_root_identity"] = dict(current)
        elif current != identity:
            raise OSError("Retained root identity differs from its pre-GO proof")
        row["identity_reference"] = dict(self.root_identity)
        return row

    def later_probes(self):
        anchor = self.followup_anchor
        if anchor is None:
            first = self.first
            anchor = first.get("probe_handle_close", {}).get("close_monotonic_ns", first["open_completed_monotonic_ns"])
        self.record["followup_anchor_monotonic_ns"] = anchor
        for offset in OFFSETS:
            remaining = anchor + offset * 1_000_000 - time.monotonic_ns()
            if remaining > 0:
                time.sleep(remaining / 1e9)
            self.probe(self.desktop.name, "independent-followup", True, offset, anchor)


@contextmanager
def observed_shell(native):
    """Process-local observation seam; production launcher and source stay unchanged."""
    original_desktop, original_job, original_subprocess = shell.OwnedTestDesktop, shell.OwnedJob, shell.subprocess
    class Desktop(original_desktop):
        def __init__(self):
            super().__init__()
            native.desktop = self
            self.ledger = native.own(self.handle, "creator desktop")
            native.event("desktop_created", name=self.name, full_name=self.full_name)

        def close(self):
            if self.handle:
                native.close(self.ledger, desktop=True)
                self.handle = None
                native.event("creator_closed", handle_ledger=dict(self.ledger))
            if native.first is not None:
                raise OSError("The original first desktop probe is not repeated")
            native.first = native.probe(self.name, "original-first-probe")
            native.record["original_first_desktop_gone"] = native.first["desktop_gone_at_this_probe"]
            return native.first["desktop_gone_at_this_probe"]

    class Job(original_job):
        def __init__(self):
            super().__init__()
            self.ledger = native.own(self.handle, "exact supervisor Job")

        def assign(self, pid):
            if native.root_observation_error:
                raise OSError(native.root_observation_error)
            if native.root is None or native.kernel.GetProcessId(native.root["value"]) != pid:
                raise OSError("Retained Popen root identity differs before GO")
            self.check(self.api.AssignProcessToJobObject(self.handle, native.root["value"]))
            member = wintypes.BOOL()
            self.check(native.kernel.IsProcessInJob(native.root["value"], self.handle, ctypes.byref(member)))
            if not member.value:
                raise OSError("Retained root is not in its exact Job")
            before = self.accounting()
            state = native.root_state("assigned-before-original-GO")
            if before != {"active": 1, "total": 1, "terminated": 0} or state["wait"] != 258:
                raise OSError("Original gated root is not alone and live before GO")
            native.event("exact_job_membership_before_GO", pid=pid, member=True, accounting=before)

        def accounting(self):
            result = super().accounting()
            if result != native.last_job:
                native.last_job = result
                try:
                    native.event("job_accounting", value=result)
                except Exception as error:
                    native.defer_observer_error("Job accounting observation", error, value=result)
            return result

        def terminate(self):
            before = None
            try:
                before = self.accounting()
                native.event("original_job_terminate", accounting=before)
            except Exception as error:
                native.defer_observer_error("Job terminate observation", error, accounting=before)
            super().terminate()

        def drain(self, timeout=5):
            try:
                native.event("original_job_drain_begin", timeout_seconds=timeout)
            except Exception as error:
                native.defer_observer_error("Job drain begin observation", error)
            result = super().drain(timeout)
            try:
                native.root_state("after-original-drain", strict=False)
            except Exception as error:
                native.defer_observer_error("Retained root after drain observation", error)
            try:
                native.event("original_job_drain_end", value=result)
            except Exception as error:
                native.defer_observer_error("Job drain end observation", error, value=result)
            return result

        def close(self):
            if self.handle:
                # __init__ failure can enter the inherited cleanup before ledger exists.
                ledger = getattr(self, "ledger", None) or native.own(self.handle, "exact supervisor Job")
                self.ledger = ledger
                if ledger["close_attempted"]:
                    return
                try:
                    native.close(ledger)
                except Exception as error:
                    # Original execute must reach its finally sample write.
                    # Defer observer failure only; never change its verdict.
                    native.defer_observer_error("exact Job close", error, handle_ledger=dict(ledger))
                    return
                self.handle = None

    class Processes:
        def __getattr__(self, key):
            return getattr(original_subprocess, key)

        def Popen(self, *args, **kwargs):
            if native.process is not None:
                raise OSError("Only one original watchdog process launch is allowed")
            process = original_subprocess.Popen(*args, **kwargs)
            native.process = process  # Keep the original Popen handle/object alive through observations.
            try:
                native.expected_root_image = Path(args[0][0]).resolve(strict=True)
                duplicate = wintypes.HANDLE()
                current = native.kernel.GetCurrentProcess()
                if not native.kernel.DuplicateHandle(current, int(process._handle), current, ctypes.byref(duplicate),
                                                      0, False, 2):
                    raise ctypes.WinError(ctypes.get_last_error())
                native.root = native.own(duplicate.value, "retained duplicate of original Popen process handle")
                native.record["popen_handle"] = {"value": int(process._handle), "ownership": "Popen; borrowed by observer"}
                native.root_state("created-before-assignment-and-GO")
            except Exception as error:
                # Return the Popen object before reporting this error from assign().
                # Original execute must own the gated, unassigned root for cleanup.
                native.root_observation_error = str(error)
                native.event("root_observation_failed_before_assignment", error=str(error))
            return process

    shell.OwnedTestDesktop, shell.OwnedJob, shell.subprocess = Desktop, Job, Processes()
    try:
        yield Desktop
    finally:
        shell.OwnedTestDesktop, shell.OwnedJob, shell.subprocess = original_desktop, original_job, original_subprocess


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_case(name, exe, output):
    record = {"case": name, "status": "failed", "events": [], "handle_ledger": [],
              "borrowed_thread_desktops": [], "original_first_desktop_gone": None,
              "original_product_verdict": None, "root_cause": None,
              "whole_lifecycle_bound_seconds": None}
    native = Native(record)
    held = None
    try:
        native.known_thread(native.kernel.GetCurrentThreadId(), "diagnostic caller; no desktop switch")
        with observed_shell(native) as Desktop:
            if name == "retained-second-handle":
                desktop = Desktop()
                extra = native.user.OpenDesktopW(desktop.name, 0, False, 1)
                if not extra:
                    raise ctypes.WinError(ctypes.get_last_error())
                held = native.own(extra, "positive-control retained second desktop handle")
                if desktop.close():
                    raise ValueError("Positive control vanished while its second handle was retained")
                record["positive_control_open_with_second_handle"] = True
                native.close(held, desktop=True)
                native.followup_anchor = held["close_monotonic_ns"]
                native.event("positive_control_second_handle_released")
            else:
                original_output = output / "original"
                try:
                    shell.execute(exe, "watchdog", original_output, timeout=3)
                except Exception as error:
                    record["original_execute_exception"] = str(error)
                sample = json.loads((original_output / "sample.json").read_text(encoding="utf-8"))
                record["original_sample"] = sample
                record["original_sample_identity"] = identity(original_output / "sample.json")
                record["original_product_verdict"] = sample["status"]
                require_observer_cleanup(record)
                if (sample.get("timed_out") is not True or sample.get("exit_code") != 99 or
                    json.loads(sample.get("stdout", "")) != {"watchdog_ready": True} or sample.get("stderr") != "" or
                    sample.get("job_at_timeout", {}).get("active", 0) < 2 or
                    sample.get("job_after", {}).get("active") != 0 or sample.get("cleanup_errors") or
                    sample.get("error") not in (None, "owned desktop remained")):
                    raise ValueError("Original watchdog/cleanup contract differs; no followup")
                if native.first is None or native.desktop.handle:
                    raise ValueError("Creator close/first probe did not complete; no followup")
        native.later_probes()
        record["status"] = "observations_complete"
    except Exception as error:
        record["error"] = str(error)
    finally:
        # Close remaining self-owned references once. Never retry a failed API close.
        for ledger in record["handle_ledger"]:
            if not ledger["close_attempted"]:
                try:
                    native.close(ledger, desktop="desktop" in ledger["kind"])
                except Exception as error:
                    record.setdefault("cleanup_errors", []).append(str(error))
        if any(not row["closed"] for row in record["handle_ledger"]):
            record["status"] = "failed"
        if record.get("cleanup_errors"):
            record["status"] = "failed"
        save(output / "diagnostic.json", record)
    return record


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--plan", type=Path, required=True)
    args = cli.parse_args()
    plan, exe, output, frozen = load_plan(args.plan.resolve(strict=True))
    output.mkdir(parents=False, exist_ok=False)
    save(output / "attempt.json", {"plan": frozen["files"][str(args.plan.resolve(strict=True))], "source_commit": plan["execution_source_commit"],
                                   "maximum_attempts_per_case": 1, "automatic_retry": False})
    batch = {"schema": "termforge-watchdog-desktop-diagnostic-v1", "source_commit": plan["execution_source_commit"],
             "cases": [], "diagnostic_complete": False, "product_acceptance": None, "root_cause": None}
    try:
        for name in FIXED["cases"]:
            directory = output / name
            directory.mkdir(exist_ok=False)
            result = run_case(name, exe, directory)
            batch["cases"].append({"case": name, "status": result["status"],
                "original_first_desktop_gone": result["original_first_desktop_gone"],
                "original_product_verdict": result["original_product_verdict"]})
            if result["status"] != "observations_complete":
                break
    finally:
        finalize_batch(batch, frozen)
        save(output / "batch.json", batch)
    print(json.dumps({"diagnostic_complete": batch["diagnostic_complete"], "cases_observed": len(batch["cases"]),
                      "product_acceptance": None, "root_cause": None}))
    return 0 if batch["diagnostic_complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
