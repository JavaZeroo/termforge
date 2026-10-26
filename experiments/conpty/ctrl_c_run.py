"""Bounded raw cmd/ConPTY Ctrl-C diagnostic; no input desktop or reference app."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading
import time
import traceback

from experiments.build_paths import ROOT, binary
from experiments.conpty.run import OwnedJob

SCHEMA = "termforge-conpty-ctrl-c-v1"
LOOP = b"for /L %i in (1,1,1000000) do @echo TF_BUSY_%i\r"
INITIAL = b"@echo off\r\x03echo TF_AFTER_IDLE\r"
FOLLOWUP = b"\x03echo TF_AFTER_FLOOD\r"
COMPLETE_INPUT = INITIAL + LOOP + FOLLOWUP + b"exit 37\r"
NATIVE_FIELDS = {
    "schema", "status", "failure_code", "idle_marker", "flood_observed", "after_marker",
    "after_marker_lines", "highest_busy", "busy_hold_ms", "after_wait_ms", "input_bytes",
    "flood_input_bytes", "output_bytes", "exit_code", "read_error", "capture_overflow",
    "active_children_after", "cleanup_complete", "forced_process_cancel", "forced_reader_cancel", "elapsed_ms",
}
FAILURE_CODES = {
    "none", "input_artifact", "system_directory", "start", "prompt", "idle_input", "idle_marker",
    "flood_input", "flood_output", "interrupt_input", "interrupt_marker", "exit_input", "exit",
}
BOOL_FIELDS = {"idle_marker", "flood_observed", "after_marker", "capture_overflow",
               "cleanup_complete", "forced_process_cancel", "forced_reader_cancel"}
STRING_FIELDS = {"schema", "status", "failure_code"}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def plain_text(raw):
    """Independent ASCII marker oracle; command echo is never a marker line."""
    text = raw.decode("utf-8", "replace")
    text = re.sub(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)", "", text)
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    return text.replace("\r", "").replace("\a", "")


def validate_trace(raw, sent):
    if sent != COMPLETE_INPUT:
        raise ValueError("input_identity")
    text = plain_text(raw)
    lines = text.split("\n")[:-1]  # a partial last line cannot establish execution
    if lines.count("TF_AFTER_IDLE") != 1 or lines.count("TF_AFTER_FLOOD") != 1:
        raise ValueError("marker_identity")
    busy = [(i, int(match.group(1))) for i, line in enumerate(lines)
            if (match := re.fullmatch(r"TF_BUSY_(\d+)", line))]
    if not busy or not 0 < max(number for _, number in busy) < 1000000:
        raise ValueError("loop_not_interrupted")
    idle, after = lines.index("TF_AFTER_IDLE"), lines.index("TF_AFTER_FLOOD")
    if not idle < busy[0][0] <= busy[-1][0] < after:
        raise ValueError("marker_order")
    return {"idle_marker_lines": 1, "after_marker_lines": 1,
            "highest_busy": max(number for _, number in busy), "busy_lines": len(busy)}


def system_directory():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.GetSystemDirectoryW.argtypes = [wintypes.LPWSTR, wintypes.UINT]
    api.GetSystemDirectoryW.restype = wintypes.UINT
    buffer = ctypes.create_unicode_buffer(32768)
    size = api.GetSystemDirectoryW(buffer, len(buffer))
    if not size or size >= len(buffer):
        raise OSError("system_directory")
    return Path(buffer.value)


def file_version(path):
    api = ctypes.WinDLL("version", use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p),
                                 ctypes.POINTER(wintypes.UINT)]
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(unused))
    if not size:
        raise OSError("file_version")
    data = ctypes.create_string_buffer(size)
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    if not api.GetFileVersionInfoW(str(path), 0, size, data) or not api.VerQueryValueW(
            data, "\\", ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise OSError("file_version")
    fixed = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD * 13)).contents
    if fixed[0] != 0xFEEF04BD:
        raise OSError("file_version")
    versions = {}
    for name, offset in (("fixed_file_version", 2), ("fixed_product_version", 4)):
        versions[name] = ".".join(str(value) for value in (
            fixed[offset] >> 16, fixed[offset] & 65535, fixed[offset + 1] >> 16, fixed[offset + 1] & 65535))
    # The localized resource string can differ from VS_FIXEDFILEINFO. Preserve
    # both; never interpret a display-string difference as a new binary.
    if not api.VerQueryValueW(data, "\\VarFileInfo\\Translation", ctypes.byref(pointer), ctypes.byref(length)):
        raise OSError("file_version_translation")
    if length.value < 4 or length.value % 4:
        raise OSError("file_version_translation")
    words = ctypes.cast(pointer, ctypes.POINTER(wintypes.WORD * (length.value // 2))).contents
    language, codepage = words[0], words[1]
    for key in ("FileVersion", "ProductVersion"):
        subblock = f"\\StringFileInfo\\{language:04x}{codepage:04x}\\{key}"
        if not api.VerQueryValueW(data, subblock, ctypes.byref(pointer), ctypes.byref(length)) or not length.value:
            raise OSError("file_version_string")
        versions["file_version_string" if key == "FileVersion" else "product_version_string"] = ctypes.wstring_at(
            pointer.value, length.value).rstrip("\0")
    return versions


def system_identity(directory):
    return {name: {"sha256": sha256(directory / name), **file_version(directory / name)}
            for name in ("cmd.exe", "conhost.exe")}


class OwnedImages:
    """Query only process IDs returned by this exact parent's owned Job."""
    def __init__(self, job, directory):
        self.job, self.directory = job, directory
        self.stop = threading.Event()
        self.observed = {"cmd.exe": False, "conhost.exe": False}
        self.error = False
        self.thread = threading.Thread(target=self.run, daemon=True)

    def sample(self):
        class Ids(ctypes.Structure):
            _fields_ = [("assigned", wintypes.DWORD), ("count", wintypes.DWORD),
                        ("ids", ctypes.c_size_t * 64)]
        ids = Ids()
        if not self.job.api.QueryInformationJobObject(self.job.handle, 3, ctypes.byref(ids),
                                                      ctypes.sizeof(ids), None) or ids.count > 64:
            raise OSError("owned_job_query")
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        api.OpenProcess.restype = wintypes.HANDLE
        api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                  wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        api.CloseHandle.argtypes = [wintypes.HANDLE]
        for pid in ids.ids[:ids.count]:
            handle = api.OpenProcess(0x1000, False, pid)
            if not handle:  # a short-lived owned process may have exited
                continue
            try:
                buffer, size = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
                if not api.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                    continue
                image = Path(buffer.value)
                if image.name.lower() in self.observed:
                    role = image.name.lower()
                    if os.path.normcase(str(image)) != os.path.normcase(str(self.directory / role)):
                        raise OSError("system_image_mismatch")
                    self.observed[role] = True
            finally:
                api.CloseHandle(handle)

    def run(self):
        try:
            while not self.stop.is_set():
                self.sample()
                self.stop.wait(0.02)
        except Exception:
            self.error = True

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=2)
        if self.thread.is_alive():
            self.error = True


