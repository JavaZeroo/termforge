"""Continuous-interval core diagnostics and clock/counter calibration, schema v2."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary

SCHEMA_VERSION = 2
EXPECTED_STATE = "5455070173120736870"
ENDPOINT = ("one continuous interval: parser construction, prior-iteration destruction, "
            "feed/state updates and timed gates; warmup, oracle, serialization and final destruction excluded")


def run_command(command, timeout=90):
    """Keep both streams even when a subprocess fails, times out or emits bad JSON."""
    def decoded(value):
        return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
    try:
        process = subprocess.run(command, capture_output=True, timeout=timeout)
        return dict(exit_code=process.returncode, timed_out=False,
                    stdout=decoded(process.stdout), stderr=decoded(process.stderr))
    except subprocess.TimeoutExpired as error:
        return dict(exit_code=None, timed_out=True, stdout=decoded(error.stdout),
                    stderr=decoded(error.stderr), error=f"subprocess timeout after {timeout} seconds")
    except OSError as error:
        return dict(exit_code=None, timed_out=False, stdout="", stderr="", error=str(error))


def validate_interval(result, floor):
    for name in ("interval_wall_seconds", "interval_cpu_seconds"):
        value = result[name]
        if not math.isfinite(value) or value < floor:
            raise ValueError(f"{name} did not meet the requested floor")
    if result["iterations"] < 1 or result["cpu_gate_polls"] < 1:
        raise ValueError("empty interval or missing completion gate")


def validate_result(result, floor, expected):
    if result.get("schema_version") != SCHEMA_VERSION or result.get("valid") is not True:
        raise ValueError("unsupported schema or failed oracle")
    if result.get("cpu_floor_seconds") != floor:
        raise ValueError("mismatched requested CPU floor")
    if result.get("counter_off_is_uninstrumented") is not False:
        raise ValueError("counter-off provenance missing")
    if result["kind"] == "calibration":
        if expected["kind"] != "calibration":
            raise ValueError("unexpected calibration response")
        validate_interval(result["clock_read_loop"], floor)
        allocation_samples = result["allocation_loop"]
        if len(allocation_samples) != 2 or [r["observe"] for r in allocation_samples] != [expected["observe_first"], 1 - expected["observe_first"]]:
            raise ValueError("calibration observer order mismatch")
        for item in allocation_samples:
            validate_interval(item, floor)
            if item["observe"]:
                if item["ordinary_allocations"] != item["iterations"] or item["ordinary_allocated_bytes"] != 64 * item["iterations"]:
                    raise ValueError("allocation calibration oracle failed")
            elif item["ordinary_allocations"] is not None or item["ordinary_allocated_bytes"] is not None:
                raise ValueError("counter-off counts should be unknown")
        return
    if result["kind"] != "core_cost" or expected["kind"] != "core_cost":
        raise ValueError("unexpected result kind")
    validate_interval(result, floor)
    for name in ("route", "chunk", "observe"):
        if result[name] != expected[name]:
            raise ValueError(f"mismatched {name}")
    if result["rounds"] != result["iterations"] or result["bytes_per_round"] != 78000:
        raise ValueError("unexpected workload volume")
    if result["state_fnv1a64"] != EXPECTED_STATE:
        raise ValueError("unexpected state digest")
    if result["cpu_interval_differences"] != 1 or result["counted_parser_destructions"] != result["rounds"] - 1:
        raise ValueError("unexpected continuous interval endpoint")
    if result["receive_seconds"] is not None or result["presented_done"] is not None:
        raise ValueError("unmeasured endpoints must remain unknown")
    if result["observe"]:
        if result["ordinary_allocations"] <= 0 or result["ordinary_allocated_bytes"] <= 0:
            raise ValueError("allocation observer did not record any work")
    elif result["ordinary_allocations"] is not None or result["ordinary_allocated_bytes"] is not None:
        raise ValueError("counter-off counts should be unknown")


def execute(command, floor, expected, timeout=90):
    record = dict(expected)
    record.update(run_command(command, timeout))
    try:
        if record["timed_out"] or record["exit_code"] != 0:
            raise RuntimeError(record.get("error", "subprocess failed"))
        result = json.loads(record["stdout"])
        record["result"] = result
        validate_result(result, floor, expected)
        if result["kind"] == "core_cost":
            volume = result["rounds"] * result["bytes_per_round"]
            record["cpu_ns_per_byte"] = result["interval_cpu_seconds"] * 1e9 / volume
            record["wall_ns_per_byte"] = result["interval_wall_seconds"] * 1e9 / volume
    except Exception as error:
        record["error"] = str(error)
    return record


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--cpu-seconds", type=float, default=1.0)
    cli.add_argument("--repeats", type=int, default=3)
    cli.add_argument("--seed", type=int, default=20261023)
    cli.add_argument("--calibration-only", action="store_true")
    args = cli.parse_args()
    if not .1 <= args.cpu_seconds <= 10 or args.repeats < 1:
        cli.error("invalid duration or repeat count")
    exe = binary("core_measure", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(schema_version=SCHEMA_VERSION, status="running", platform=platform.platform(),
                    started_utc=datetime.now(timezone.utc).isoformat(),
                    machine=platform.machine(), python=platform.python_version(),
                    exe_sha256=sha(exe), source_sha256=sha(Path(__file__).with_name("measure.cpp")),
                    runner_sha256=sha(Path(__file__)),
                    core_sources={name: sha(ROOT / "experiments/core" / name) for name in ("terminal.hpp", "screen.hpp", "unicode_width.hpp")},
                    fixture_sha256=hashlib.sha256((b"x" * 76 + b"\r\n") * 1000).hexdigest(),
                    cpu_floor_seconds=args.cpu_seconds, repeats=args.repeats, seed=args.seed,
                    calibration_only=args.calibration_only, endpoint=ENDPOINT,
                    comparable_to_schema_v1=False, performance_gate="not_evaluated",
                    samples=0, calibrations=0)
    meta = args.out / "manifest.json"
    meta.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    configs = [("parse", chunk, observer) for chunk, observer in itertools.product((512, 4096, 65536), (0, 1))]
    configs += [("state", 65536, observer) for observer in (0, 1)]
    rng = random.Random(manifest["seed"])
    try:
        with (args.out / "samples.jsonl").open("w", encoding="utf-8") as output, (args.out / "calibration.jsonl").open("w", encoding="utf-8") as calibration:
            def record_calibration(index):
                first = index % 2
                record = execute([str(exe), "calibrate", str(args.cpu_seconds), str(first)],
                                 args.cpu_seconds, dict(kind="calibration", observe_first=first, index=index), timeout=200)
                calibration.write(json.dumps(record) + "\n"); calibration.flush()
                if "error" in record:
                    raise RuntimeError(f"calibration failed: {record['error']}")
                manifest["calibrations"] += 1
            record_calibration(0)
            if args.calibration_only:
                for index in range(1, args.repeats):
                    record_calibration(index)
            else:
                for repeat in range(args.repeats):
                    rng.shuffle(configs)
                    for route, chunk, observer in configs:
                        expected = dict(kind="core_cost", repeat=repeat, route=route, chunk=chunk, observe=observer)
                        record = execute([str(exe), route, str(chunk), str(observer), str(args.cpu_seconds)], args.cpu_seconds, expected)
                        output.write(json.dumps(record) + "\n"); output.flush()
                        if "error" in record:
                            raise RuntimeError(f"cost sample failed: {record['error']}")
                        manifest["samples"] += 1
                    print(f"Completed cost round {repeat + 1}/{args.repeats}", flush=True)
                record_calibration(1)
        manifest["status"] = "complete"
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        raise
    finally:
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        meta.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
