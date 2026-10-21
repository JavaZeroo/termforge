"""One fresh-session step of a resumable, manual WindTerm sampler diagnostic."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys
import time

import psutil

from terminal_run import find_target, run


def schedule():
    rows = [{"phase": "warmup", "sampling": False} for _ in range(2)]
    for pair in range(5):
        rows.extend({"phase": "measurement", "pair": pair, "sampling": enabled}
                    for enabled in ((False, True) if pair % 2 == 0 else (True, False)))
    return rows


def write(path, value):
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)


def session_shell(parents, target_pid):
    shells = []
    for parent in parents:
        if parent.pid == target_pid:
            break
        if parent.name().lower() in ("cmd.exe", "powershell.exe", "pwsh.exe"):
            shells.append(parent)
    if not shells:
        raise ValueError("Open a fresh local Cmd or PowerShell session first")
    return shells[-1]


def compare(records):
    measured = [r for r in records if r["step"]["phase"] == "measurement"]
    result = {"kind": "sampler_producer_time_comparison", "performance_gate": "not_evaluated",
              "status": "incomplete", "measurement_count": len(measured),
              "limitations": ["Diagnostic profile only; C0 settings and stable idle not verified.",
                              "Manual marker check does not verify all output or presentation time.",
                              "Only sampling on/off; capture overhead is not measured."]}
    if len(records) != 12 or [r["step"] for r in records] != schedule():
        return result
    if any(r["run"]["status"] != "producer_complete" or not r["marker_confirmed"] for r in records):
        result["status"] = "failed_samples_retained"
        return result
    sizes = {(r["run"]["actual_settings"].get("columns"),
              r["run"]["actual_settings"].get("rows")) for r in records}
    if len(sizes) != 1 or any(None in size for size in sizes):
        result["status"] = "inconsistent_or_unknown_terminal_size"
        return result
    values = {False: [], True: []}
    for row in measured:
        producer = row["run"]["producer"]
        elapsed = producer["producer_done_counter_ns"] - producer["t0_counter_ns"]
        if elapsed <= 0 or producer["fully_written_chunk_bytes"] != producer["payload_bytes"]:
            result["status"] = "invalid_producer_record"
            return result
        values[row["step"]["sampling"]].append(elapsed)
    off, on = (statistics.median(values[enabled]) for enabled in (False, True))
    delta = (on / off - 1) * 100
    result.update(status="diagnostic_complete", sampler_off_ns=values[False],
                  sampler_on_ns=values[True], median_off_ns=off, median_on_ns=on,
                  median_elapsed_change_percent=delta, above_5_percent_trigger=abs(delta) > 5)
    return result


def next_step(directory, batch, app):
    target = find_target(app)
    # Outermost shell below WindTerm, not the transient cmd.exe used to run a
    # .cmd wrapper from PowerShell. Otherwise the same tab could pass as fresh.
    shell = session_shell(psutil.Process().parents(), target.pid)
    directory, batch = Path(directory).resolve(), Path(batch).resolve()
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    payload_hash = hashlib.sha256((directory / "payload.bin").read_bytes()).hexdigest()
    if payload_hash != manifest["sha256"] or manifest["kind"] != "ascii" or manifest["target_bytes"] != 8388608:
        raise ValueError("Expected intact T01 8 MiB ASCII workload")
    identity = {"application_sha256": hashlib.sha256(Path(app).read_bytes()).hexdigest(),
                "root_pid": target.pid, "root_created": target.create_time(),
                "workload_sha256": payload_hash,
                "tool_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                for name in ("terminal_calibration.py", "terminal_run.py", "play.py", "sample.py")}}
    batch.mkdir(parents=True, exist_ok=True)
    lock = batch / "active.lock"
    with lock.open("x"):
        pass
    try:
        state_path = batch / "batch.json"
        if state_path.exists():
            state = json.loads(state_path.read_text(encoding="utf-8"))
            if state["identity"] != identity:
                raise ValueError("Application/process/workload/tools changed; use a new batch directory")
        else:
            state = {"identity": identity, "profile": "D0-diagnostic-not-C0", "records": [],
                     "pending_step": None, "stable_idle_verified": False}
        if state["pending_step"] is not None:
            raise ValueError("Interrupted step retained; inspect batch before starting a new batch")
        index = len(state["records"])
        if index == len(schedule()):
            print("Batch already complete; see comparison.json")
            return
        if state["records"]:
            previous = state["records"][-1]
            if previous["run"]["status"] != "producer_complete" or not previous["marker_confirmed"]:
                raise ValueError("Previous step failed/unconfirmed; inspect it before creating a new batch")
            try:
                old = psutil.Process(previous["shell_pid"])
                if old.create_time() == previous["shell_created"] and old.is_running():
                    raise ValueError("Close the previous test tab and run this command in a NEW session")
            except psutil.NoSuchProcess:
                pass
            # Minimum settle interval only; no claim that CPU/memory returned to idle.
            print("Previous shell exited. Minimum settle wait: 30s", flush=True)
            time.sleep(30)
        step = schedule()[index]
        state["pending_step"] = index
        write(state_path, state)
        print(f"Step {index + 1}/12: {step['phase']}, sampler={step['sampling']}", flush=True)
        report = run(directory, batch / f"step-{index:02d}", app, sampling=step["sampling"])
        print("\nProducer status: " + report["status"], flush=True)
        confirmed = False
        if report["status"] == "producer_complete":
            print("Expected final marker: " + manifest["marker"])
            print("Check the PAYLOAD marker ABOVE the producer status, not this expected-text prompt.")
            confirmed = input("Payload marker visible? [y/N]: ").strip().lower() == "y"
        state["records"].append({"step": step, "run": report, "marker_confirmed": confirmed,
                                 "shell_pid": shell.pid, "shell_created": shell.create_time(),
                                 "finished_counter_ns": time.perf_counter_ns()})
        state["pending_step"] = None
        write(state_path, state)
        write(batch / "comparison.json", compare(state["records"]))
        if report["status"] != "producer_complete" or not confirmed:
            print("Unconfirmed/failed step retained. Stop and inspect before continuing.")
        elif index == 11:
            print("All 12 steps recorded. See comparison.json; this is not a C0 baseline.")
        else:
            print("Close this test tab. Open a NEW local session and run the same command again.")
    finally:
        lock.unlink()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--batch", required=True)
    parser.add_argument("--app", required=True)
    args = parser.parse_args()
    next_step(args.directory, args.batch, args.app)
