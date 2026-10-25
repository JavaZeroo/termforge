"""Bounded recovery fuzz with first-failure preservation, byte minimization and fixed regressions."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import random

from compare_candidate import compare_traces, run_case
from run import ROOT, sha, state_sha, write_json
from experiments.candidates.alacritty.run import VERSION as CANDIDATE_VERSION, vendor_sources


TOKENS = (b"A", b" ", b"\r", b"\n", b"\b", b"\t", b"\x1b[31m", b"\x1b[0m",
          b"\x1b[2J", b"\x1b[1J", b"\x1b[2K", b"\x1b[2;3H", b"\x1b[A", b"\x1b[?9999z",
          b"\x1b]0;t\x07", b"\xff", b"\x18", b"\xe4\xb8\xad", b"\xcc\x81")


class GeneratedCase:
    def __init__(self, name, data):
        self.name, self.data = name, data
        self.rows, self.columns, self.history = 3, 12, 3


def generate(seed):
    rng = random.Random(seed)
    return b"".join(rng.choice(TOKENS) for _ in range(rng.randint(1, 12)))


def without_offsets(records):
    return [dict(offset=0, screen=record["screen"]) for record in records]


def signature(report, core, candidate):
    difference = report["first_difference"]
    return dict(input_sha256=report["input_sha256"], rows=report["rows"], columns=report["columns"],
                history=report["history"],
                first_difference={key: value for key, value in difference.items() if key != "checkpoint"},
                final_core_sha256=state_sha(core[-1]["screen"]),
                final_candidate_sha256=state_sha(candidate[-1]["screen"]))


def known_failures():
    path = Path(__file__).with_name("fuzz-known-failures.json")
    if not path.exists():
        return {}
    records = json.loads(path.read_text(encoding="utf-8"))
    return {record["signature"]["input_sha256"]: record for record in records}


def evaluate(core_binary, candidate_binary, data, name, output):
    report, core, candidate = run_case(core_binary, candidate_binary, GeneratedCase(name, data), "whole", output,
                                       ("--recovery",))
    status = report["status"]
    if status == "difference":
        known = known_failures().get(report["input_sha256"])
        if known and known["signature"] == signature(report, core, candidate):
            status = "known_failure"
    return status, report, core, candidate


def minimize(core_binary, candidate_binary, data, path, output):
    current = data
    changed = True
    while changed:
        changed = False
        for index in range(len(current) - 1, -1, -1):
            candidate_data = current[:index] + current[index + 1:]
            status, report, _, _ = evaluate(core_binary, candidate_binary, candidate_data,
                                            "minimize", output / f"{len(current)}-{index}")
            difference = report["first_difference"]
            if status not in ("match", "reference_difference") and difference and difference["path"] == path:
                current, changed = candidate_data, True
                break
    return current


def run_suite(core_binary, candidate_binary, output, seeds=4):
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for seed in range(seeds):
        data = generate(seed)
        for plan in ("whole", "byte"):
            if plan == "byte":
                (output / f"seed-{seed}-byte").mkdir(parents=True, exist_ok=False)
                (output / f"seed-{seed}-byte/input.bin").write_bytes(data)
                chunks_path = output / f"seed-{seed}-byte/chunks.txt"
                chunks_path.write_text("1\n" * len(data), encoding="ascii")
                from compare_candidate import execute
                core, core_code, core_error = execute(core_binary, "core", GeneratedCase(f"seed_{seed}", data),
                                                      [1] * len(data), output / f"seed-{seed}-byte", ("--recovery",))
                candidate, candidate_code, candidate_error = execute(candidate_binary, "candidate",
                                                                      GeneratedCase(f"seed_{seed}", data),
                                                                      [1] * len(data), output / f"seed-{seed}-byte")
                whole_status, _, whole_core, whole_candidate = evaluate(
                    core_binary, candidate_binary, data, f"seed_{seed}", output / f"seed-{seed}-whole-for-byte")
                core_difference = compare_traces(without_offsets(whole_core[-1:]), without_offsets(core[-1:]))
                candidate_difference = compare_traces(without_offsets(whole_candidate[-1:]),
                                                    without_offsets(candidate[-1:]))
                status = "fragmentation_failure" if core_difference or candidate_difference else whole_status
                report = dict(case=f"seed_{seed}", plan=plan, status=status,
                              input_sha256=hashlib.sha256(data).hexdigest(), checkpoints=len(candidate),
                              first_difference=core_difference or candidate_difference,
                              core_fragmentation_difference=core_difference,
                              candidate_fragmentation_difference=candidate_difference,
                              core_returncode=core_code, candidate_returncode=candidate_code,
                              core_error=core_error, candidate_error=candidate_error)
                write_json(output / f"seed-{seed}-byte/report.json", report)
            else:
                status, report, _, _ = evaluate(core_binary, candidate_binary, data, f"seed_{seed}",
                                                 output / f"seed-{seed}-whole")
            report["status"] = status
            write_json((output / f"seed-{seed}-{plan}") / "report.json", report)
            records.append(report)
            if status not in ("match", "reference_difference", "known_failure"):
                failure = dict(seed=seed, plan=plan, original_hex=data.hex(), report=report)
                path = report["first_difference"]["path"] if report["first_difference"] else None
                if path:
                    minimized = minimize(core_binary, candidate_binary, data, path, output / "minimize")
                    failure["minimized_hex"] = minimized.hex()
                    _, failure["minimized_report"], _, _ = evaluate(core_binary, candidate_binary, minimized,
                                                                     "minimized", output / "minimized")
                write_json(output / "failure.json", failure)
                summary = dict(schema=1, passed=False, execution_passed=False, first_failure=failure,
                               runs=len(records), performance_measured=False)
                write_json(output / "summary.json", summary)
                return summary
    sources = ["experiments/differential/fuzz_matrix.py", "experiments/differential/compare_candidate.py",
               "experiments/differential/fuzz-known-failures.json", "experiments/core/trace.cpp",
               "experiments/candidates/alacritty/run.py"] + vendor_sources()
    passed = all(record["status"] in ("match", "reference_difference", "known_failure") for record in records)
    summary = dict(schema=1, platform=platform.platform(), candidate="alacritty_terminal",
                   version=CANDIDATE_VERSION, core_trace_sha256=sha(core_binary),
                   candidate_trace_sha256=sha(candidate_binary),
                   source_sha256={path: sha(ROOT / path) for path in sources}, seeds=seeds,
                   runs=len(records),
                   counts={status: sum(record["status"] == status for record in records)
                           for status in sorted({record["status"] for record in records})},
                   passed=passed, execution_passed=passed, semantic_acceptance=False,
                   performance_measured=False, records=records)
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--seeds", type=int, default=4)
    args = parser.parse_args()
    if args.seeds < 1 or args.seeds > 10000:
        parser.error("--seeds must be between 1 and 10000")
    result = run_suite(args.trace.resolve(strict=True), args.candidate.resolve(strict=True), args.out.resolve(),
                       args.seeds)
    print(json.dumps({key: result.get(key) for key in ("passed", "execution_passed", "seeds", "runs", "counts",
                                                       "first_failure")}, indent=2))
    return 0 if result["execution_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
