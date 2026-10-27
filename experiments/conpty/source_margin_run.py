"""Bounded same-source native margin comparison; no product backend selection."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import subprocess

from experiments.conpty.margin_contract import inspect_native
from experiments.conpty.margin_run import execute, sha256, SOURCE_SHA256

ROOT = Path(__file__).resolve().parents[2]
RUN = "40449292015"
UPSTREAM = "dc8ae365847d41f62d1ebca93815bb00959bc12f"
PATCH = "dc27377e399bd0217a0b76e16cc89f087c1b781929e4c958ad30c3a475322b81"
SUMMARY = "8132695b32162ff22e61d2de166a31fad508f0b63cbb46de8d1612bd197e1ded"
DLL = "9d165745cce4b29a5818a45c23f68e962b3a3140c2e00566bd17fee970fdd9d1"
HOSTS = {
    "baseline": ("fabdc934d9755c405fe848251905e8c0e70a2bf7702b8ed7ccd104eec6591588", 1146880),
    "patched": ("1c8f071f6e4779ec297fb041af4eaea39be29b6920cef625ab91d1cbc20c79cb", 1147392),
}


def identity(variant):
    if variant not in HOSTS:
        raise ValueError("unknown source variant")
    return f"source-dc8-r56-{variant}-run{RUN}"


def reviewed_pair(directory, variant):
    """Bind both binary bytes and their audited build receipt before launch."""
    directory = Path(directory).resolve(strict=True)
    host_hash, host_bytes = HOSTS[variant]
    files = {"OpenConsole.exe": {"sha256": host_hash, "bytes": host_bytes},
             "conpty.dll": {"sha256": DLL, "bytes": 108544}}
    expected = {"schema": "termforge-upstream-conpty-variant-v1", "upstream_source": UPSTREAM,
                "workflow_run": RUN, "production_patch_applied": variant == "patched",
                "focused_adapter_result": "passed" if variant == "patched" else "expected-failure",
                "full_adapter_verified": variant == "patched", "patch_sha256": PATCH, "files": files}
    receipt = json.loads((directory / "variant.json").read_text(encoding="utf-8-sig"))
    # JSON comparison distinguishes booleans from numbers and rejects extra fields.
    if json.dumps(receipt, sort_keys=True) != json.dumps(expected, sort_keys=True):
        raise ValueError("source variant receipt differs")
    if sha256(directory / "summary.json") != SUMMARY:
        raise ValueError("source build receipt differs")
    if sha256(directory / "dc8-native-margin-candidate.patch") != PATCH:
        raise ValueError("source patch differs")
    for name, record in files.items():
        file = directory / name
        if file.stat().st_size != record["bytes"] or sha256(file) != record["sha256"]:
            raise ValueError("source pair identity differs")
    return directory, expected


def expected_baseline_failure(sample):
    if sample.get("status") != "completed":
        return False
    stages = sample["native_contract"]["stages"]
    return (all(stages[name]["passed"] for name in ("before", "alt", "restored")) and
            stages["final"] == {"text": False, "cell_text": False, "cells_and_attributes": False,
                                "cursor": True, "passed": False} and
            sample["native_lines"]["final"][:5] ==
            ["TOP_FIXED", "REGION_A", "REGION_B", "REGION_CIXED", "REGION_D"])


def compare(samples):
    results = []
    for columns, rows in ((80, 24), (12, 7)):
        get = lambda variant, mode: samples[f"{variant}-{columns}x{rows}-{mode}"]
        baseline, patched = get("baseline", "vt-on"), get("patched", "vt-on")
        off_baseline, off_patched = get("baseline", "vt-off"), get("patched", "vt-off")
        native_red = expected_baseline_failure(baseline)
        native_green = (patched.get("status") == "completed" and
                        patched["native_contract"]["all_native_stages_passed"])
        terminal_green = all(s.get("status") == "completed" and
                             s["terminal_main_margins_restored"] and not s["terminal_alternate"]
                             for s in (baseline, patched))
        off_equal = (all(s.get("status") == "completed" for s in (off_baseline, off_patched)) and
                     all(off_baseline[key] == off_patched[key] for key in
                         ("native_lines", "native_cursor", "native_cell_sha256", "terminal_lines")))
        results.append({"columns": columns, "rows": rows, "baseline_expected_native_failure": native_red,
                        "patched_all_native_stages_passed": native_green,
                        "both_terminal_finals_passed": terminal_green, "vt_off_unchanged": off_equal,
                        "passed": native_red and native_green and terminal_green and off_equal})
    return results


def run(baseline, patched, probe, trace, output):
    pairs = {name: reviewed_pair(folder, name) for name, folder in
             (("baseline", baseline), ("patched", patched))}
    probe, trace = Path(probe).resolve(strict=True), Path(trace).resolve(strict=True)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = ["CMakeLists.txt", "experiments/conpty/backend.hpp", "experiments/conpty/session.hpp",
               "experiments/conpty/margin_probe.cpp", "experiments/conpty/margin_run.py",
               "experiments/conpty/margin_contract.py", "experiments/conpty/source_margin_run.py"]
    summary = {"schema": "termforge-source-conpty-margin-v1", "generated_utc": datetime.now(timezone.utc).isoformat(),
               "upstream_source": UPSTREAM, "build_run": RUN, "source_sha256": SOURCE_SHA256,
               "termforge_source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
               "source_files": {name: sha256(ROOT / name) for name in sources},
               "platform": platform.platform(), "probe_sha256": sha256(probe), "trace_sha256": sha256(trace),
               "pair_receipts": {name: receipt for name, (_, receipt) in pairs.items()},
               "samples": {}, "comparisons": [], "passed": False,
               "full_r2_contract_verified": False, "product_backend_selected": False,
               "presented_done": None,
               "unmeasured": ["styled_source", "history", "resize_cross_margin", "terminal_stage_cursors",
                              "full_product_workflows", "performance", "WindTerm_equivalence"]}
    try:
        # One planned execution per variant/grid/mode. Failures remain distinct;
        # there is no retry, altered input, deadline extension or fallback.
        for columns, rows in ((80, 24), (12, 7)):
            for mode in ("vt-on", "vt-off"):
                for variant, (directory, _) in pairs.items():
                    name = f"{variant}-{columns}x{rows}-{mode}"
                    case = output / "_private" / name
                    try:
                        sample = execute(probe, str(directory / "conpty.dll"), trace, case,
                                         mode, columns, rows, backend_identity=identity(variant))
                        sample["native_cell_sha256"] = {stage: sha256(case / f"{stage}.cells.bin")
                                                        for stage in ("before", "alt", "restored", "final")}
                        if mode == "vt-on":
                            sample["native_contract"] = inspect_native(case, columns, rows)
                    except Exception as error:
                        diagnostic = case / "diagnostic.json"
                        sample = json.loads(diagnostic.read_text()) if diagnostic.exists() else {}
                        sample.update(status="failed", error_type=type(error).__name__)
                    summary["samples"][name] = sample
                    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        summary["comparisons"] = compare(summary["samples"])
        summary["passed"] = all(row["passed"] for row in summary["comparisons"])
    finally:
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--patched", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--candidate-trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run(args.baseline, args.patched, args.probe, args.candidate_trace, args.output)
    print(json.dumps({"passed": result["passed"], "comparisons": result["comparisons"]}))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
