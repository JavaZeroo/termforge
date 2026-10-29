"""Bounded diagnostic runner for the system ConPTY resize reply (issue #145).

The probe host runs inside an owned job; every round resizes the pseudo console
and immediately asks the client for its own console size. The runner only
collects evidence; it asserts nothing about the observed size.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def load_owned_job():
    # Reuse the lifecycle suite's supervisor rather than copying job code.
    path = Path(__file__).resolve().with_name("run.py")
    spec = importlib.util.spec_from_file_location("termforge_conpty_run", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.OwnedJob


def summarize(rounds):
    stale_buffer = [row for row in rounds if not row["buffer_matches_request"]]
    stale_window = [row for row in rounds if not row["window_matches_request"]]
    disagreement = [row for row in rounds if row["buffer_matches_request"] != row["window_matches_request"]]
    send_us = [row["resize_to_send_us"] for row in rounds]
    reply_us = [row["resize_to_reply_us"] for row in rounds]
    timing = lambda values: ({"min": min(values), "median": statistics.median(values), "max": max(values)}
                             if values else {})
    return {
        "rounds": len(rounds),
        "stale_buffer": len(stale_buffer),
        "stale_window": len(stale_window),
        "buffer_window_disagreement": len(disagreement),
        "resize_vt_before_reply": sum(1 for row in rounds if row["resize_vt_before_reply"]),
        "resize_to_send_us": timing(send_us),
        "resize_to_reply_us": timing(reply_us),
        "stale_samples": stale_buffer[:20],
        "disagreement_samples": disagreement[:20],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--mode", choices=("raw", "barrier"), default="raw")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 1000:
        parser.error("rounds must be 1..1000")
    if not args.binary.is_file():
        parser.error("probe binary missing")
    args.out.mkdir(parents=True, exist_ok=False)
    OwnedJob = load_owned_job()
    job = OwnedJob()
    process = None
    result = {
        "schema": "termforge-conpty-resize-reply-v1",
        "status": "failed",
        "mode": args.mode,
        "rounds_requested": args.rounds,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "binary": str(args.binary.resolve()),
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "source_sha256": {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(Path(__file__).resolve().with_name(name)
                               for name in ("resize_reply_probe.cpp", "session.hpp", "backend.hpp",
                                            "resize_reply_run.py"))},
        "performance_claim": False,
    }
    try:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "--host", str(args.rounds), args.mode],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        stdout, stderr = process.communicate(b"GO\n", timeout=300)
        result["returncode"] = process.returncode
        result["stderr"] = stderr.decode("utf-8", "replace")
        result["job_after"] = job.drain()
        records = [json.loads(line) for line in stdout.decode("utf-8", "replace").splitlines() if line.strip()]
        if len(records) < 2 or not records[0].get("ready"):
            raise RuntimeError("probe did not report a ready record")
        result["ready"] = records[0]
        result["complete"] = records[-1]
        result.update(summarize(records[1:-1]))
        if process.returncode != 0:
            raise RuntimeError(f"probe exit {process.returncode}: {result['stderr'][:500]}")
        if result["job_after"]["active"] != 0:
            raise RuntimeError(f"owned job not drained: {result['job_after']}")
        result["status"] = "passed"
    except BaseException as error:
        result["error"] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            job.terminate()
            process.kill()
            process.communicate(timeout=10)
        job.close()
        (args.out / "resize-reply.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()