"""Capture bounded native/real IME evidence without converting unavailable to pass."""
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("station", "desktop", "hosted"), default="station")
    parser.add_argument("--timeout", type=float, default=55)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 120:
        parser.error("timeout must be between 1 and 120 seconds")
    executable = args.exe.resolve(strict=True)
    args.out.mkdir(parents=True, exist_ok=True)
    command = [str(executable)] + {"station": [], "desktop": ["--desktop-only"], "hosted": ["--hosted-input"]}[args.mode]
    record = {
        "schema": "termforge-ime-evidence-v1",
        "mode": args.mode,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "timeout_seconds": args.timeout,
        "elapsed_seconds": None,
        "returncode": None,
        "timed_out": False,
        "probe": None,
        "parse_error": None,
        "real_ime_workflow_verified": False,
        "status": "failed",
    }
    started = time.perf_counter()
    stdout = stderr = ""
    try:
        completed = subprocess.run(command, capture_output=True, timeout=args.timeout)
        record["returncode"] = completed.returncode
        stdout = completed.stdout.decode("utf-8", errors="replace")
        stderr = completed.stderr.decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired as error:
        record["timed_out"] = True
        stdout = (error.stdout or b"").decode("utf-8", errors="replace")
        stderr = (error.stderr or b"").decode("utf-8", errors="replace")
    except OSError as error:
        stderr = str(error)
    record["elapsed_seconds"] = time.perf_counter() - started
    (args.out / "stdout.jsonl").write_text(stdout, encoding="utf-8")
    (args.out / "stderr.txt").write_text(stderr, encoding="utf-8")
    try:
        probe = json.loads(stdout)
        if not isinstance(probe, dict) or probe.get("schema") != "termforge-ime-probe-v1":
            raise ValueError("missing expected probe schema")
        record["probe"] = probe
        real = args.mode == "hosted" and record["returncode"] == 0 and all(
            probe.get(name) is True for name in (
                "profile_enabled", "composition_observed", "actual_candidate_position_verified",
                "commit_verified", "cancel_verified", "real_ime_workflow_verified",
                "context_cancelled", "owned_window_destroyed",
            )
        )
        record["real_ime_workflow_verified"] = real
        if real:
            record["status"] = "real-ime-probe-passed"
        elif args.mode != "hosted" and record["returncode"] == 0:
            record["status"] = "native-context-only-real-ime-unverified"
        elif record["returncode"] in (2, 3):
            record["status"] = "unavailable-or-incomplete"
    except (ValueError, TypeError) as error:
        record["parse_error"] = str(error)
    (args.out / "result.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": record["status"], "result": str(args.out / "result.json")}, ensure_ascii=False))
    if record["timed_out"]:
        return 124
    if record["parse_error"]:
        return record["returncode"] or 1
    if args.mode == "hosted" and not record["real_ime_workflow_verified"]:
        return record["returncode"] or 3
    return record["returncode"] or 0


if __name__ == "__main__":
    raise SystemExit(main())