def validate_sample(row):
    native, trace = row.get("native", {}), row.get("trace", {})
    if row.get("cleanup_errors") or not re.fullmatch(r"[0-9a-f]{64}", row.get("probe_sha256") or ""):
        raise ValueError("cleanup_or_probe_identity")
    if row.get("schema") != SCHEMA or row.get("timed_out") or row.get("returncode") != 0:
        raise ValueError("execution")
    if (set(native) != NATIVE_FIELDS or native.get("schema") != "termforge-ctrl-c-native-v1" or
            native.get("status") != "passed" or native.get("failure_code") != "none"):
        raise ValueError("native_identity")
    if any(native.get(key) is not True for key in (
            "idle_marker", "flood_observed", "after_marker", "cleanup_complete")):
        raise ValueError("native_lifecycle")
    if any(native.get(key) is not False for key in (
            "capture_overflow", "forced_process_cancel", "forced_reader_cancel")):
        raise ValueError("native_cleanup")
    if (native.get("exit_code") != 37 or native.get("active_children_after") != 0 or
            native.get("read_error") not in (0, 109) or native.get("input_bytes") != len(COMPLETE_INPUT) or
            native.get("flood_input_bytes") != len(FOLLOWUP) or native.get("after_marker_lines") != 1 or
            not 1000 <= native.get("busy_hold_ms", -1) < 15000 or
            not 0 <= native.get("after_wait_ms", -1) <= 5000 or
            not 0 < native.get("highest_busy", 0) < 1000000 or
            native.get("highest_busy") != trace.get("highest_busy") or trace.get("after_marker_lines") != 1):
        raise ValueError("interrupt_contract")
    if row.get("job_after", {}).get("active") != 0 or row.get("forced_job_cleanup"):
        raise ValueError("outer_cleanup")
    if row.get("image_query_error") or row.get("owned_images") != {"cmd.exe": True, "conhost.exe": True}:
        raise ValueError("system_image_identity")
    if row.get("system_files_before") != row.get("system_files_after"):
        raise ValueError("system_identity_changed")
    if set(row.get("system_files_before", {})) != {"cmd.exe", "conhost.exe"}:
        raise ValueError("system_identity")
    for identity in row.get("system_files_before", {}).values():
        if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", identity.get("fixed_file_version", "")) or not re.fullmatch(
                r"[0-9a-f]{64}", identity.get("sha256", "")):
            raise ValueError("system_identity")


