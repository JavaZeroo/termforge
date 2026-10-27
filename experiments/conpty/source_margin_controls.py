"""One bounded source-pair comparison for three additional native controls."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import subprocess

from experiments.candidates.alacritty.run import run_trace
from experiments.conpty.margin_controls import (
    evaluate_native, expected_native, expected_source, matches_unpatched_prediction,
)
from experiments.conpty.margin_run import sha256, validate_lifecycle
from experiments.conpty.source_margin_run import identity, reviewed_pair, ROOT, RUN, UPSTREAM
from experiments.conpty.test_backend import BackendTests
from experiments.shell.workflow_checks import validate_console_window

CASES = ("main-only", "default-alt", "repeat-alt")
GRIDS = ((80, 24), (12, 7))


def inspect_case(directory, trace, case, columns, rows):
    source = (directory / "source.bin").read_bytes()
    if source != expected_source(case, columns, rows):
        raise ValueError("fixed control source changed")
    transport = (directory / "transport.bin").read_bytes()
    if not 0 < len(transport) <= 8192:
        raise ValueError("control transport bound")
    expected = expected_native(case, columns, rows)
    states = {stage: validate_console_window(directory, expected_role=stage,
                                             expected_generation=1, prefix=stage)
              for stage in expected}
    report, records = run_trace(trace, transport, rows, columns, 100,
                                [len(transport)], directory / "replay")
    if report["passed"] is not True:
        raise ValueError("control replay rejected")
    final = records[-1]
    lines = ["".join(cell["text"] for cell in line if cell.get("width", 1)).rstrip()
             for line in final["screen"]["cells"]]
    alternate = bool(final["mode_bits"] & (1 << 12))
    return {"source_bytes": len(source), "source_sha256": sha256(directory / "source.bin"),
            "transport_bytes": len(transport), "transport_sha256": sha256(directory / "transport.bin"),
            "native_contract": evaluate_native(states, case, columns, rows),
            "unpatched_prediction_matches": matches_unpatched_prediction(states, case, columns, rows),
            "native_lines": {stage: row["lines"] for stage, row in states.items()},
            "native_cursor": {stage: [row["cursor_row"], row["cursor_column"]] for stage, row in states.items()},
            "native_cell_sha256": {stage: sha256(directory / (stage + ".cells.bin")) for stage in expected},
            "terminal_lines": lines, "terminal_alternate": alternate,
            "terminal_final_passed": lines == expected["final"]["lines"] and not alternate}


def compare(samples):
    results = []
    for columns, rows in GRIDS:
        for case in CASES:
            baseline = samples[f"baseline-{columns}x{rows}-{case}"]
            patched = samples[f"patched-{columns}x{rows}-{case}"]
            completed = all(row.get("status") == "completed" for row in (baseline, patched))
            qualified = (completed and baseline["unpatched_prediction_matches"] and
                         baseline["native_contract"]["passed"] == (case == "main-only"))
            repaired = completed and patched["native_contract"]["passed"]
            terminal = completed and all(row["terminal_final_passed"] for row in (baseline, patched))
            results.append({"case": case, "columns": columns, "rows": rows,
                            "baseline_matches_prediction": qualified,
                            "patched_native_passed": repaired, "both_terminal_finals_passed": terminal,
                            "passed": qualified and repaired and terminal})
    return results


def run(baseline, patched, probe, trace, output):
    pairs = {name: reviewed_pair(folder, name) for name, folder in
             (("baseline", baseline), ("patched", patched))}
    probe, trace = Path(probe).resolve(strict=True), Path(trace).resolve(strict=True)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    subject = BackendTests()
    subject.out, subject.serial = output / "_private" / "supervision", 0
    sources = ["CMakeLists.txt", "experiments/conpty/backend.hpp", "experiments/conpty/session.hpp",
               "experiments/conpty/test_backend.py", "experiments/conpty/margin_controls_probe.cpp",
               "experiments/conpty/margin_controls.py", "experiments/conpty/source_margin_controls.py",
               "experiments/conpty/margin_run.py", "experiments/conpty/source_margin_run.py",
               "experiments/conpty/run.py", "experiments/candidates/alacritty/run.py",
               "experiments/shell/workflow_fixture.hpp", "experiments/shell/workflow_checks.py"]
    summary = {"schema": "termforge-source-margin-controls-v1", "upstream_source": UPSTREAM,
               "generated_utc": datetime.now(timezone.utc).isoformat(), "build_run": RUN,
               "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
               "source_files": {name: sha256(ROOT / name) for name in sources},
               "platform": platform.platform(), "probe_sha256": sha256(probe), "trace_sha256": sha256(trace),
               "pair_receipts": {name: receipt for name, (_, receipt) in pairs.items()},
               "deadlines": {"child_seconds": 5, "outer_seconds": 15, "cleanup_seconds": 5},
               "planned_executions": 12, "samples": {}, "comparisons": [], "passed": False,
               "full_r2_contract_verified": False, "product_backend_selected": False,
               "presented_done": None,
               "unmeasured": ["styled_source", "history_stress", "resize", "queries_and_api_writes",
                              "terminal_stage_cursors", "full_product", "performance", "WindTerm_equivalence"]}

    def save():
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    try:
        # Exactly one execution per planned cell; failed cells remain failed.
        for columns, rows in GRIDS:
            for case in CASES:
                for variant, (directory, _) in pairs.items():
                    name = f"{variant}-{columns}x{rows}-{case}"
                    folder = output / "_private" / name
                    folder.mkdir(parents=True)
                    sample = {"status": "failed", "case": case, "columns": columns, "rows": rows,
                              "supervision_case": subject.serial}
                    stage = "native_execution"
                    try:
                        lifecycle = subject.invoke(directory, case, executable=probe,
                                                   arguments=(folder, columns, rows), timeout=15)
                        sample["lifecycle"] = lifecycle
                        stage = "lifecycle_validation"
                        job = lifecycle["outer_job_after"]
                        if (lifecycle["returncode"] != 0 or lifecycle["error"] is not None or
                                job["active"] != 0 or job["total"] < 1 or job["terminated"] != 0):
                            raise ValueError("control native probe failed")
                        validate_lifecycle(lifecycle["native"], True, backend_identity=identity(variant))
                        stage = "capture_and_replay_validation"
                        sample.update(inspect_case(folder, trace, case, columns, rows), status="completed")
                    except Exception as error:
                        sample["error_type"] = type(error).__name__
                        sample["failure_stage"] = stage
                    summary["samples"][name] = sample
                    save()
        summary["comparisons"] = compare(summary["samples"])
        summary["passed"] = all(row["passed"] for row in summary["comparisons"])
    finally:
        save()
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "patched", "probe", "candidate-trace", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    result = run(args.baseline, args.patched, args.probe, args.candidate_trace, args.output)
    print(json.dumps({"passed": result["passed"], "comparisons": result["comparisons"]}))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
