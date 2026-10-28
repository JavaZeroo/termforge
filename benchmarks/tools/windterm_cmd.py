"""Owned WindTerm local-cmd correctness diagnostic; never a performance run.

The target runs on a new non-input desktop. No clipboard, global input or
display settings are touched. Raw target/evidence/profile files stay in local/.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.launch import (EXPECTED, EXPECTED_FILES, StartupInfo, ProcessInfo,
                                OwnedJob, prepare, isolated_environment,
                                check_profile_scope, digest)
from research.ui.r1_launch import cli_command, run_cli
from benchmarks.tools.play import write_all

SCHEMA = "termforge.windterm-cmd-diagnostic.v1"
PROBE = ROOT / "research/ui/qt_cmd_diagnostic.js"
MAX_LINES = 128
# subprocess.list2cmdline quotes for a C argv parser, not cmd.exe grammar.
CMD_PATH_META = frozenset('&|<>^()%!"')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def write_json(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2, allow_nan=False)
        stream.write("\n")


def workload(run_id):
    require(isinstance(run_id, str) and re.fullmatch(r"[0-9a-f]{32}", run_id), "Invalid run_id")
    lines = ["TF-CMD-BEGIN " + run_id]
    for index in range(12):
        token = hashlib.sha256(f"{run_id}:{index}".encode("ascii")).hexdigest()[:24]
        lines.append(f"TF-CMD-{index:03d} {token}")
    lines.append("TF-CMD-END " + run_id)
    return lines, ("\r\n".join(lines) + "\r\n").encode("ascii")


def console_state():
    class Coord(c.Structure):
        _fields_ = [("x", c.c_short), ("y", c.c_short)]
    class Rect(c.Structure):
        _fields_ = [("left", c.c_short), ("top", c.c_short), ("right", c.c_short), ("bottom", c.c_short)]
    class Info(c.Structure):
        _fields_ = [("size", Coord), ("cursor", Coord), ("attributes", w.WORD), ("window", Rect), ("max_size", Coord)]
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetStdHandle.argtypes, kernel.GetStdHandle.restype = [w.DWORD], w.HANDLE
    kernel.GetConsoleMode.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.SetConsoleMode.argtypes = [w.HANDLE, w.DWORD]
    kernel.GetConsoleScreenBufferInfo.argtypes = [w.HANDLE, c.POINTER(Info)]
    handle, mode, info = kernel.GetStdHandle(-11), w.DWORD(), Info()
    readable = bool(kernel.GetConsoleMode(handle, c.byref(mode)))
    enabled = bool(readable and kernel.SetConsoleMode(handle, mode.value | 4))
    sized = bool(kernel.GetConsoleScreenBufferInfo(handle, c.byref(info)))
    return {"vt_enable_succeeded": enabled, "original_mode": mode.value if readable else None,
            "columns": info.window.right - info.window.left + 1 if sized else None,
            "rows": info.window.bottom - info.window.top + 1 if sized else None,
            "buffer_columns": info.size.x if sized else None,
            "buffer_rows": info.size.y if sized else None}


def producer(out, run_id):
    """Write bounded synthetic data to inherited cmd console, then await cleanup."""
    import msvcrt
    out = Path(out).resolve()
    expected, data = workload(run_id)
    manifest = json.loads((out / "workload.json").read_text(encoding="utf-8"))
    require(manifest == {"run_id": run_id, "lines": expected, "payload_bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()}, "Producer workload differs")
    fd = sys.stdout.fileno()
    msvcrt.setmode(fd, os.O_BINARY)
    state = console_state()
    require(state["vt_enable_succeeded"], "Local console VT setup failed")
    # Clear startup cmd/banner contents before the independently checked payload.
    write_all(fd, b"\x1b[2J\x1b[H")
    start, status, error = time.perf_counter_ns(), "complete", None
    try:
        write_all(fd, data)
    except OSError as exc:
        status, error = "failed", type(exc).__name__
    end = time.perf_counter_ns()
    write_json(out / "producer.json", {"run_id": run_id, "status": status,
               "pid": os.getpid(), "sha256": manifest["sha256"], "payload_bytes": len(data),
               "clock": "perf_counter_ns", "t0_counter_ns": start,
               "producer_done_counter_ns": end if not error else None,
               "attempt_ended_counter_ns": end, "consumer_done": None, "presented_done": None,
               "console": state, "error": error})
    # Exit is not used as completion: keep the terminal document available.
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline and not (out / "STOP_PRODUCER").exists():
        time.sleep(.05)
    return 0 if status == "complete" else 1


def prepare_run(source, out, run_id):
    lines, data = workload(run_id)
    # These three paths enter process.arguments, which WindTerm passes to cmd /c.
    # Reject both supplied and resolved spellings before reading/copying the
    # distribution or creating output; C argv quoting cannot neutralise cmd.
    for original in (out, __file__, sys.executable):
        path = Path(original)
        for text in (str(path), str(path.resolve())):
            require(not any(char in CMD_PATH_META or ord(char) < 32 for char in text),
                    "Unsupported cmd path characters")
    app = prepare(Path(source), Path(out))
    out = Path(out).resolve()
    manifest = {"run_id": run_id, "lines": lines, "payload_bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}
    write_json(out / "workload.json", manifest)
    (out / "payload.bin").write_bytes(data)
    # Explicitly synthetic cmd invocation; no shell text is accepted from input.
    session = out / "profile/.wind/profiles/default.v10/terminal/user.sessions"
    rows = json.loads(session.read_text(encoding="utf-8"))
    invocation = subprocess.list2cmdline([sys.executable, "-B", "-X", "utf8", str(Path(__file__).resolve()),
                                        "producer", "--out", str(out), "--run-id", run_id])
    rows[0]["process.arguments"] = '/d /q /c "' + invocation + '"'
    session.write_text(json.dumps(rows, indent=2), encoding="utf-8")
    return app, manifest


def evidence_payloads(identity, evidence, script_digest):
    require(evidence.get("ok") is True, "REA invocation failed")
    record = evidence["data"]["evidence"]
    require(record.get("operation") == "instrument_with_frida", "Wrong observation operation")
    result = record["normalized_result"]
    require(result.get("source_sha256") == identity.get("script_sha256") == script_digest,
            "Probe source mismatch")
    require(result.get("pid") == identity.get("pid"), "Wrong owned target")
    require(result.get("messages_truncated") is False, "Truncated or unspecified observation")
    require(result.get("cleanup_error") is None and evidence["data"].get("cleanup_error") is None,
            "Frida cleanup failed")
    messages = result.get("messages", [])
    require(not any(m.get("type") == "error" for m in messages), "Frida script error")
    payloads = [m["payload"] for m in messages if m.get("type") == "send"]
    require(all(isinstance(p, dict) and p.get("run_id") == identity["run_id"] for p in payloads),
            "Probe run_id mismatch")
    require(not any(p.get("error") for p in payloads), "Probe reported an error")
    return payloads


def check_text(snapshot, expected):
    """Compare every logical line, permitting only empty unused terminal rows."""
    rows = snapshot.get("lines")
    require(isinstance(rows, list) and len(rows) <= MAX_LINES and
            all(isinstance(line, str) for line in rows), "Missing bounded full text")
    require(type(snapshot.get("line_count")) is int and snapshot["line_count"] == len(rows),
            "Incomplete text enumeration")
    # lineText is observed to exclude the logical newline. Do not trim spaces,
    # normalise characters, skip the beginning or accept a marker substring.
    require(rows[:len(expected)] == expected and all(line == "" for line in rows[len(expected):]),
            "Full text differs from oracle")
    require(len(rows) >= len(expected), "Missing payload lines")
    return {"status": "passed", "method": "all-bounded-logical-lines-exact",
            "payload_lines": len(expected), "observed_lines": len(rows),
            "terminal_text_sha256": hashlib.sha256(json.dumps(rows, separators=(",", ":")).encode()).hexdigest()}


def terminal_settings(snapshot):
    """Whitelist only validated scalar settings, never arbitrary probe objects."""
    capacity, geometry, font = (snapshot.get(k) for k in ("screen_line_capacity", "widget_geometry", "widget_font"))
    require(capacity is None or type(capacity) is int and 1 <= capacity <= 10000, "Invalid screen capacity")
    require(isinstance(geometry, list) and len(geometry) == 4 and
            all(type(value) is int and abs(value) <= 100000 for value in geometry), "Invalid widget geometry")
    require(isinstance(font, dict) and isinstance(font.get("family"), str) and
            len(font["family"]) <= 128 and not any(char in font["family"] for char in "\\/\r\n"), "Invalid font family")
    for key in ("point_size", "pixel_size"):
        value = font.get(key)
        require(type(value) in (int, float) and math.isfinite(value) and -1 <= value <= 10000, "Invalid font size")
    return {"screen_line_capacity": capacity, "widget_geometry": geometry,
            "widget_font": {key: font[key] for key in ("family", "point_size", "pixel_size")}}


def summarize(out):
    """Strict local replay and a deliberately small, publication-reviewable report."""
    out = Path(out)
    read = lambda name: json.loads((out / name).read_text(encoding="utf-8"))
    identity, manifest, cleanup, profile = (read(name) for name in
                                          ("run.json", "workload.json", "cleanup.json", "profile-check.json"))
    require(identity.get("schema") == SCHEMA and identity.get("sha256") == EXPECTED and
            identity.get("files_sha256") == EXPECTED_FILES, "Unpinned target identity")
    require(identity.get("profile") == "synthetic-local-only" and identity.get("desktop") == "owned-non-input",
            "Unknown target isolation")
    require(cleanup.get("run_id") == identity["run_id"] and cleanup.get("target_pid") == identity["pid"],
            "Cleanup run identity mismatch")
    require(cleanup.get("active") == 0, "Owned processes remain")
    require(profile == {"only_synthetic_session": True, "session_count": 1}, "Profile scope changed")
    lines, data = workload(identity["run_id"])
    require(manifest == {"run_id": identity["run_id"], "lines": lines, "payload_bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()}, "Oracle identity mismatch")
    require((out / "payload.bin").read_bytes() == data, "Payload identity mismatch")
    require(identity.get("runner_sha256") == digest(Path(__file__)), "Runner source changed since execution")
    result = {"schema": SCHEMA, "run_id": identity["run_id"], "kind": "correctness-diagnostic",
              "target_sha256": EXPECTED, "distribution_sha256": EXPECTED_FILES,
              "runner_sha256": identity["runner_sha256"], "probe_sha256": digest(out / "probe.js"),
              "payload_sha256": manifest["sha256"], "payload_bytes": len(data),
              "producer_status": "unknown", "correctness": {"status": "unknown"},
              "actual_settings": {}, "consumer_done": None, "presented_done": None,
              "performance_gate": "not_evaluated", "cleanup": {"owned_active": 0, "only_synthetic_session": True},
              "limitations": ["Instrumented local cmd diagnostic on a non-input desktop; no native input or presentation timing.",
                              "Small ASCII payload only; no T01 baseline, retained-history, Unicode or performance acceptance.",
                              "Widget font and console dimensions are readable settings, not complete C0 verification."]}
    problems = []
    if (out / "launcher-failure.json").exists():
        problems.append("launcher-failed")
    if (out / "producer.json").exists():
        produced = read("producer.json")
        require(produced.get("run_id") == identity["run_id"] and produced.get("sha256") == manifest["sha256"] and
                produced.get("payload_bytes") == len(data), "Producer identity mismatch")
        require(produced.get("consumer_done") is None and produced.get("presented_done") is None,
                "Producer cannot provide terminal endpoints")
        require(produced.get("status") == "complete" and produced.get("error") is None and
                type(produced.get("t0_counter_ns")) is int and type(produced.get("producer_done_counter_ns")) is int and
                produced["producer_done_counter_ns"] >= produced["t0_counter_ns"], "Producer did not complete")
        result["producer_status"] = "complete"
        console = produced["console"]
        require(isinstance(console, dict) and console.get("vt_enable_succeeded") is True, "Producer VT setup missing")
        selected = {"vt_enable_succeeded": True}
        for key in ("original_mode", "columns", "rows", "buffer_columns", "buffer_rows"):
            value = console.get(key)
            require(value is None or type(value) is int and 0 <= value <= 0xffffffff, "Invalid console setting")
            selected[key] = value
        result["actual_settings"]["producer_console"] = selected
    else:
        problems.append("producer-not-observed")
    if (out / "evidence.json").exists():
        try:
            payloads = evidence_payloads(identity, read("evidence.json"), digest(out / "probe.js"))
            finished = [p for p in payloads if p.get("kind") == "text_complete"]
            require(len(finished) == 1, "Full text snapshot not observed exactly once")
            require(result["producer_status"] == "complete", "Missing producer completion")
            result["correctness"] = check_text(finished[0], lines)
            result["actual_settings"]["terminal"] = terminal_settings(finished[0])
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            problems.append(type(exc).__name__ + ": " + str(exc))
    else:
        problems.append("probe-not-observed")
    if problems:
        result["correctness"] = {"status": "unknown", "reasons": problems}
    result["raw_records_sha256"] = {name: digest(out / name) for name in
                                      ("run.json", "workload.json", "producer.json", "evidence.json", "cleanup.json", "profile-check.json")
                                      if (out / name).exists()}
    return result


def cleanup_owned(kernel, info, job, out, run_id):
    """Record Job cleanup even if stopping an unassigned suspended target fails."""
    try:
        if info.process and kernel.WaitForSingleObject(info.process, 0) == 258:
            terminated = kernel.TerminateProcess(info.process, 99)
            error = c.get_last_error() if sys.platform == "win32" else 0
            # TerminateProcess is asynchronous; the owned handle must signal.
            if kernel.WaitForSingleObject(info.process, 5000) != 0:
                if not terminated:
                    raise OSError(error, "Owned target termination failed")
                raise RuntimeError("Owned target did not terminate")
    finally:
        if job:
            job.terminate()
            remaining = job.drain()
            write_json(out / "cleanup.json", {"run_id": run_id, "target_pid": info.pid, **remaining})
            require(remaining["active"] == 0, "Owned Job did not drain")
            check_profile_scope(out)


def run(source, out, rea, seconds=30):
    require(sys.platform == "win32", "Windows is required")
    require(type(seconds) is int and 10 <= seconds <= 60, "Diagnostic bound must be 10..60 seconds")
    # Resolve/validate the existing public CLI before creating target artifacts.
    cli = cli_command(Path(rea))
    require(Path(cli[0]).is_file(), "Existing REA CLI unavailable")
    out, run_id = Path(out).resolve(), uuid.uuid4().hex
    app, _ = prepare_run(source, out, run_id)
    environment = isolated_environment(out)
    block = c.create_unicode_buffer("\0".join(f"{k}={v}" for k, v in sorted(environment.items())) + "\0")
    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    user.CreateDesktopW.argtypes, user.CreateDesktopW.restype = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p], w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD,
                                     c.c_void_p, w.LPCWSTR, c.POINTER(StartupInfo), c.POINTER(ProcessInfo)]
    kernel.ResumeThread.argtypes, kernel.ResumeThread.restype = [w.HANDLE], w.DWORD
    kernel.WaitForSingleObject.argtypes, kernel.WaitForSingleObject.restype = [w.HANDLE, w.DWORD], w.DWORD
    kernel.TerminateProcess.argtypes, kernel.CloseHandle.argtypes = [w.HANDLE, w.UINT], [w.HANDLE]
    check, info, job = OwnedJob.check, ProcessInfo(), None
    desktop_name = "TermForgeCmd-" + run_id
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
    check(desktop)
    try:
        job = OwnedJob()
        start = StartupInfo()
        start.cb, start.desktop = c.sizeof(start), desktop_name
        binary = app / "WindTerm.exe"
        command = c.create_unicode_buffer('"' + str(binary) + '"')
        check(kernel.CreateProcessW(str(binary), command, None, None, False, 4 | 0x400, block,
                                    str(app), c.byref(start), c.byref(info)))
        job.assign(info.pid)
        script = PROBE.read_text(encoding="utf-8")
        for key, value in {"__UI_THREAD_ID__": str(info.tid), "__RUN_ID_JSON__": json.dumps(run_id),
                           "__RUN_DIRECTORY_JSON__": json.dumps(out.as_posix()),
                           "__DESKTOP_JSON__": json.dumps(desktop_name),
                           "__TIMEOUT_MS__": str((seconds - 3) * 1000)}.items():
            script = script.replace(key, value)
        (out / "probe.js").write_text(script, encoding="utf-8")
        write_json(out / "run.json", {"schema": SCHEMA, "run_id": run_id, "pid": info.pid, "tid": info.tid,
                   "sha256": digest(binary), "files_sha256": EXPECTED_FILES,
                   "script_sha256": digest(out / "probe.js"), "runner_sha256": digest(Path(__file__)),
                   "probe_template_sha256": digest(PROBE), "profile": "synthetic-local-only",
                   "desktop": "owned-non-input", "seconds": seconds,
                   "environment": {"os": platform.platform(), "python": platform.python_version()},
                   "consumer_done": None, "presented_done": None})
        if kernel.ResumeThread(info.thread) == 0xffffffff:
            raise c.WinError(c.get_last_error())
        time.sleep(2)
        code = run_cli(cli + ["instrument-with-frida", "attach", str(info.pid), "--script-file", str(out / "probe.js"),
                              "--duration-ms", str(seconds * 1000), "--format", "json", "--full-output"],
                       out, job, seconds + 15)
        if code:
            raise RuntimeError("REA invocation failed")
    except BaseException as exc:
        if not (out / "launcher-failure.json").exists():
            write_json(out / "launcher-failure.json", {"reason": "diagnostic-" + type(exc).__name__})
        raise
    finally:
        try:
            cleanup_owned(kernel, info, job, out, run_id)
        finally:
            if job: job.close()
            if info.thread: kernel.CloseHandle(info.thread)
            if info.process: kernel.CloseHandle(info.process)
            check(user.CloseDesktop(desktop))
    report = summarize(out)
    write_json(out / "diagnostic.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    launch = commands.add_parser("run")
    launch.add_argument("--source", required=True, type=Path)
    launch.add_argument("--out", required=True, type=Path)
    launch.add_argument("--rea", required=True, type=Path)
    launch.add_argument("--seconds", type=int, default=30)
    peer = commands.add_parser("producer")
    peer.add_argument("--out", required=True, type=Path)
    peer.add_argument("--run-id", required=True)
    replay = commands.add_parser("summarize")
    replay.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "producer":
        return producer(args.out, args.run_id)
    result = run(args.source, args.out, args.rea, args.seconds) if args.command == "run" else summarize(args.out)
    print(json.dumps(result, ensure_ascii=True, allow_nan=False))
    return 0 if result["correctness"]["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
