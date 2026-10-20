"""Visible owned-window diagnostic. Not a terminal or photon-latency benchmark."""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import platform
import queue
import random
import statistics
import subprocess
import sys
import threading
import time
import psutil

from visual import Capture, Windows, clock_bounds, marker_matches, WIDTH, HEIGHT


def nearest_rank(values, quantile):
    if not values or not 0 < quantile <= 1:
        raise ValueError("invalid quantile/sample")
    return sorted(values)[math.ceil(len(values) * quantile) - 1]


class Channel:
    def __init__(self, process):
        self.process = process
        self.queue = queue.Queue()
        self.backlog = []

        def read():
            for line in process.stdout:
                try:
                    self.queue.put(json.loads(line))
                except json.JSONDecodeError:
                    self.queue.put({"type": "invalid_output"})
            self.queue.put({"type": "eof"})

        threading.Thread(target=read, daemon=True).start()

    def send(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def receive(self, kind, timeout=5):
        for i, message in enumerate(self.backlog):
            if message["type"] == kind:
                return self.backlog.pop(i)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message = self.queue.get(timeout=max(.001, deadline - time.monotonic()))
            if message["type"] in ("eof", "invalid_output"):
                raise RuntimeError("fixture closed or invalid response")
            if message["type"] == kind:
                return message
            self.backlog.append(message)
        raise TimeoutError(kind)


def observe(capture, expected, emit, event_id, timeout=5, interval=.008, submit=None):
    deadline = time.perf_counter_ns() + int(timeout * 1e9)
    previous = None
    count = 0
    while time.perf_counter_ns() < deadline:
        data, times = capture.grab()
        match = marker_matches(data, WIDTH, HEIGHT, expected)
        count += 1
        row = {"type": "frame", "event_id": event_id, "matched": match, **times}
        row["actual_frame_gap_ns"] = None if previous is None else times["capture_before_ns"] - previous["capture_before_ns"]
        if match:
            row["frame_sha256"] = hashlib.sha256(data).hexdigest()
        emit(row)
        if match:
            result = {"status": "observed", "frames": count,
                      "first_match": times, "previous_nonmatch": previous}
            if submit is not None:
                result["capture_observed_latency_ns"] = times["capture_after_ns"] - submit
                result["observation_bracket_ns"] = [
                    max(0, (previous["capture_before_ns"] if previous else submit) - submit),
                    times["capture_after_ns"] - submit,
                ]
            return result
        previous = times
        time.sleep(interval)
    return {"status": "timeout", "frames": count, "latency_ns": None,
            "timeout_seconds": timeout}


def calibrate(directory, events=10):
    if not 1 <= events <= 200:
        raise ValueError("events must be in 1..200")
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    ctypes.windll.user32.SetProcessDPIAware()
    rng = random.Random(20261020)
    samples, exchanges = [], []
    report = {
        "kind": "owned_window_visual_diagnostic", "status": "running",
        "python_version": platform.python_version(), "os": platform.platform(),
        "events_per_delay": events, "configured_delays_ms": [0, 80],
        "backend": "visible desktop BitBlt BGRA ROI; SendInput F13",
        "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                          for name in ("visual.py", "visual_fixture.py", "calibrate_visual.py")},
        "samples": samples,
        "limitations": [
            "Calibration fixture only, no WindTerm or PTY. Capture visibility is not photons.",
            "BitBlt has no compositor presentation timestamp; polling bracket is observational.",
            "Marker checks 32 cell centers, not full screenshot/text/style correctness.",
            "Focus guard cannot atomically bind global SendInput to a window; fixture only.",
            "Topmost overlays/HDR/color transforms can invalidate visible-desktop capture.",
            "No universal capture overhead bound or formal T01 calibration established."
        ],
    }
    process = None
    fixture_process = None
    with (directory / "frames.jsonl").open("x", encoding="utf-8") as trace:
        def emit(row):
            trace.write(json.dumps(row) + "\n")

        try:
            with (directory / "fixture-stderr.txt").open("x", encoding="utf-8") as stderr:
                process = subprocess.Popen(
                    [sys.executable, str(Path(__file__).with_name("visual_fixture.py"))],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                    text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
                channel = Channel(process)
                ready = channel.receive("ready")
                # Windows venv Python can be a redirector that starts the real
                # interpreter as a child. Accept only this owned process tree.
                owned = psutil.Process(process.pid)
                descendants = {p.pid: p for p in owned.children(recursive=True)}
                descendants[owned.pid] = owned
                fixture_process = descendants.get(ready["pid"])
                if fixture_process is None or not fixture_process.is_running():
                    raise RuntimeError("fixture PID outside owned process tree")
                fixture_pid = fixture_process.pid
                api = Windows()
                for i in range(20):
                    before = time.perf_counter_ns()
                    channel.send({"op": "ping", "id": i})
                    response = channel.receive("pong")
                    after = time.perf_counter_ns()
                    if response["id"] != i:
                        raise RuntimeError("clock response mismatch")
                    exchanges.append({"before_ns": before, "after_ns": after,
                                      "remote_ns": response["remote_ns"]})
                report["clock_alignment"] = clock_bounds(exchanges)
                with Capture(api, ready["canvas_hwnd"], fixture_pid) as capture:
                    for delay in (0, 80):
                        nonce = rng.getrandbits(32)
                        channel.send({"op": "configure", "id": delay,
                                      "nonce": nonce, "delay_ms": delay})
                        response = channel.receive("configured")
                        baseline = observe(capture, nonce, emit, f"baseline-{delay}")
                        if baseline["status"] != "observed":
                            raise RuntimeError("initial marker not visible")
                        for sequence in range(1, events + 1):
                            # Establish the previous marker before each input.
                            data, _ = capture.grab()
                            if not marker_matches(data, WIDTH, HEIGHT, nonce ^ (sequence - 1)):
                                raise RuntimeError("unexpected pre-input marker")
                            event_id = f"{delay}-{sequence}"
                            row = {"event_id": event_id, "delay_ms": delay, "sequence": sequence}
                            samples.append(row)  # Persist incomplete attempts on any failure.
                            if not fixture_process.is_running():
                                raise RuntimeError("fixture identity expired")
                            submitted = api.inject_f13(ready["canvas_hwnd"], fixture_pid)
                            row["injection"] = submitted
                            row.update(observe(capture, nonce ^ sequence, emit, event_id,
                                               submit=submitted["submit_ns"]))
                            if row["status"] != "observed":
                                raise RuntimeError("marker timeout; stop to prevent sequence drift")
                            response = channel.receive("paint_submitted")
                            if response["sequence"] != sequence:
                                raise RuntimeError("fixture sequence mismatch")
                            row["fixture"] = response
                            time.sleep(rng.uniform(.25, .75))
                    # Capture-only diagnostic of method cost, independent of input.
                    durations = []
                    cpu_start = time.process_time()
                    for _ in range(100):
                        _, times = capture.grab()
                        durations.append(times["capture_after_ns"] - times["capture_before_ns"])
                    report["capture_only"] = {"samples_ns": durations,
                                              "process_cpu_seconds": time.process_time() - cpu_start,
                                              "median_ns": statistics.median(durations)}
                report["status"] = "complete_diagnostic"
        except Exception as exc:
            report["status"] = "aborted"
            report["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            if process is not None:
                if process.poll() is None:
                    try:
                        process.stdin.write('{"op":"quit"}\n')
                        process.stdin.flush()
                        process.wait(timeout=3)
                    except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                        if fixture_process is not None and fixture_process.is_running():
                            fixture_process.terminate()
                        process.terminate()
                        process.wait(timeout=3)
                if process.stdin:
                    process.stdin.close()
                if process.stdout:
                    process.stdout.close()
            for delay in (0, 80):
                values = [s["capture_observed_latency_ns"] for s in samples
                          if s.get("status") == "observed" and s["delay_ms"] == delay]
                report.setdefault("summary", {})[str(delay)] = {
                    "observed": len(values), "median_ns": statistics.median(values) if values else None,
                    "p95_ns": nearest_rank(values, .95) if len(values) >= 200 else None,
                    "p99_ns": nearest_rank(values, .99) if len(values) >= 1000 else None,
                }
            (directory / "summary.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "summary": report["summary"],
                      "error": report.get("error")}))
    return 0 if report["status"] == "complete_diagnostic" else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--events", type=int, default=10)
    args = parser.parse_args()
    sys.exit(calibrate(args.out, args.events))
