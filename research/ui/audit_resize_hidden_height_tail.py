"""Supplement one preserved hidden-height recording without rerunning its probe.

The original audit must still fail with its exact text-allowlist error. A fresh
namespace adds only S[:61]+S[91:] from S = 89 spaces + uvw, at document row 11
of phases 5 and 6. The original mechanics file must remain absent.
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
from research.ui import export_resize_hidden_height_reference as exporter
from research.ui import resize_hidden_height_reference as probe

require, read, sha, digest = exporter.require, exporter.read, exporter.sha, exporter.digest
CASE = "hidden-primary-height"
ORIGINAL_ERROR = "Only fixed hidden-height-source synthetic document lines are allowed"
RAW_NAMES = tuple(name for name in exporter.raw_names(CASE) if name != "mechanics-check.json")
AUDIT_SOURCES = ("research/ui/audit_resize_hidden_height_tail.py", *exporter.EXPORT_SOURCES)
FROZEN_SOURCES = {
    'research/ui/resize_hidden_height_reference.py': '565d10b69b7a1015c3412b4b5fd974818acd395aa8ba846835d23cc8998bb0bc',
    'research/ui/resize_hidden_height_reference.js': 'd03cb790dd9db24275f859e297d867453e35675db2ab3f730e0b3f775fd57bc9',
    'research/ui/resize_state_reference.py': '628f9e3a5e3ecea6c34829810df2cdf0e93dc221bd3d19b31b3f5028759f07fd',
    'research/ui/mode_state_reference.py': 'bac745e74a2653412ea7a701e3a1f3defa1fb97fdd7841b73fad483120496ec4',
    'research/ui/alternate_reference.py': '94b85d49347e0e9813f7acddf0244f58520a7d1fce14084677c91cf4ddafffea',
    'research/ui/r1_launch.py': '54fb41872c6abcc48242a0938044184854b016785cc13659c66b1bf404da0d8a',
    'research/ui/launch.py': '6970e0d4f2fe5e7ec4827df7aa1026cd0e74aeb828cdc9b7a0b42b3c887eb4e6',
    'research/ui/qt_r1_text.js': 'f5820d413fdd72c13dffcefbf4d0ade615abfe15754c6b0b52e1e098814eea6f',
    'research/ui/r1_vt_fixture.py': '5853f684d5b8a4c17a30f22a536f04f0eb212813b8d6b3cc3c197df392eb732a',
    'experiments/conpty/run.py': '3766879cafe38aabf53cfac8ab2f62985b1976b6fa24c8b6031258bdc914258f',
    'research/ui/export_resize_hidden_height_reference.py': 'bcd8ed771ca0233dfb63b2a11876daf5a13020ba612e4bc6f306acdb9133094f',
    'research/ui/export_resize_state_reference.py': '473e901305354d4ba8b22c08d22fa506c4d40da0e73bed3bf3122e4ff0e3c9b7',
    'research/ui/export_mode_state_reference.py': '7e0a1e0e49f149791464c0dec357d3b5260bc2c34ca9f9c1be4edab35626cdf3',
}


def frozen_sources():
    require(all(sha(ROOT / name) == expected for name, expected in FROZEN_SOURCES.items()),
            "Frozen original audit/source bytes differ")


def synthetic_lines(case, phase):
    require(case == CASE, "Supplement applies only to the fixed hidden-height case")
    result = set(probe.synthetic_lines(case, phase))
    if phase in (5, 6):
        original = " " * 89 + "uvw"
        result.add(original[:61] + original[91:])
    return result


def validate_document(case, phase, terminal):
    original = probe.validate_document
    bounded = FunctionType(original.__code__, {**original.__globals__, "synthetic_lines": synthetic_lines},
                           argdefs=original.__defaults__)
    bounded(case, phase, terminal)
    candidate = " " * 61 + "w"
    if candidate in terminal["lines"]:
        require(phase in (5, 6) and len(terminal["lines"]) > 11 and terminal["lines"][11] == candidate,
                "Supplemental tail must stay at captured document row 11")
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
    require(plan["case_id"] == CASE, "Supplement applies only to the fixed hidden-height case")
    # Fail closed on duplicate fields and nonfinite values before the frozen
    # runtime's ordinary json.loads sees the same immutable recording.
    for name in RAW_NAMES:
        if name.endswith(".json"):
            read(directory / name)
    reproduce_failure(directory)
    raw = read(directory / probe.RESULT_FILE)
    for phase in (5, 6):
        lines = raw["observations"][phase]["terminal"]["lines"]
        require(len(lines) > 11 and lines[11] == " " * 61 + "w",
                "Preserved return-phase tail observation differs")
    original = probe.audit_mechanics
    mechanics = FunctionType(original.__code__, {**original.__globals__, "validate_document": validate_document},
                             argdefs=original.__defaults__)
    adapted_probe = SimpleNamespace(**{**vars(probe), "audit_mechanics": mechanics,
                                      "synthetic_lines": synthetic_lines, "validate_document": validate_document})
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
    facts = capture(directory, plan, source_root)
    for phase in facts["phases"]:
        phase["document_projection"]["synthetic_text"]["catalog_source"] = (
            "audit_resize_hidden_height_tail.synthetic_lines(case_id, phase); sorted ASCII scalar order")
    return facts


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in
                                                    (directory, plan_path, destination, source_root)]
    # The caller supplies the old authorized plan's external hash. Never infer
    # that authorization from a mutable plan read from the recording directory.
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    require(not destination.exists(), "Export destination must be new")
    plan = read(plan_path)
    require(plan.get("case_id") == CASE and all(type(plan.get(k)) is type(v) and plan[k] == v
            for k, v in probe.fixed_contract(CASE).items()), "Fixed hidden-height contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    before = {name: sha(directory / name) for name in RAW_NAMES}
    facts = capture_facts(directory, plan, source_root)
    original = exporter.provenance_facts
    provenance_function = FunctionType(original.__code__, {**original.__globals__, "raw_names": lambda case: RAW_NAMES},
                                       argdefs=original.__defaults__)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_function(directory, plan_path, plan, source_root, reference_source.resolve())
    require(before == {name: sha(directory / name) for name in RAW_NAMES} and
            sha(plan_path) == expected_plan_sha256 and not (directory / "mechanics-check.json").exists(),
            "Preserved recording/plan changed during supplemental audit")
    basis = probe.fixed_contract(CASE)["geometry_basis"]["combined"]
    result = {
        "schema": "termforge-original-resize-hidden-height-tail-supplement-v1", "issue": 29, "related_issue": 27,
        "case_id": CASE, "authorized_plan_sha256": expected_plan_sha256,
        "offline_audit_source_files_sha256": {name: sha(ROOT / name) for name in AUDIT_SOURCES},
        "status": "original-audit-failed-offline-supplement-passed", "failure_preserved": True,
        "original_audit": {"status": "failed", "error_type": "ValueError", "message": ORIGINAL_ERROR,
                           "failure_reproduced": True, "mechanics_file_absent": True},
        "offline_supplemental_audit": {"status": "passed", "additional_original_runs": 0,
            "source_interval": {"source_ascii_bytes": 92, "begin": 61, "end": 91,
                                "operation": "S[:61] + S[91:]", "source_utf8_hex": (" "*89+"uvw").encode().hex(),
                                "candidate_utf8_hex": (" "*61+"w").encode().hex(),
                                "candidate_sha256": "fd45397fff8c462d3d86f913e07aab5f74f3e2675d07c05fdc61ad7e0a08110a"},
            "additional_text_candidates": 1, "applicable_phases": [5, 6], "document_line": 11},
        "geometry_basis": {"combined": {"reference": basis["reference"],
            "strict_export_sha256": digest(basis["strict_export_sha256"]),
            "execution_source_base_commit": basis["execution_source_base_commit"]},
            "hidden_primary_height_previously_observed": False},
        "actual_original_runs": 1, "maximum_original_runs": 1, "semantic_acceptance": False,
        "hidden_height_policy_classification": None, "full_active_grid_verified": False,
        "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
        "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
        "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
        "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
        "capture": facts, "provenance": provenance,
        "limitations": [
            "The original fixed audit failed; that failure is reproduced and its missing mechanics file is not fabricated.",
            "Only S[:61]+S[91:] from the fixed 89-space-plus-uvw source is added, at document row11 in phases5/6; no target is rerun.",
            "The separate offline audit rechecks the frozen protocol, geometry, generated script, provider, complete original package, sources and profile.",
            "Document markers, DSR and visible rectangles remain separate observations; exposed text during alternate stages is not classified as the active alternate grid or private buffer storage.",
            "This one direct Telnet recording does not establish full grid, styles, local-shell transport, reliability repetition or performance acceptance.",
            "Only complete finite source-derived ASCII arrays are reconstructed publicly; unrestricted raw text, paths, process identifiers, profiles and original artifacts remain private.",
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
