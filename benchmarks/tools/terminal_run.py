"""Run one producer diagnostic from inside an explicitly selected Windows terminal."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

import psutil

from sample import sample
from summarize_resources import summarize


def find_target(executable):
    expected = Path(executable).resolve(strict=True)
    matches = [p for p in psutil.Process().parents()
               if Path(p.exe()).resolve() == expected]
    if len(matches) != 1:
        raise ValueError("Run inside the selected terminal; expected exactly one matching ancestor")
    return matches[0]


def run(directory, output, executable, sampling=False, timeout=120):
    if os.name != "nt" or not sys.stdout.isatty():
        raise ValueError("Windows terminal stdout required; redirected output is not a terminal run")
    if not 1 <= timeout <= 120:
        raise ValueError("timeout must be within 1..120 seconds")
    target = find_target(executable)
    identity = (target.pid, target.create_time())
    app_hash = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
    directory = Path(directory).resolve(strict=True)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "kind": "terminal_producer_diagnostic", "schema_version": "0.1",
        "status": "starting", "application_sha256": app_hash,
        "root_pid": identity[0], "root_create_time": identity[1],
        "sampling_enabled": sampling, "timeout_seconds": timeout,
        "workload_sha256": manifest["sha256"], "payload_bytes": manifest["payload_bytes"],
        "actual_settings": {}, "correctness_status": "not_checked",
        "consumer_done": None, "presented_done": None,
        "performance_gate": "not_evaluated", "resource_summary": None,
        "limitations": [
            "Single manual run, not a formal T01 batch; record actual settings separately.",
            "Producer completion does not establish parsing, presentation, or correctness.",
            "Resource window includes setup and 500 ms after producer exit, not exactly payload time.",
            "Descendants include shell, controller and producer; do not sum sampler with descendants.",
            "No capture or input injection; on/off measurements alone cannot calibrate capture."
        ]}
    report_path = output / "run.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    stop = threading.Event()
    failures = []
    thread = None
    producer = None
    trace = output / "resources.jsonl"

    def observe():
        try:
            sample(target.pid, trace, seconds=timeout + 10, stop=stop.is_set)
        except Exception as exc:
            failures.append(type(exc).__name__)

    try:
        if sampling:
            thread = threading.Thread(target=observe)
            thread.start()
            deadline = time.monotonic() + 5
            while True:
                if failures or not thread.is_alive():
                    raise RuntimeError("sampler exited before producer startup")
                if trace.exists() and '"type": "sample"' in trace.read_text(encoding="utf-8"):
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("sampler startup")
                time.sleep(.02)
        if not target.is_running() or target.create_time() != identity[1]:
            raise RuntimeError("terminal identity changed")
        # The base interpreter avoids the Windows venv redirector. play.py uses
        # only stdlib and creates no descendants, so timeout cleanup owns one PID.
        interpreter = getattr(sys, "_base_executable", sys.executable)
        with (output / "producer-stderr.txt").open("xb") as errors:
            producer = subprocess.Popen(
                [interpreter, str(Path(__file__).with_name("play.py")), str(directory),
                 "--result", str(output / "producer.json"), "--chunk", "65536"],
                stdin=subprocess.DEVNULL, stderr=errors)
            try:
                code = producer.wait(timeout=timeout)
                report["status"] = "producer_complete" if code == 0 else "producer_failed"
                report["producer_exit_code"] = code
            except subprocess.TimeoutExpired:
                report["status"] = "producer_timeout"
                producer.kill()
                producer.wait()
        time.sleep(.5)
    except KeyboardInterrupt:
        report["status"] = "interrupted"
    except Exception as exc:
        report["status"] = "failed"
        report["error_type"] = type(exc).__name__
    finally:
        if producer is not None and producer.poll() is None:
            producer.kill()
            producer.wait()
        stop.set()
        if thread is not None:
            thread.join()
        if sampling:
            if failures:
                report["sampling_errors"] = failures
                report["status"] = "failed"
            elif trace.exists():
                try:
                    resources = summarize(trace)
                    (output / "resource-summary.json").write_text(
                        json.dumps(resources, indent=2) + "\n", encoding="utf-8")
                    report["resource_summary"] = "resource-summary.json"
                except ValueError as exc:
                    report["sampling_error"] = str(exc)
                    report["status"] = "failed"
        producer_path = output / "producer.json"
        if producer_path.exists():
            try:
                events = json.loads(producer_path.read_text(encoding="utf-8"))
                report["producer"] = events
            except json.JSONDecodeError:
                report["producer_record_status"] = "incomplete"
        report_path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--out", required=True)
    parser.add_argument("--app", required=True, help="Exact terminal executable path")
    parser.add_argument("--sample", action="store_true")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    result = run(args.directory, args.out, args.app, args.sample, args.timeout)
    print("\nTermForge diagnostic: " + result["status"])
    sys.exit(0 if result["status"] == "producer_complete" else 1)
