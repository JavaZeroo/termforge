"""Supplement one preserved EL1 recording without changing or rerunning its probe.

The original audit must still fail with its exact text-allowlist error. A fresh
namespace adds only the fixed source interval [91, 92) erased in the retained
``90 spaces + wx`` line. The original mechanics file must remain absent.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
from types import FunctionType, SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import export_resize_address_reference as exporter
from research.ui import resize_address_reference as probe

require, read, sha, digest = exporter.require, exporter.read, exporter.sha, exporter.digest
CASE = "erase-line-start"
ORIGINAL_ERROR = "Only fixed action-derived synthetic document lines are allowed"
RAW_NAMES = tuple(name for name in exporter.RAW_NAMES if name != "mechanics-check.json")
AUDIT_SOURCES = ("research/ui/audit_resize_address_erase.py", *exporter.EXPORT_SOURCES)
FROZEN_SOURCES = {
    "research/ui/resize_address_reference.py": "033f263efc01d48a5e8b78f2277a78b661a6f3d110a3fb5f18dbee615ff2cab4",
    "research/ui/export_resize_address_reference.py": "50e52df2882cfb85ec60d511840a71b720050315ed492eddbcc9916dfc2866a4",
    "research/ui/export_resize_state_reference.py": "473e901305354d4ba8b22c08d22fa506c4d40da0e73bed3bf3122e4ff0e3c9b7",
    "research/ui/export_mode_state_reference.py": "7e0a1e0e49f149791464c0dec357d3b5260bc2c34ca9f9c1be4edab35626cdf3",
}


def frozen_sources():
    require(all(sha(ROOT / name) == expected for name, expected in FROZEN_SOURCES.items()),
            "Frozen original audit/source bytes differ")


def synthetic_lines(case, phase):
    require(case == CASE, "Supplement applies only to the fixed EL1 case")
    result = probe.synthetic_lines(case, phase)
    if phase >= 2:
        original = " " * 90 + "wx"
        result.add(original[:91] + " " + original[92:])
    return result


def validate_document(case, phase, terminal):
    original = probe.validate_document
    bounded = FunctionType(original.__code__, {**original.__globals__, "synthetic_lines": synthetic_lines},
                           argdefs=original.__defaults__)
    bounded(case, phase, terminal)
    # Every nonblank source marker occurs at most once in the fixed input.
    # Repeating an otherwise allowed line must not duplicate those markers.
    for marker in exporter.MARKERS:
        require(sum(line.count(marker) for line in terminal["lines"]) <= 1,
                "Repeated synthetic source marker")


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
    require(plan["case_id"] == CASE, "Supplement applies only to the fixed EL1 case")
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
            for k, v in probe.fixed_contract(CASE).items()), "Fixed EL1 contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    before = {name: sha(directory / name) for name in RAW_NAMES}
    facts = capture_facts(directory, plan, source_root)
    original = exporter.provenance_facts
    provenance_function = FunctionType(original.__code__, {**original.__globals__, "RAW_NAMES": RAW_NAMES},
                                       argdefs=original.__defaults__)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_function(directory, plan_path, plan, source_root, reference_source.resolve())
    require(before == {name: sha(directory / name) for name in RAW_NAMES} and
            sha(plan_path) == expected_plan_sha256 and not (directory / "mechanics-check.json").exists(),
            "Preserved recording/plan changed during supplemental audit")
    result = {
        "schema": "termforge-original-resize-address-erase-supplement-v1", "issue": 29, "related_issue": 27,
        "case_id": CASE, "authorized_plan_sha256": expected_plan_sha256,
        "offline_audit_source_files_sha256": {name: sha(ROOT / name) for name in AUDIT_SOURCES},
        "status": "original-audit-failed-offline-supplement-passed", "failure_preserved": True,
        "original_audit": {"status": "failed", "error_type": "ValueError", "message": ORIGINAL_ERROR,
                           "failure_reproduced": True, "mechanics_file_absent": True},
        "offline_supplemental_audit": {"status": "passed", "additional_original_runs": 0,
            "source_interval": {"logical_source_columns": 92, "begin": 91, "end": 92},
            "additional_text_candidates": 1, "applicable_phases": [2, 3]},
        "actual_original_runs": 1, "maximum_original_runs": 1, "semantic_acceptance": False,
        "addressing_policy_classification": None, "full_active_grid_verified": False,
        "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
        "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
        "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
        "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
        "capture": facts, "provenance": provenance,
        "limitations": [
            "The original fixed audit failed; that failure is reproduced and its missing mechanics file is not fabricated.",
            "Only one finite source-derived interval-erasure candidate is added for post-action phases; no target is rerun.",
            "The separate offline audit rechecks the frozen protocol, geometry, generated script, provider, complete original package, sources and profile.",
            "Document markers, DSR and visible rectangles remain separate observations; no internal addressing policy is classified.",
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
