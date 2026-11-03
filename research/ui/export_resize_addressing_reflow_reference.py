"""Strict offline publication for one fixed post-reflow CUP action and its baseline.

The protected resize exporter supplies the package/source/protocol audit; this
module only changes the fixed action contract, the finite source-derived text
allowlist and the published schema. No target is executed here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import sys
from types import FunctionType

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import export_mode_state_reference as common
from research.ui import export_resize_state_reference as resize_export
from research.ui import resize_addressing_reflow_reference as probe

require, sha, digest, integer = common.require, common.sha, common.digest, common.integer
MARKERS = (*resize_export.MARKERS, "Q")
RAW_NAMES = ("evidence.json", "run.json", "probe.js", "peer.json", probe.RESULT_FILE, "files.json",
             "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
             *(f"ack-{n}" for n in range(4)), *(f"resize-{kind}-{n}" for n in (1, 3) for kind in ("request", "ready")))
EXPORT_SOURCES = ("research/ui/export_resize_addressing_reflow_reference.py",
                  "research/ui/export_resize_state_reference.py",
                  "research/ui/export_mode_state_reference.py")


def read(path):
    require(path.stat().st_size <= 4*1024*1024, "Recording exceeds byte bound")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate recording field")
            result[key] = value
        return result
    def nonfinite(_):
        raise ValueError("Nonfinite JSON value")
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique, parse_constant=nonfinite)


def extract(path, start, end):
    source = path.read_text(encoding="utf-8")
    require(source.count(start) == source.count(end) == 1, "Protected offline audit function boundary changed")
    return start+source.split(start, 1)[1].split(end, 1)[0]


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1, 1); integer(run["tid"], 2**32-1, 1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    driver = (source_root / probe.CONTROLLER).read_text(encoding="utf-8")
    require(driver.count(probe.CONTROLLER_ANCHOR) == 1, "Case template anchor differs")
    template += driver.replace(probe.CONTROLLER_ANCHOR, json.dumps(probe.validate_case(plan["case_id"])))
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


# The immutable existing helpers are adapted in fresh namespaces. Each textual
# boundary is checked, and all three offline source files are identified below.
_source = extract(Path(common.__file__), "def provenance_facts(", "\n\ndef export(")
_source = _source.replace("One original launch attempt; retain after failure and never retry this plan.",
                          probe.ATTEMPT.rstrip("\n"))
require(_source.count("probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)") == 1,
        "Protected offline audit adapter changed")
_source = _source.replace("probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)",
                          "probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES)")
_PROVENANCE = {**vars(common), "ROOT": ROOT, "probe": probe, "RAW_NAMES": RAW_NAMES, "read": read}
exec(compile(_source, str(Path(common.__file__)), "exec"), _PROVENANCE)
provenance_facts = _PROVENANCE["provenance_facts"]
marker_projection = FunctionType(common.marker_projection.__code__, {**vars(common), "MARKERS": MARKERS})
geometry = FunctionType(resize_export.geometry.__code__, {**vars(resize_export), "probe": probe})
_source = extract(Path(resize_export.__file__), "def capture_facts(", "\n\ndef export(")
for _old, _new in {
    "probe.audit_mechanics(directory)": 'probe.audit_mechanics(directory, plan["case_id"])',
    '"resize-state-result.json"': "probe.RESULT_FILE",
    "6 <= len(messages) <= 69": "7 <= len(messages) <= 70",
    '"scenario": "resize-state-v1"': '"scenario": "resize-address-v1", "case_id": plan["case_id"]',
    "count = len(payloads)-5": "count = len(payloads)-6",
    '"resize_state_phase"': '"resize_address_phase"',
    '"resize_state_result"': '"resize_address_result"',
    "for phase in (1, 2):": "for phase in (1, 3):",
    'probe.phase_records(grids[0][1], grids[0][0])': 'probe.phase_records(plan["case_id"], grids[0][1], grids[0][0])',
    "synthetic_lines(grids[0][0])": 'probe.synthetic_lines(plan["case_id"], source["phase"])',
    '"dsr_request_bytes": 12': '"dsr_request_bytes": 16',
    'enumerate(peer["resize_boundaries"], 1)': 'zip(probe.RESIZE_PHASES, peer["resize_boundaries"])',
    '"phase_messages": 3': '"phase_messages": 4',
}.items():
    require(_source.count(_old) == 1, "Protected offline audit adapter changed: "+_old)
    _source = _source.replace(_old, _new)
_CAPTURE = {**vars(resize_export), "ROOT": ROOT, "probe": probe, "read": read,
            "expected_probe": expected_probe, "geometry": geometry, "marker_projection": marker_projection}
exec(compile(_source, str(Path(resize_export.__file__)), "exec"), _CAPTURE)


def capture_facts(directory, plan, source_root=ROOT):
    result = _CAPTURE["capture_facts"](directory, plan, source_root)
    raw = read(directory / probe.RESULT_FILE)
    for index, (phase, observed) in enumerate(zip(result["phases"], raw["observations"])):
        phase["stage_role"] = ("initial-baseline", "shrunk-before-action", "shrunk-after-action", "restored-after-action")[index]
        lines = observed["terminal"]["lines"]
        phase["document_projection"]["synthetic_lines_sha256"] = hashlib.sha256(json.dumps(lines, separators=(",", ":")).encode()).hexdigest()
    result["pre_action_observation_count"] = 2
    result["declared_action_hex"] = probe.CASES[plan["case_id"]].hex()
    return result


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in probe.fixed_contract(plan.get("case_id")).items()),
            "Fixed reflow-addressing contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    facts = capture_facts(directory, plan, source_root)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_facts(directory, plan_path, plan, source_root, reference_source.resolve())
    result = {"schema": "termforge-original-resize-reflow-addressing-reference-v1", "issue": 29, "related_issue": 27,
              "case_id": plan["case_id"], "authorized_plan_sha256": expected_plan_sha256,
              "offline_export_source_files_sha256": {name: sha(ROOT / name) for name in EXPORT_SOURCES},
              "status": "bounded-direct-protocol-and-addressing-observation", "actual_original_runs": 1, "maximum_original_runs": 1,
              "semantic_acceptance": False, "addressing_policy_classification": None, "full_active_grid_verified": False,
              "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
              "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
              "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "capture": facts, "provenance": provenance, "limitations": [
                  "One fresh direct Telnet process executes one CUP+Q action after the shrunk baseline has been observed and acknowledged.",
                  "The initial body contains a 123-character soft-wrapped line before the target rows, so narrowing shifts later display rows while retained source-fragment rows keep their numbering.",
                  "Four stages preserve both pre-action observations; only phases 1 and 3 resize, while phase 2 sends the exact declared CUP byte sequence.",
                  "Document markers, protocol DSR, caret API and visible rectangles remain different observations; no internal storage or addressing policy is inferred.",
                  "The finite text allowlist admits source-derived one-character writes without predicting the actually affected row.",
                  "Only 91x20 -> 61x20 -> 91x20 with fixed ASCII is covered; no full grid, styles, local-shell transport, alternate screen, height change, reliability repetition or performance acceptance.",
                  "Raw document text, paths, process identifiers, profiles, resources and provider diagnostics remain private."]}
    payload = (json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+"\n").encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(payload)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "plan", "destination"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--reference-source", type=Path)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination, args.expected_plan_sha256, args.source_root, args.reference_source)
    print(json.dumps({"exported": True, "case_id": result["case_id"], "phases": len(result["capture"]["phases"]), "sha256": sha(args.destination)}))
