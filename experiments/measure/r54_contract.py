"""Prepare the pre-measurement R54 plan; preparation never passes a speed gate."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random

from experiments.measure.compare import WORKLOADS
from experiments.measure.run import validate_result

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "termforge-r54-cost-plan-v1"
SOURCES = {
    "baseline": dict(commit="65988f5f20f62294ff52fcac5b5b81e563f93f3b", version="0.26.1-dev+termforge.3",
                     states=dict(project="11966868740884721388", interactive="8035857386658397897")),
    "candidate": dict(commit="3d6d44d3b3e9a9a3e41da98e8711e9f19a211fd8", version="0.26.1-dev+termforge.4",
                      states={route: value["states"]["alacritty"] for route, value in WORKLOADS.items()}),
}
CHUNKS = dict(project=(512, 4096, 65536), interactive=(1, 16, 64, 4096, 22000))
SEEDS = (20261026, 20261027)
REPEATS = 7
CPU_SECONDS = 2.0


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sample_order(seed):
    """Adjacent source pairs limit drift; alternate AB/BA with frozen randomness."""
    rng = random.Random(seed)
    result = []
    configs = [(route, chunk) for route, chunks in CHUNKS.items() for chunk in chunks]
    first = {(route, chunk): rng.randrange(2) for route, chunk in configs}
    for repeat in range(REPEATS):
        order = list(configs)
        rng.shuffle(order)
        for route, chunk in order:
            variants = ("baseline", "candidate")
            if (first[(route, chunk)] + repeat) % 2:
                variants = tuple(reversed(variants))
            for variant in variants:
                result.append(dict(sequence=len(result), repeat=repeat, route=route,
                                   chunk=chunk, variant=variant, observe=0))
    return result


def protocol():
    return dict(
        schema=SCHEMA, sources=SOURCES,
        workloads={route: dict(bytes=len(value["fixture"]),
                              sha256=hashlib.sha256(value["fixture"]).hexdigest(),
                              rows=24, columns=80, history=100, chunks=list(CHUNKS[route]))
                   for route, value in WORKLOADS.items()},
        build=dict(profile="release", cargo_locked=True, default_features=False,
                   allocator_wrapper_compiled=False, same_measurement_harness_required=True,
                   separate_target_directories_required=True),
        timing=dict(cpu_floor_seconds=CPU_SECONDS, wall_floor_seconds=CPU_SECONDS,
                    independent_batch_seeds=list(SEEDS), repeats_per_group=REPEATS,
                    wall_gate_every_iterations=8, cpu_poll_min_wall_seconds=0.25,
                    interval_wall_budget_seconds=60, child_timeout_seconds=90,
                    endpoint="construction + previous destruction + feed + timing gates; warmup, final oracle, JSON and final destruction excluded",
                    cpu_windows="GetProcessTimes kernel plus user; one end-minus-start difference",
                    cpu_unix="getrusage RUSAGE_SELF user plus system; one end-minus-start difference"),
        schedules=[dict(seed=seed, samples=sample_order(seed)) for seed in SEEDS],
        correctness=dict(warmup=True, final_iteration=True, intermediate_iterations=False,
                         full_projection_digest_per_source=SOURCES,
                         separate_unicode_all_chunks_glyph_selection_regression_required=True),
        statistics=dict(primary="median of paired candidate/baseline CPU ns per byte, each route/chunk/batch separately",
                        report=["all samples", "paired ratios", "median", "min", "max", "median absolute deviation", "wall ns per byte"],
                        combine_chunks=False, combine_batches=False,
                        failure_policy="preserve and stop the batch; no retries, outlier removal or replacement samples",
                        unstable_policy="retain uncertainty and diagnose; a new batch needs a new identity"),
        budget=dict(status="not_set", numerical_regression_limit=None,
                    decision="review controlled costs and noise, document user/correctness rationale and freeze numeric limits before evaluating any optimization; never backfill a threshold to pass existing results",
                    performance_gate="not_evaluated"),
        endpoints=dict(receive_seconds=None, input_latency_seconds=None, presented_done=None),
        environment=dict(idle_status="unverified", dedicated_host=False,
                         required_record=["OS/build", "CPU", "power policy", "toolchain", "compiler flags", "source trees", "lockfiles", "binary SHA256", "background CPU evidence", "known concurrent work"],
                         simultaneous_build_or_test_forbidden=True),
        allocation=dict(counter_off_is_uninstrumented=False,
                        absent_wrapper_is_not_absent_timing_instrumentation=True,
                        metrics_unknown=True,
                        overhead_diagnostic="same fixed core, wrapped/unwrapped factorial with identical projection switch; report raw costs, do not subtract from primary samples"),
        original_diagnostic=dict(preserved=True, samples=12,
                                 path="experiments/shell/results/2026-10-26-streaming-graphemes/cost",
                                 comparable_to_this_protocol=False),
    )


def expected_sample(item):
    return dict(kind="core_cost", route=item["route"], chunk=item["chunk"], observe=0,
                bytes_per_round=len(WORKLOADS[item["route"]]["fixture"]),
                state_fnv1a64=SOURCES[item["variant"]]["states"][item["route"]],
                allocator_wrapper_compiled=False)


def validate_sample(result, item):
    validate_result(result, CPU_SECONDS, expected_sample(item))
    if result["wall_gate_every_iterations"] != 8 or result["cpu_gate_min_wall_spacing_seconds"] != 0.25:
        raise ValueError("timing gate contract mismatch")
    if result["interval_wall_seconds"] >= 60:
        raise ValueError("internal wall budget exceeded")
    if result["cpu_clock"] not in ("GetProcessTimes kernel plus user", "getrusage RUSAGE_SELF user plus system"):
        raise ValueError("CPU clock contract mismatch")
    if result["wall_clock"] != "std::time::Instant":
        raise ValueError("wall clock contract mismatch")


def prepare(output):
    value = protocol()
    result = dict(protocol=value, protocol_sha256=hashlib.sha256(canonical(value)).hexdigest(),
                  prepared_utc=datetime.now(timezone.utc).isoformat(),
                  phase="prepared_not_built_or_measured", performance_gate="not_evaluated",
                  builds={name: None for name in SOURCES}, environment_evidence=None,
                  harness_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                  for name in ("experiments/candidates/alacritty/Cargo.toml",
                                               "experiments/candidates/alacritty/src/measure.rs",
                                               "experiments/measure/run.py",
                                               "experiments/measure/r54_contract.py")})
    # Exclusive creation preserves already frozen plans; fresh timing results and
    # build manifests must be separate artifacts rather than rewriting this file.
    with Path(output).open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    value = prepare(args.out)
    print(json.dumps({"phase": value["phase"], "protocol_sha256": value["protocol_sha256"],
                      "performance_gate": value["performance_gate"]}))


if __name__ == "__main__":
    main()
