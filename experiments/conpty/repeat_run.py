"""Bounded repeat of the recorded lifecycle sample (issue #145).

Every iteration runs one gated probe host inside its own owned job and keeps the
independent result. A failing iteration retains the native failure record
instead of being retried to green.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import platform
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path


def load_run():
    path = Path(__file__).resolve().with_name("run.py")
    spec = importlib.util.spec_from_file_location("termforge_conpty_run", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def summarize(records, iterations, repeats, mode):
    failures = [row for row in records if row["status"] != "passed"]
    elapsed = [row["elapsed_s"] for row in records]
    return {
        "iterations": iterations,
        "sessions_per_iteration": repeats + 1,
        "sessions_total": iterations * (repeats + 1),
        "mode": mode,
        "failures": len(failures),
        "elapsed_s": {"min": min(elapsed), "median": statistics.median(elapsed), "max": max(elapsed)} if elapsed else {},
        "failure_samples": failures[:5],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=40)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--mode", choices=("normal", "large", "cancel", "close"), default="normal")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 200:
        parser.error("iterations must be 1..200")
    if not 1 <= args.repeats <= 20:
        parser.error("repeats must be 1..20")
    if not args.binary.is_file():
        parser.error("probe binary missing")
    args.out.mkdir(parents=True, exist_ok=False)
    run = load_run()
    summary = {
        "schema": "termforge-conpty-probe-repeat-v1",
        "issue": 145,
        "status": "running",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "binary": str(args.binary.resolve()),
        "performance_claim": False,
    }
    records = []
    for index in range(1, args.iterations + 1):
        begin = time.monotonic()
        try:
            result = run.execute(args.binary, args.mode, args.repeats, timeout=120)
            record = {"iteration": index, "status": result["status"],
                      "elapsed_s": round(time.monotonic() - begin, 3)}
        except BaseException as error:
            detail = getattr(error, "conpty_result", {})
            record = {"iteration": index, "status": "failed",
                      "elapsed_s": round(time.monotonic() - begin, 3),
                      "error": str(error),
                      # Keep the native diagnostics that the lifecycle probe
                      # already collects for a red sample.
                      "failure": {key: detail.get(key) for key in
                                  ("returncode", "timed_out", "job_after", "startup",
                                   "stdout", "stderr", "error") if key in detail}}
        records.append(record)
        summary.update(summarize(records, args.iterations, args.repeats, args.mode))
        summary["completed_iterations"] = index
        (args.out / "repeat.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                              encoding="utf-8")
        print(f"iteration {index}: {record['status']} ({record['elapsed_s']}s)", flush=True)
    summary["status"] = "passed" if not summary["failures"] else "failed"
    summary["finished_utc"] = datetime.now(timezone.utc).isoformat()
    (args.out / "repeat.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                          encoding="utf-8")
    # A red sample is the evidence we are looking for, not a job failure.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
