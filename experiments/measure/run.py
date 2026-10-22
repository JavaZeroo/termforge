"""Repeated single-thread core cost probe; keeps CPU floor, state oracle and observer modes visible."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--cpu-seconds", type=float, default=1.0)
    cli.add_argument("--repeats", type=int, default=3)
    args = cli.parse_args()
    if not .1 <= args.cpu_seconds <= 10 or args.repeats < 1:
        cli.error("invalid duration or repeat count")
    exe = binary("core_measure", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(status="running", platform=platform.platform(), python=platform.python_version(),
                    exe_sha256=sha(exe), source_sha256=sha(Path(__file__).with_name("measure.cpp")),
                    runner_sha256=sha(Path(__file__)),
                    core_sources={name: sha(ROOT / "experiments/core" / name) for name in ("terminal.hpp", "screen.hpp", "unicode_width.hpp")},
                    fixture_sha256=hashlib.sha256((b"x" * 76 + b"\r\n") * 1000).hexdigest(),
                    cpu_floor_seconds=args.cpu_seconds, repeats=args.repeats, seed=20261022,
                    endpoint="parser construction plus feed/state updates; oracle and destruction excluded",
                    performance_gate="not_evaluated", samples=0)
    meta = args.out / "manifest.json"
    meta.write_text(json.dumps(manifest, indent=2) + "\n")
    configs = [("parse", chunk, observer) for chunk, observer in itertools.product((512, 4096, 65536), (0, 1))]
    configs += [("state", 65536, observer) for observer in (0, 1)]
    rng = random.Random(manifest["seed"])
    try:
        with (args.out / "samples.jsonl").open("w") as output:
            for repeat in range(args.repeats):
                rng.shuffle(configs)
                for route, chunk, observer in configs:
                    record = dict(repeat=repeat, route=route, chunk=chunk, observer=observer)
                    try:
                        p = subprocess.run([str(exe), route, str(chunk), str(observer), str(args.cpu_seconds)],
                                           capture_output=True, text=True, timeout=90)
                        record.update(exit_code=p.returncode, stderr=p.stderr)
                        result = json.loads(p.stdout)
                        record["result"] = result
                        if p.returncode or not result["valid"] or result["work_cpu_seconds"] < args.cpu_seconds:
                            raise RuntimeError("failed execution, oracle or CPU floor")
                        if result["state_fnv1a64"] != "5455070173120736870":
                            raise RuntimeError("unexpected state digest")
                        record["cpu_ns_per_byte"] = result["work_cpu_seconds"] * 1e9 / result["rounds"] / result["bytes_per_round"]
                        record["wall_ns_per_byte"] = result["work_seconds"] * 1e9 / result["rounds"] / result["bytes_per_round"]
                        manifest["samples"] += 1
                    except Exception as error:
                        record["error"] = str(error)
                        raise
                    finally:
                        output.write(json.dumps(record) + "\n"); output.flush()
                print(f"Completed cost round {repeat + 1}/{args.repeats}", flush=True)
        manifest["status"] = "complete"
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        raise
    finally:
        meta.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
