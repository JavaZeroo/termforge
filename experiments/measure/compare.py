"""Interleaved TermForge/Alacritty core-cost samples with raw evidence, not a speed gate."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import random
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary
from experiments.candidates.alacritty.run import vendor_sources
from experiments.measure.run import EXPECTED_STATE, execute

CANDIDATE_STATE = "11966868740884721388"
ENDPOINT = ("same 78,000-byte project workload and continuous interval; observation off excludes projection, "
            "observation on includes projection/JSON and allocation counting; counters are not an uninstrumented baseline")
WORKLOADS = {
    "project": dict(chunks=(512, 4096, 65536), fixture=(b"x" * 76 + b"\r\n") * 1000,
                    bytes_per_round=78000, endpoint=ENDPOINT,
                    states=dict(termforge=EXPECTED_STATE, alacritty=CANDIDATE_STATE)),
    "interactive": dict(
        chunks=(1, 16, 64, 22000),
        fixture=b"abcdefgh\x1b[D\x1b[D\x1b[DZ\x08Q\r\n" * 1000,
        bytes_per_round=22000,
        endpoint=("same 22,000-byte interactive workload and continuous interval; observation off excludes projection, "
                  "observation on includes projection/JSON and allocation counting; counters are not an uninstrumented baseline"),
        states=dict(termforge="8292075482910706113", alacritty="8035857386658397897")),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def candidate_binary(value=None):
    value = value or os.environ.get("TERMFORGE_ALACRITTY_MEASURE")
    if value:
        return Path(value).resolve(strict=True)
    name = "alacritty-measure" + (".exe" if os.name == "nt" else "")
    return (ROOT / "experiments/candidates/alacritty/target/release" / name).resolve(strict=True)


def run_batch(core, candidate, output, floor, repeats, seed, route="project"):
    workload = WORKLOADS[route]
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema_version=2, status="running", platform=platform.platform(),
                    started_utc=datetime.now(timezone.utc).isoformat(), machine=platform.machine(),
                    cpu_floor_seconds=floor, repeats=repeats, seed=seed, route=route,
                    bytes_per_round=workload["bytes_per_round"], endpoint=workload["endpoint"],
                    core_sha256=sha(core), candidate_sha256=sha(candidate),
                    source_sha256={path: sha(ROOT / path) for path in (
                        "experiments/measure/measure.cpp", "experiments/measure/compare.py",
                        "experiments/measure/run.py", "experiments/candidates/alacritty/run.py",
                        "experiments/candidates/alacritty/src/measure.rs",
                        "experiments/candidates/alacritty/src/main.rs") + tuple(vendor_sources())},
                    fixture_sha256=hashlib.sha256(workload["fixture"]).hexdigest(),
                    comparable_to_schema_v1=False, performance_gate="not_evaluated", samples=0, calibrations=0)
    meta = output / "manifest.json"
    meta.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    implementations = (("termforge", core), ("alacritty", candidate))
    configs = list(itertools.product(workload["chunks"], (0, 1)))
    rng = random.Random(seed)
    samples = []
    try:
        with (output / "samples.jsonl").open("w", encoding="utf-8") as raw, \
                (output / "calibration.jsonl").open("w", encoding="utf-8") as calibration:
            record = execute([str(core), "calibrate", str(floor), "0"], floor,
                             dict(kind="calibration", observe_first=0), timeout=200)
            calibration.write(json.dumps(record) + "\n")
            if "error" in record:
                raise RuntimeError(record["error"])
            manifest["calibrations"] += 1
            for repeat in range(repeats):
                order = list(itertools.product(implementations, configs))
                rng.shuffle(order)
                for (implementation, executable), (chunk, observe) in order:
                    expected = dict(kind="core_cost", implementation=implementation, repeat=repeat,
                                    route=route, chunk=chunk, observe=observe,
                                    bytes_per_round=workload["bytes_per_round"],
                                    state_fnv1a64=workload["states"][implementation])
                    record = execute([str(executable), route, str(chunk), str(observe), str(floor)],
                                     floor, expected)
                    raw.write(json.dumps(record, ensure_ascii=False) + "\n")
                    raw.flush()
                    if "error" in record:
                        raise RuntimeError(record["error"])
                    samples.append(record)
                    manifest["samples"] += 1
        manifest["status"] = "complete"
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        raise
    finally:
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        meta.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    groups = []
    for implementation, chunk, observe in itertools.product(("termforge", "alacritty"), workload["chunks"], (0, 1)):
        selected = [record for record in samples if record["implementation"] == implementation and
                    record["chunk"] == chunk and record["observe"] == observe]
        groups.append(dict(implementation=implementation, chunk=chunk, observe=observe, samples=len(selected),
                           cpu_ns_per_byte_median=statistics.median(record["cpu_ns_per_byte"] for record in selected),
                           wall_ns_per_byte_median=statistics.median(record["wall_ns_per_byte"] for record in selected),
                           rounds=[record["result"]["rounds"] for record in selected],
                           ordinary_allocations=[record["result"]["ordinary_allocations"] for record in selected],
                           ordinary_allocated_bytes=[record["result"]["ordinary_allocated_bytes"]
                                                     for record in selected]))
    summary = dict(schema_version=2, route=route, bytes_per_round=workload["bytes_per_round"],
                   state_fnv1a64=workload["states"], fixture_sha256=manifest["fixture_sha256"], repeats=repeats,
                   cpu_floor_seconds=floor, groups=groups, performance_gate="not_evaluated",
                   allocation_scopes_differ=True, counter_off_is_uninstrumented=False,
                   observation_cost_not_subtracted=True)
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--route", choices=sorted(WORKLOADS), default="project")
    parser.add_argument("--cpu-seconds", type=float, default=1.0)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--seed", type=int, default=20261025)
    args = parser.parse_args()
    if not .1 <= args.cpu_seconds <= 10 or not 1 <= args.repeats <= 20:
        parser.error("invalid duration or repeats")
    core = args.core.resolve(strict=True) if args.core else binary("core_measure")
    candidate = candidate_binary(args.candidate)
    _, summary = run_batch(core, candidate, args.out.resolve(), args.cpu_seconds, args.repeats, args.seed,
                           route=args.route)
    print(json.dumps({"samples": sum(group["samples"] for group in summary["groups"]),
                      "groups": len(summary["groups"]), "performance_gate": summary["performance_gate"]}, indent=2))


if __name__ == "__main__":
    main()
