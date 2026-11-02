"""Publish bounded geometry observations after verifying one immutable recording."""
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
from research.ui import resize_geometry_reference as probe

require, sha, digest, integer = common.require, common.sha, common.digest, common.integer
EXPORT_SOURCES = ("research/ui/export_resize_geometry_reference.py", "research/ui/export_mode_state_reference.py")
RAW_NAMES = ("evidence.json", "run.json", "probe.js", "peer.json", probe.RESULT_FILE, "files.json",
             "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
             "ack-0", "ack-1", "resize-request-1", "geometry-ready-1")


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


def fields(value, names):
    require(type(value) is dict and set(value) == set(names.split()), "Unexpected geometry fields")


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1, 1); integer(run["tid"], 2**32-1, 1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    driver = (source_root / "research/ui/resize_geometry_reference.js").read_text(encoding="utf-8")
    require(driver.count("__RESIZE_GEOMETRY_CASE_JSON__") == 1, "Case template anchor differs")
    template += driver.replace("__RESIZE_GEOMETRY_CASE_JSON__", json.dumps(probe.controller_contract(plan["case_id"])))
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


# Retain the established complete-package and source-archive checks in an
# isolated namespace; only this protocol's fixed attempt text and source API differ.
_source = Path(common.__file__).read_text(encoding="utf-8")
_start, _end = "def provenance_facts(", "\n\ndef export("
require(_source.count(_start) == _source.count(_end) == 1, "Provenance function boundary changed")
_source = _start+_source.split(_start, 1)[1].split(_end, 1)[0]
for _old, _new in {
    "One original launch attempt; retain after failure and never retry this plan.": probe.ATTEMPT.rstrip("\n"),
    "probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)":
        "probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES)",
}.items():
    require(_source.count(_old) == 1, "Protected provenance adapter changed")
    _source = _source.replace(_old, _new)
_PROVENANCE = {**vars(common), "ROOT": ROOT, "probe": probe, "read": read, "RAW_NAMES": RAW_NAMES}
exec(compile(_source, str(Path(common.__file__)), "exec"), _PROVENANCE)


def provenance_facts(directory, plan_path, plan, source_root, reference_source):
    function = FunctionType(_PROVENANCE["provenance_facts"].__code__, _PROVENANCE)
    return function(directory, plan_path, plan, source_root, reference_source)


def public_snapshot(value):
    """Rebuild every nested field; never forward captured objects or free text."""
    fields(value, "sequence phase reason elapsed_ms requested_window window peer terminal widgets")
    probe.validate_snapshot(value, integer(value["sequence"], 95))
    window, peer, terminal, widgets = [value[key] for key in ("window", "peer", "terminal", "widgets")]
    fields(window, "rect size error"); fields(peer, "grid event_count resize_armed error")
    fields(terminal, "connected protocol capacity line_count error")
    fields(widgets, "status error nodes roles"); fields(widgets["roles"], "terminal editor viewport")
    nodes = []
    for node in widgets["nodes"]:
        fields(node, "id parent_id role rect size global_origin is_splitter is_handle visible")
        nodes.append({"id": integer(node["id"], 63),
                      "parent_id": None if node["parent_id"] is None else integer(node["parent_id"], 63),
                      "role": node["role"], "rect": list(node["rect"]), "size": list(node["size"]),
                      "global_origin": list(node["global_origin"]), "is_splitter": node["is_splitter"],
                      "is_handle": node["is_handle"], "visible": node["visible"]})
    return {"sequence": value["sequence"], "phase": value["phase"], "reason": value["reason"],
            "elapsed_ms": value["elapsed_ms"], "requested_window": list(value["requested_window"]),
            "window": {"rect": None if window["rect"] is None else list(window["rect"]),
                       "size": None if window["size"] is None else list(window["size"]), "error": window["error"]},
            "peer": {"grid": None if peer["grid"] is None else list(peer["grid"]),
                     "event_count": peer["event_count"], "resize_armed": peer["resize_armed"], "error": peer["error"]},
            "terminal": {"connected": terminal["connected"], "protocol": terminal["protocol"],
                         "capacity": terminal["capacity"], "line_count": terminal["line_count"], "error": terminal["error"]},
            "widgets": {"status": widgets["status"], "error": widgets["error"], "nodes": nodes,
                        "roles": {role: None if widgets["roles"][role] is None else integer(widgets["roles"][role], 63)
                                  for role in ("terminal", "editor", "viewport")}}}


def capture_facts(directory, plan, source_root=ROOT):
    case = probe.validate_case(plan["case_id"])
    # Read bounded JSON with duplicate/nonfinite rejection before the runtime
    # mechanical audit. The retained runtime itself remains unchanged.
    parsed = {name: read(directory / name) for name in RAW_NAMES if name.endswith(".json") and name != "files.json"}
    for name in ("fixture-ready", "resize-request-1", "geometry-ready-1"):
        read(directory / name)
    peer, result, run, outer = [parsed[name] for name in ("peer.json", probe.RESULT_FILE, "run.json", "evidence.json")]
    fields(result, "schema status error case_id semantic_acceptance requested_windows terminal_input_bytes observations snapshots widget_geometry_complete")
    require(type(result["terminal_input_bytes"]) is int and result["terminal_input_bytes"] == 0, "Terminal input differs")
    mechanics = probe.audit_mechanics(directory, case)
    require(parsed["mechanics-check.json"] == mechanics, "Saved mechanics audit differs")
    snapshots = [public_snapshot(value) for value in result["snapshots"]]
    observations = [public_snapshot(value) for value in result["observations"]]
    require(run["sha256"] == probe.EXE_SHA256 and run["files_sha256"] == probe.MANIFEST_SHA256, "Original identity differs")
    require(run["scenario"] == "menu-wrap" and run["profile"] == "synthetic-local-only" and
            run["clipboard"] == "owned-window-station", "Launch scope differs")
    require((directory / "probe.js").read_bytes() == expected_probe(plan, run, source_root) and
            sha(directory / "probe.js") == run["script_sha256"], "Runtime probe bytes differ")
    require(outer["ok"] is True and outer["data"]["cleanup_error"] is None, "REA outer result failed")
    evidence = outer["data"]["evidence"]
    require(evidence["provider"] == {"id": "frida", "name": "Frida", "version": None} and
            evidence["confidence"] == "observed" and evidence["operation"] == "instrument_with_frida" and
            evidence["subject"] is None and evidence["environment"] is None and evidence["raw_result"] is None,
            "Provider identity differs")
    require(type(evidence["evidence_id"]) is str and re.fullmatch(r"ev_[0-9a-f]{64}", evidence["evidence_id"]), "Evidence identity differs")
    normalized = evidence["normalized_result"]
    require(normalized["messages_truncated"] is False and normalized["cleanup_error"] is None and
            normalized["source_sha256"] == run["script_sha256"] and normalized["mode"] == "attach" and
            normalized["source_kind"] == "file" and normalized["state"] == "running" and
            type(normalized["pid"]) is int and normalized["pid"] == run["pid"] and
            PureWindowsPath(normalized["source_path"]) == PureWindowsPath(plan["out"]) / "probe.js",
            "Provider source/lifecycle differs or is incomplete")
    scripts, messages = normalized["scripts"], normalized["messages"]
    require(type(scripts) is list and len(scripts) == 1 and type(messages) is list and
            len(snapshots)+3 <= len(messages) <= len(snapshots)+66, "Provider message count differs")
    require(all(row["type"] == "send" and row["script_id"] == scripts[0]["script_id"] and
                type(row["payload"]) is dict for row in messages), "Provider script error or unrelated message")
    payloads = [row["payload"] for row in messages]
    require(payloads[0] == {"kind": "probe_ready", "station_isolated": True, "scenario": "resize-geometry-v1", "case_id": case},
            "Probe ready identity differs")
    count = len(payloads)-len(snapshots)-2
    readiness = payloads[1:1+count]
    require(all(row.get("kind") == "readiness" and row["phase"] == "ui" and
                all(type(row[name]) is bool for name in ("ready", "has_tree", "has_engine", "has_editor", "peer_ready"))
                for row in readiness), "Readiness sequence differs")
    require(payloads[1+count:-1] == [{"kind": "resize_geometry_snapshot", **value} for value in result["snapshots"]] and
            payloads[-1] == {"kind": "resize_geometry_result", **result}, "Provider and saved observations differ")
    require(type(peer["phase"]) is int and peer["phase"] == 1, "Peer phase differs")
    cleanup = parsed["cleanup.json"]
    return {"observations": observations, "snapshots": snapshots, "stateful_observation_count": 2,
            "snapshot_count": len(snapshots), "requested_windows": [list(value) for value in probe.WINDOWS],
            "window_rect_format": "qt-qrect-inclusive-ltrb", "widget_rect_format": "qt-qrect-inclusive-ltrb",
            "widget_geometry_complete": result["widget_geometry_complete"],
            "terminal_input_bytes": 0, "DSR_requests": 0, "dsr_request_bytes": 0, "application_reply_bytes": 0,
            "application_input_sha256": hashlib.sha256(b"").hexdigest(),
            "naws_history": [[integer(v[0], 91, 91), integer(v[1], 200, 1)] for v in peer["naws_history"]],
            "freeze_event_count": integer(peer["freeze_event_count"], 64, 1),
            "resize_boundary": {"before_event_count": integer(peer["before_event_count"], 64, 1),
                                "after_event_count": integer(peer["after_event_count"], 64, 1)},
            "job_after": {name: integer(cleanup[name], 100) for name in ("active", "total", "terminated")},
            "provider": {"id": "frida", "version": None, "confidence": "observed", "evidence_id": evidence["evidence_id"],
                         "messages": len(messages), "readiness_messages": count, "snapshot_messages": len(snapshots),
                         "messages_truncated": False, "cleanup_error": None}}


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    require(all(json.dumps(plan.get(k), sort_keys=True) == json.dumps(v, sort_keys=True)
                for k, v in probe.fixed_contract(plan.get("case_id")).items()), "Fixed geometry contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    facts = capture_facts(directory, plan, source_root)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_facts(directory, plan_path, plan, source_root, reference_source.resolve())
    result = {"schema": "termforge-original-resize-geometry-reference-v1", "issue": 29, "related_issue": 27,
              "case_id": plan["case_id"], "authorized_plan_sha256": expected_plan_sha256,
              "offline_export_source_files_sha256": {name: sha(ROOT / name) for name in EXPORT_SOURCES},
              "status": "observation", "observation_kind": "bounded-public-window-widget-and-naws-geometry",
              "actual_original_runs": 1, "maximum_original_runs": 1, "semantic_acceptance": False,
              "height_policy_classification": None, "full_active_grid_verified": False,
              "terminal_styles_verified": False, "local_shell_transport_verified": False,
              "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
              "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "capture": facts, "provenance": provenance, "limitations": [
                  "One fresh synthetic loopback Telnet session requests 1360x749 then 1360x569; actual final geometry and NAWS are observations, not predicted values or acceptance.",
                  "Terminal application input, DSR requests and application replies are all zero; Telnet negotiation and NAWS are recorded independently.",
                  "Widget geometry is optional. Unavailable widget observations preserve the independent window, NAWS and capacity observations.",
                  "Widget IDs are recording-local ordinals, not pointers; rectangles are inclusive Qt coordinates and global origins are pixel coordinates.",
                  "No document body, caret, full active grid, terminal editing, resize policy, ConPTY, performance or complete R2 acceptance is established.",
                  "Private paths, process identifiers, profile content, raw resources and unrestricted provider diagnostics are not published."]}
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
    print(json.dumps({"exported": True, "case_id": result["case_id"], "observations": 2, "sha256": sha(args.destination)}))
