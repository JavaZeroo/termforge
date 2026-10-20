"""Windows process-tree resource trace. Does not launch or kill target apps."""
import argparse
import json
import math
import os
from pathlib import Path
import time

import psutil


def sample(pid, output, seconds=10.0, interval=0.1, stop=None, discovery_interval=1.0):
    if os.name != "nt":
        raise RuntimeError("Windows-only: private bytes mapping is not portable")
    if not all(math.isfinite(x) and x > 0 for x in (seconds, interval, discovery_interval)):
        raise ValueError("duration/interval must be finite and positive")
    root = psutil.Process(pid)
    root_identity = (root.pid, root.create_time())
    known = {root_identity: root}
    sampler = psutil.Process()
    baseline = {}
    start = time.perf_counter_ns()
    deadline = start + int(seconds * 1e9)
    step = int(interval * 1e9)
    if step < 1:
        raise ValueError("interval below clock representation")
    scheduled = start
    previous = None
    next_discovery = start
    with Path(output).open("x", encoding="utf-8") as stream:
        def emit(row):
            stream.write(json.dumps(row) + "\n")
            stream.flush()

        emit({"type": "metadata", "schema_version": "0.1", "root_pid": pid,
              "root_create_time": root_identity[1], "psutil_version": psutil.__version__,
              "clock": "perf_counter_ns", "start_counter_ns": start,
              "clock_resolution_seconds": time.get_clock_info("perf_counter").resolution,
              "requested_interval_ns": step,
              "requested_discovery_interval_ns": int(discovery_interval * 1e9),
              "limitations": [
                  "Polling can miss short-lived children and final CPU before exit.",
                  "New process CPU deltas start at first observation, not process creation.",
                  "Tree resources include shells/helpers/producers; root and descendant groups are separate.",
                  "Working sets may double-count shared pages. Peak is sampled, not exact.",
                  "No screen capture, input injection, or terminal throughput measured."
              ]})
        while time.perf_counter_ns() < deadline:
            if stop is not None and stop():
                break
            begun = time.perf_counter_ns()
            errors = []
            # Keep tracking discovered descendants after their original parent exits.
            if begun >= next_discovery:
                for identity, process in list(known.items()):
                    try:
                        if process.is_running():
                            for child in process.children(recursive=True):
                                known.setdefault((child.pid, child.create_time()), child)
                    except (psutil.NoSuchProcess, psutil.AccessDenied) as exc:
                        errors.append({"pid": identity[0], "operation": "children", "error": type(exc).__name__})
                next_discovery = begun + int(discovery_interval * 1e9)
            rows = []
            targets = [(key, proc, "root" if key == root_identity else "descendant")
                       for key, proc in known.items()]
            targets.append(((sampler.pid, sampler.create_time()), sampler, "sampler"))
            for identity, process, group in targets:
                try:
                    if not process.is_running():
                        raise psutil.NoSuchProcess(process.pid)
                    with process.oneshot():
                        cpu = process.cpu_times()
                        memory = process.memory_info()
                        handles = process.num_handles()
                    cumulative = cpu.user + cpu.system
                    key = (identity, group)
                    base = baseline.setdefault(key, cumulative)
                    rows.append({"pid": identity[0], "create_time": identity[1], "group": group,
                                 "cpu_seconds_since_first_observation": cumulative - base,
                                 "cpu_seconds_lifetime": cumulative,
                                 "working_set_bytes": memory.rss, "private_bytes": memory.private,
                                 "handles": handles, "status": "observed"})
                except (psutil.NoSuchProcess, psutil.AccessDenied) as exc:
                    rows.append({"pid": identity[0], "create_time": identity[1], "group": group,
                                 "status": type(exc).__name__})
                    if isinstance(exc, psutil.NoSuchProcess):
                        known.pop(identity, None)
            emit({"type": "sample", "elapsed_ns": begun - start,
                  "actual_interval_ns": None if previous is None else begun - previous,
                  "schedule_lateness_ns": max(0, begun - scheduled),
                  "collection_ns": time.perf_counter_ns() - begun,
                  "processes": rows, "errors": errors})
            previous = begun
            scheduled += step
            now = time.perf_counter_ns()
            if now > scheduled:
                missed = (now - scheduled) // step + 1
                emit({"type": "schedule_gap", "skipped_ticks": missed})
                scheduled += missed * step
            time.sleep(max(0, min(scheduled, deadline) - time.perf_counter_ns()) / 1e9)
        emit({"type": "end", "elapsed_ns": time.perf_counter_ns() - start, "status": "complete"})


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pid", type=int, required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--seconds", type=float, default=10)
    p.add_argument("--interval-ms", type=float, default=100)
    p.add_argument("--discovery-ms", type=float, default=1000)
    a = p.parse_args()
    sample(a.pid, a.out, a.seconds, a.interval_ms / 1000,
           discovery_interval=a.discovery_ms / 1000)
