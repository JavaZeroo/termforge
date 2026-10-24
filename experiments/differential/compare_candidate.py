"""Execute common traces against TermForge and the Alacritty candidate; differences stay undecided."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import subprocess

from cases import cases
from run import ROOT, canonicalize_core, chunks, compare_traces, sha, state_sha, write_json
from experiments.candidates.alacritty.run import trace_binary as candidate_trace_binary


def common_projection(records):
    canonicalize_core(records)
    result = []
    for record in records:
        screen = record["screen"]
        for group in ("cells", "history"):
            for row in screen[group]:
                for cell in row:
                    if cell["text"] in ("", " ") and cell["width"] in (0, 1):
                        cell["text"], cell["width"] = "", 0
        result.append(dict(offset=record["offset"], screen=screen))
    return result


def execute(binary, prefix, case, sizes, output):
    command = [str(binary), str(output / "input.bin"), str(case.rows), str(case.columns),
               str(case.history), str(output / "chunks.txt")]
    try:
        process = subprocess.run(command, capture_output=True, timeout=20)
        stdout, stderr, returncode = process.stdout, process.stderr, process.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, returncode = error.stdout or b"", error.stderr or b"", None
    except OSError as error:
        stdout, stderr, returncode = b"", str(error).encode("utf-8"), None
    (output / f"{prefix}.jsonl").write_bytes(stdout)
    (output / f"{prefix}.stderr.txt").write_bytes(stderr)
    records, error = [], None
    if returncode == 0:
        try:
            records = common_projection([json.loads(line) for line in stdout.decode("utf-8").splitlines()])
            if not records or records[-1]["offset"] != len(case.data):
                raise ValueError("missing final checkpoint")
        except (ValueError, KeyError, TypeError) as exc:
            error = str(exc)
    return records, returncode, error


def run_case(core_binary, candidate_binary, case, plan, output):
    output.mkdir(parents=True, exist_ok=False)
    sizes = chunks(len(case.data), plan)
    (output / "input.bin").write_bytes(case.data)
    (output / "chunks.txt").write_text("".join(f"{size}\n" for size in sizes), encoding="ascii")
    core, core_code, core_error = execute(core_binary, "core", case, sizes, output)
    candidate, candidate_code, candidate_error = execute(candidate_binary, "candidate", case, sizes, output)
    difference = None
    if core_code == 0 and candidate_code == 0 and not core_error and not candidate_error:
        difference = compare_traces(core, candidate)
    status = "execution_failure" if core_code != 0 or candidate_code != 0 or core_error or candidate_error else "match"
    if difference:
        signature = dict(input_sha256=sha(output / "input.bin"), rows=case.rows, columns=case.columns,
                         history=case.history,
                         first_difference={key: value for key, value in difference.items() if key != "checkpoint"},
                         final_core_sha256=state_sha(core[-1]["screen"]),
                         final_candidate_sha256=state_sha(candidate[-1]["screen"]))
        known_path = Path(__file__).with_name("candidate-known-differences.json")
        known = json.loads(known_path.read_text(encoding="utf-8")) if known_path.exists() else {}
        expected = known.get(case.name, {}).get("plans", {}).get(plan)
        status = "reference_difference" if expected == signature else "difference"
    report = dict(case=case.name, plan=plan, input_sha256=sha(output / "input.bin"),
                  bytes=len(case.data), rows=case.rows, columns=case.columns, history=case.history,
                  chunk_sizes=sizes, checkpoints=len(core), status=status,
                  core_returncode=core_code, candidate_returncode=candidate_code,
                  core_error=core_error, candidate_error=candidate_error,
                  first_difference=difference,
                  difference_direction="expected=TermForge core; actual=Alacritty candidate; neither is an oracle",
                  normalization="NFC text; blank/empty cells share a projection; explicit-space semantics are excluded; mode_bits/replies are not compared here",
                  reproduce={"core": ["terminal_trace", "input.bin", str(case.rows), str(case.columns),
                                      str(case.history), "chunks.txt"],
                             "candidate": ["termforge-alacritty-trace", "input.bin", str(case.rows),
                                           str(case.columns), str(case.history), "chunks.txt"]})
    write_json(output / "report.json", report)
    return report, core, candidate


def run_suite(core_binary, candidate_binary, output, selected=None, plans=None):
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for case in selected or cases():
        byte_core, byte_candidate = None, None
        for plan in plans or ["byte", "whole", "seed:42", "seed:20261022", "seed:8675309"]:
            folder = output / case.name / plan.replace(":", "-")
            report, core, candidate = run_case(core_binary, candidate_binary, case, plan, folder)
            if plan == "byte":
                byte_core, byte_candidate = core, candidate
            elif byte_core is not None and len(byte_core) == len(case.data) + 1:
                offsets = [item["offset"] for item in core]
                report["core_fragmentation_difference"] = compare_traces([byte_core[i] for i in offsets], core)
                report["candidate_fragmentation_difference"] = compare_traces(
                    [byte_candidate[i] for i in offsets], candidate)
                if report["core_fragmentation_difference"] or report["candidate_fragmentation_difference"]:
                    report["status"] = "fragmentation_failure"
                write_json(folder / "report.json", report)
            records.append(report)
    sources = ["experiments/core/trace.cpp", "experiments/core/terminal.hpp", "experiments/core/screen.hpp",
               "experiments/core/unicode_width.hpp", "experiments/differential/run.py",
               "experiments/differential/cases.py", "experiments/differential/compare_candidate.py",
               "experiments/differential/candidate-known-differences.json",
               "experiments/candidates/alacritty/src/main.rs", "experiments/candidates/alacritty/Cargo.toml",
               "experiments/candidates/alacritty/Cargo.lock"]
    execution_passed = all(record["status"] not in ("execution_failure", "fragmentation_failure")
                           for record in records)
    summary = dict(schema=1, candidate="alacritty_terminal", version="0.26.0",
                   platform=platform.platform(), python=platform.python_version(),
                   core_trace_sha256=sha(core_binary), candidate_trace_sha256=sha(candidate_binary),
                   source_sha256={path: sha(ROOT / path) for path in sources},
                   runs=len(records), checkpoints=sum(record["checkpoints"] for record in records),
                   counts={status: sum(record["status"] == status for record in records)
                           for status in sorted({record["status"] for record in records})},
                   passed=execution_passed, execution_passed=execution_passed,
                   semantic_acceptance=False,
                   differences_adjudicated=all(record["status"] in ("match", "reference_difference")
                                               for record in records),
                   performance_measured=False, records=records)
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--case", choices=[case.name for case in cases()])
    parser.add_argument("--plan", help="byte, whole, or seed:<integer>; default runs all five plans")
    args = parser.parse_args()
    core = args.trace.resolve(strict=True) if args.trace else None
    if core is None:
        from run import trace_binary
        core = trace_binary()
    candidate = args.candidate.resolve(strict=True) if args.candidate else candidate_trace_binary()
    selected = [case for case in cases() if args.case is None or case.name == args.case]
    result = run_suite(core, candidate, args.out.resolve(), selected, [args.plan] if args.plan else None)
    print(json.dumps({key: result[key] for key in ("execution_passed", "runs", "checkpoints", "counts",
                                                   "semantic_acceptance")}, indent=2))
    return 0 if result["execution_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
