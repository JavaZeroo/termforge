"""Supplement one preserved hidden-primary recording without rerunning its probe.

A fresh namespace adds exactly ``61 spaces + x`` from the return-only stage onward.
This finite source-derived candidate permits an observation, not a claim about
internal frame storage. The original text-allowlist failure must still reproduce,
and its mechanics file must remain absent.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
from types import FunctionType, SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import export_resize_frame_reference as exporter
from research.ui import resize_frame_reference as probe

require, read, sha, digest = exporter.require, exporter.read, exporter.sha, exporter.digest
CASE = "hidden-primary"
ORIGINAL_ERROR = "Only fixed frame-source synthetic document lines are allowed"
RAW_NAMES = tuple(name for name in exporter.raw_names(CASE) if name != "mechanics-check.json")
AUDIT_SOURCES = ("research/ui/audit_resize_frame_hidden.py", *exporter.EXPORT_SOURCES)
FROZEN_SOURCES = {
    "research/ui/resize_frame_reference.py": "cb02eb95ff12caff05accaed5223e816cb081818e5a077d6c562070979f02928",
    "research/ui/export_resize_frame_reference.py": "0d76dae1a64741970bf2ec77d053623f5942b4c2ef2919851f24057b3e9f600b",
    "research/ui/export_resize_state_reference.py": "473e901305354d4ba8b22c08d22fa506c4d40da0e73bed3bf3122e4ff0e3c9b7",
    "research/ui/export_mode_state_reference.py": "7e0a1e0e49f149791464c0dec357d3b5260bc2c34ca9f9c1be4edab35626cdf3",
}


def frozen_sources():
    require(all(sha(ROOT / name) == expected for name, expected in FROZEN_SOURCES.items()),
            "Frozen original audit/source bytes differ")


def synthetic_lines(case, phase):
    require(case == CASE, "Supplement applies only to the fixed hidden-primary case")
    result = set(probe.synthetic_lines(case, phase))
    if phase >= 3:
        # The known source is 90 spaces + wx: preserve the existing one-cell
        # successor x after a finite 61-column predecessor. No arbitrary text,
        # length, padding horizon or earlier phase is admitted.
        result.add(" " * 61 + "x")
    return result


def validate_document(case, phase, terminal):
    original = probe.validate_document
    bounded = FunctionType(original.__code__, {**original.__globals__, "synthetic_lines": synthetic_lines},
                           argdefs=original.__defaults__)
    # The original validator still enforces every source-marker count and phase.
    bounded(case, phase, terminal)


def reproduce_failure(directory):
    require(not (directory / "mechanics-check.json").exists(), "Original mechanics file must remain absent")
    try:
        probe.audit_mechanics(directory, CASE)
    except ValueError as error:
        require(type(error) is ValueError and str(error) == ORIGINAL_ERROR,
                "Original audit failed for a different reason")
    else:
        raise ValueError("Original fixed text-allowlist failure did not reproduce")


def capture_facts(directory, plan, source_root=ROOT):
    frozen_sources()
    require(plan["case_id"] == CASE, "Supplement applies only to the fixed hidden-primary case")
    # Fail closed on duplicate fields and nonfinite values before the frozen
    # runtime's ordinary json.loads sees the same immutable recording.
    for name in RAW_NAMES:
        if name.endswith(".json"):
            read(directory / name)
    reproduce_failure(directory)
    original = probe.audit_mechanics
    mechanics = FunctionType(original.__code__, {**original.__globals__, "validate_document": validate_document},
                             argdefs=original.__defaults__)
    adapted_probe = SimpleNamespace(**{**vars(probe), "audit_mechanics": mechanics, "synthetic_lines": synthetic_lines})
    source = exporter.adapt(exporter._source, {
        'require(read(directory / "mechanics-check.json") == mechanics, "Saved mechanics audit differs")':
        'require(not (directory / "mechanics-check.json").exists(), "Original mechanics file must remain absent")',
    })
    namespace = {**exporter._CAPTURE, "probe": adapted_probe}
    exec(compile(source, str(Path(exporter.__file__)), "exec"), namespace)
    # Keep the existing stage roles and document hashes, as well as all its
    # provider, script, exact geometry, NAWS, DSR and marker-text checks.
    wrapper = exporter.capture_facts
    capture = FunctionType(wrapper.__code__, {**wrapper.__globals__, "_CAPTURE": namespace, "probe": adapted_probe},
                           argdefs=wrapper.__defaults__)
    return capture(directory, plan, source_root)


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in
                                                    (directory, plan_path, destination, source_root)]
    # The caller supplies the old authorized plan's external hash. Never infer
    # that authorization from a mutable plan read from the recording directory.
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    require(not destination.exists(), "Export destination must be new")
    plan = read(plan_path)
    require(plan.get("case_id") == CASE and all(type(plan.get(k)) is type(v) and plan[k] == v
            for k, v in probe.fixed_contract(CASE).items()), "Fixed hidden-primary contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    before = {name: sha(directory / name) for name in RAW_NAMES}
    facts = capture_facts(directory, plan, source_root)
    original = exporter.provenance_facts
    def preserved_raw_names(case):
        require(case == CASE, "Supplement applies only to the fixed hidden-primary case")
        return RAW_NAMES
    provenance_function = FunctionType(original.__code__, {**original.__globals__, "raw_names": preserved_raw_names},
                                       argdefs=original.__defaults__)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_function(directory, plan_path, plan, source_root, reference_source.resolve())
    require(before == {name: sha(directory / name) for name in RAW_NAMES} and
            sha(plan_path) == expected_plan_sha256 and not (directory / "mechanics-check.json").exists(),
            "Preserved recording/plan changed during supplemental audit")
    result = {
        "schema": "termforge-original-resize-frame-hidden-supplement-v1", "issue": 29, "related_issue": 27,
        "case_id": CASE, "authorized_plan_sha256": expected_plan_sha256,
        "offline_audit_source_files_sha256": {name: sha(ROOT / name) for name in AUDIT_SOURCES},
        "status": "original-audit-failed-offline-supplement-passed", "failure_preserved": True,
        "original_audit": {"status": "failed", "error_type": "ValueError", "message": ORIGINAL_ERROR,
                           "failure_reproduced": True, "mechanics_file_absent": True},
        "offline_supplemental_audit": {"status": "passed", "additional_original_runs": 0,
            "additional_candidate": {"leading_spaces": 61, "suffix": "x", "length": 62},
            "additional_text_candidates": 1, "applicable_phases": [3, 4, 5, 6, 7]},
        "actual_original_runs": 1, "maximum_original_runs": 1, "semantic_acceptance": False,
        "frame_policy_classification": None, "full_active_grid_verified": False,
        "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
        "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
        "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
        "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
        "capture": facts, "provenance": provenance,
        "limitations": [
            "The original fixed audit failed; that failure is reproduced and its missing mechanics file is not fabricated.",
            "Only one finite source-derived candidate is added from the untouched return stage onward; no target is rerun.",
            "The separate offline audit rechecks the frozen protocol, geometry, generated script, provider, complete original package, sources and profile.",
            "Document markers, DSR and visible rectangles remain separate observations; no internal frame or origin bit is classified.",
            "This one direct Telnet recording does not establish full grid, styles, local-shell transport, reliability repetition or performance acceptance.",
            "Raw document text, paths, process identifiers, profiles and original artifacts remain private.",
        ],
    }
    payload = (json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(payload)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "plan", "destination"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--reference-source", type=Path)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination, args.expected_plan_sha256,
                    args.source_root, args.reference_source)
    print(json.dumps({"exported": True, "schema": result["schema"], "original_audit_failed": True,
                      "supplemental_audit_passed": True, "sha256": sha(args.destination)}))
