"""Run bounded receive experiments without UI interaction (Windows Release build)."""
import argparse
import hashlib
import itertools
import json
import platform
from pathlib import Path
import random
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(row):
    assert row["valid"] and row["consumed"] == row["bytes"], "payload mismatch"
    assert 0 < row["peak_queued_bytes"] <= row["capacity"], "queue exceeded bound"
    assert 0 < row["min_read"] <= row["max_read"] <= row["chunk"], "read size invalid"
    assert row["reads"] * row["min_read"] <= row["bytes"] <= row["reads"] * row["max_read"]
    assert 0 < row["notifications"] <= row["reads"], "notification count invalid"
    if row["policy"] == "each":
        assert row["notifications"] == row["reads"]
    assert row["elapsed_seconds"] > 0 and row["cpu_ms"] >= 0
    assert row["queue_to_validated_block_p50_us"] <= row["queue_to_validated_block_p95_us"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, help="explicit receive executable")
    parser.add_argument("--bin-dir", type=Path, help="CMake runtime directory; overrides TERMFORGE_BIN_DIR")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.exe is not None and args.bin_dir is not None:
        parser.error("choose --exe or --bin-dir")
    args.exe = args.exe.resolve() if args.exe is not None else binary("receive", args.bin_dir)
    if args.repeats < 1:
        parser.error("repeats must be positive")
    args.out.mkdir(parents=True, exist_ok=False)
    meta = {"schema_version": 1, "platform": platform.platform(), "machine": platform.machine(),
            "python": platform.python_version(), "exe_sha256": sha(args.exe),
            "source_sha256": sha(Path(__file__).with_name("receive.cpp")),
            "runner_sha256": sha(Path(__file__)), "repeats": args.repeats, "seed": 20261022,
            "fixture": "xorshift32 seed 0x12345678; low byte after shifts 13,17,5",
            "status": "running", "timeout_per_run_seconds": 60}
    manifest = args.out / "manifest.json"
    manifest.write_text(json.dumps(meta, indent=2) + "\n")
    rows = []
    try:
        with (args.out / "samples.jsonl").open("w") as stream:
            def execute(chunk, policy, mode, size, capacity, phase, repeat):
                command = [str(args.exe.resolve()), str(chunk), policy, mode, str(size), str(capacity)]
                record = {"phase": phase, "repeat": repeat, "command_args": command[1:]}
                try:
                    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                    record.update(exit_code=result.returncode, stderr=result.stderr)
                    record["result"] = json.loads(result.stdout)
                    if result.returncode:
                        raise RuntimeError("experiment exited unsuccessfully")
                    check(record["result"])
                except Exception as error:
                    record["error"] = str(error)
                    raise
                finally:
                    stream.write(json.dumps(record) + "\n"); stream.flush()
                rows.append(record)

            configs = list(itertools.product((512, 4096, 65536), ("each", "edge"), ("stream", "burst", "slow", "yield")))
            # Tight queue and non-multiple payload exercise backpressure, EOF and short final reads.
            for chunk, policy, mode in configs:
                execute(chunk, policy, mode, 131101, chunk, "correctness", 0)
            sizes = {"stream": 8 * 1024**2, "burst": 1024**2, "slow": 262144, "yield": 1024**2}
            rng = random.Random(meta["seed"])
            for repeat in range(args.repeats + 1):
                rng.shuffle(configs)
                for chunk, policy, mode in configs:
                    execute(chunk, policy, mode, sizes[mode], 262144,
                            "warmup" if repeat == 0 else "measurement", repeat)
                print(f"Completed round {repeat}/{args.repeats}", flush=True)
        summary = []
        for chunk, policy, mode in sorted(configs):
            group = [r["result"] for r in rows if r["phase"] == "measurement" and
                     (r["result"]["chunk"], r["result"]["policy"], r["result"]["mode"]) == (chunk, policy, mode)]
            item = {"chunk": chunk, "policy": policy, "mode": mode, "samples": len(group)}
            for key in ("mib_per_second", "cpu_ms", "notifications", "reads", "peak_queued_bytes",
                        "peak_working_set_bytes", "queue_to_validated_block_p95_us"):
                values = [r[key] for r in group]
                item[key] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
            summary.append(item)
        (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        meta.update(status="complete", checked_runs=len(rows))
    except Exception as error:
        meta.update(status="failed", error=str(error))
        raise
    finally:
        manifest.write_text(json.dumps(meta, indent=2) + "\n")


if __name__ == "__main__":
    main()