def checked_native(value):
    """Do not copy arbitrary executable output or error strings into public JSON."""
    if (not isinstance(value, dict) or set(value) != NATIVE_FIELDS or
            value.get("schema") != "termforge-ctrl-c-native-v1" or
            value.get("status") not in ("passed", "failed") or value.get("failure_code") not in FAILURE_CODES):
        raise ValueError("native_identity")
    for key in BOOL_FIELDS:
        if type(value[key]) is not bool:
            raise ValueError("native_identity")
    for key in NATIVE_FIELDS - BOOL_FIELDS - STRING_FIELDS:
        if type(value[key]) is not int or not (-1 if key == "after_wait_ms" else 0) <= value[key] <= 0xFFFFFFFF:
            raise ValueError("native_identity")
    return value


def execute(exe, out, *, timeout=15, gate=b"GO\n"):
    """Always write a sanitized sample; raise on a normal workload failure.

    gate=None deliberately withholds GO for the watchdog regression. Guard
    cases return their failed/rejected record, without accepting the workload.
    """
    if os.name != "nt" or not 0 < timeout <= 30:
        raise ValueError("Windows and a bounded timeout are required")
    exe, out = Path(exe).resolve(), Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    private = out / "_private"
    private.mkdir()
    row = {"schema": SCHEMA, "status": "failed", "failure_code": "execution",
           "timed_out": False, "handler_delivery_verified": False, "performance_claim": False,
           "probe_sha256": None, "cleanup_errors": [], "job_after_cleanup": None,
           "process_reaped": None}
    job = process = images = None
    assigned = False
    stdout = stderr = b""
    cleanup_details = []
    started = time.monotonic()

    def cleanup_fault(code):
        row["cleanup_errors"].append(code)
        row["status"] = "failed"
        if row["failure_code"] != "cleanup_failed":
            row["failure_before_cleanup"] = row["failure_code"]
        row["failure_code"] = "cleanup_failed"
        cleanup_details.append(traceback.format_exc())

    try:
        row["probe_sha256"] = sha256(exe)
        directory = system_directory()
        row["system_files_before"] = system_identity(directory)
        job = OwnedJob()
        process = subprocess.Popen([str(exe)], cwd=private, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        images = OwnedImages(job, directory)
        images.start()
        try:
            if gate is None:
                process.wait(timeout=timeout)
                stdout, stderr = process.communicate(timeout=2)
            else:
                stdout, stderr = process.communicate(gate, timeout=timeout)
        except subprocess.TimeoutExpired:
            row["timed_out"] = True
            row["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=5)
        row["returncode"] = process.returncode
        row["job_after"] = job.drain()
        if row["job_after"]["active"]:
            row["forced_job_cleanup"] = True
            job.terminate()
            row["job_after"] = job.drain()
        images.finish()
        row["owned_images"] = images.observed
        row["image_query_error"] = images.error
        row["system_files_after"] = system_identity(directory)
        if gate == b"GO\n":
            row["native"] = checked_native(json.loads(stdout))
            try:
                row["trace"] = validate_trace((private / "output.bin").read_bytes(),
                                               (private / "input.bin").read_bytes())
            except (ValueError, OSError):
                row["failure_code"] = "trace_contract"
                raise ValueError("trace_contract") from None
            validate_sample(row)
            row.update(status="passed", failure_code="none")
        else:
            row["failure_code"] = "guard_test"
    except BaseException:
        # Preserve the original exception even if private evidence is unwritable.
        try:
            (private / "exception.txt").write_text(traceback.format_exc(), encoding="utf-8")
        except Exception:
            cleanup_fault("private_exception_write")
        raise
    finally:
        # Every cleanup stage is independent. In particular, a failed terminate
        # or pipe reap must never skip sampler shutdown, kill-on-close or JSON.
        try:
            if process is not None:
                try:
                    running = process.poll() is None
                except Exception:
                    running = True
                    cleanup_fault("process_poll")
                if running:
                    try:
                        if assigned:
                            job.terminate()
                        else:
                            process.kill()  # no GO was sent; no children exist
                    except Exception:
                        cleanup_fault("process_terminate")
                    try:
                        stdout, stderr = process.communicate(timeout=5)
                    except Exception:
                        cleanup_fault("process_communicate")
            if images is not None:
                try:
                    images.finish()
                except Exception:
                    cleanup_fault("image_sampler_finish")
            if job is not None and assigned:
                try:
                    row["job_after_cleanup"] = job.drain()
                except Exception:
                    # Unknown is explicit; a failed query never establishes zero.
                    row["job_after_cleanup"] = None
                    cleanup_fault("job_accounting")
                if row["job_after_cleanup"] is not None and row["job_after_cleanup"]["active"]:
                    try:
                        job.terminate()
                    except Exception:
                        cleanup_fault("job_terminate")
                    try:
                        row["job_after_cleanup"] = job.drain()
                        if row["job_after_cleanup"]["active"]:
                            cleanup_fault("job_not_empty")
                    except Exception:
                        row["job_after_cleanup"] = None
                        cleanup_fault("job_accounting")
        finally:
            # This is the final containment fallback when explicit termination
            # fails. It is attempted before writing any diagnostic artifacts.
            if job is not None:
                try:
                    job.close()
                except Exception:
                    cleanup_fault("job_close")
        if process is not None:
            try:
                if process.poll() is None:
                    stdout, stderr = process.communicate(timeout=5)
                row["process_reaped"] = process.poll() is not None
                if not row["process_reaped"]:
                    cleanup_fault("process_not_reaped")
            except Exception:
                row["process_reaped"] = None
                cleanup_fault("process_reap_after_close")
        for name, data in (("stdout.json", stdout), ("stderr.txt", stderr)):
            try:
                (private / name).write_bytes(data)
            except Exception:
                cleanup_fault("private_stream_write")
        if cleanup_details:
            try:
                (private / "cleanup.exception.txt").write_text("\n".join(cleanup_details), encoding="utf-8")
            except Exception:
                cleanup_fault("private_cleanup_write")
        row["wall_seconds"] = time.monotonic() - started
        row["artifacts"] = {}
        for name in ("input.bin", "output.bin", "stdout.json", "stderr.txt"):
            try:
                if (private / name).is_file():
                    row["artifacts"][name] = {"sha256": sha256(private / name), "bytes": (private / name).stat().st_size}
            except Exception:
                cleanup_fault("artifact_identity")
        try:
            (out / "sample.json").write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8")
        except Exception:
            cleanup_fault("public_sample_write")
    if row["cleanup_errors"]:
        raise ValueError("cleanup_failed")
    return row


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--exe", type=Path)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--repeats", type=int, default=3)
    args = cli.parse_args()
    if os.name != "nt" or not 1 <= args.repeats <= 20:
        cli.error("Windows and 1..20 fresh repeats are required")
    exe = args.exe or binary("conpty_ctrl_c_probe")
    args.out.mkdir(parents=True, exist_ok=False)
    sources = ["experiments/conpty/ctrl_c_probe.cpp", "experiments/conpty/session.hpp",
               "experiments/conpty/ctrl_c_run.py", "experiments/conpty/run.py", "CMakeLists.txt"]
    manifest = {"schema": SCHEMA, "status": "failed", "started_utc": datetime.now(timezone.utc).isoformat(),
                "probe_sha256": sha256(exe), "source_sha256": {name: sha256(ROOT / name) for name in sources},
                "source_binding": "recorded_inputs_not_independent_build_attestation",
                "workload": {"loop_count": 1000000, "busy_hold_min_ms": 1000, "flood_etx_count": 1,
                             "followup_delay_ms": 0, "marker_timeout_ms": 5000, "outer_timeout_seconds": 15},
                "handler_delivery_verified": False, "performance_claim": False, "requested_repeats": args.repeats,
                "passed_repeats": 0}
    try:
        for repeat in range(args.repeats):
            execute(exe, args.out / f"sample-{repeat}")
            manifest["passed_repeats"] += 1
            print(f"cmd raw Ctrl-C {repeat + 1}/{args.repeats}: passed", flush=True)
        manifest["status"] = "passed"
    except Exception:
        manifest["failure_code"] = "sample_failed"
        print("cmd raw Ctrl-C: failed; inspect private diagnostic artifacts", flush=True)
        return 1
    finally:
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
