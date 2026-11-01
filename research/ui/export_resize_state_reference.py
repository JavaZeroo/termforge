"""Publish bounded resize scalars after auditing a complete original recording.

The frozen mode exporter supplies the complete package/source provenance audit.
Resize has independent geometry, protocol, template and text guards. Document
text is validated against the synthetic source but is not copied to the export.
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
from research.ui import resize_state_reference as probe

require, read, sha, digest, integer = common.require, common.read, common.sha, common.digest, common.integer
MARKERS = ("ANCHOR", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "v", "w", "x")
RAW_NAMES = ("evidence.json", "run.json", "probe.js", "peer.json", "resize-state-result.json", "files.json",
             "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
             *(f"ack-{index}" for index in range(3)),
             *(f"resize-{kind}-{index}" for index in (1, 2) for kind in ("request", "ready")))
EXPORT_SOURCES = ("research/ui/export_resize_state_reference.py", "research/ui/export_mode_state_reference.py")

# Reuse the already exercised provenance function with three explicit adapter
# changes. Neither the frozen exporter nor any execution source is modified.
_source = Path(common.__file__).read_text(encoding="utf-8")
require(_source.count("def provenance_facts(") == _source.count("\n\ndef export(") == 1,
        "Protected provenance function boundary changed")
_source = "def provenance_facts(" + _source.split("def provenance_facts(", 1)[1].split("\n\ndef export(", 1)[0]
for _old, _new in {
    "One original launch attempt; retain after failure and never retry this plan.":
        "One original resize launch attempt; preserve failures and never retry this plan.",
    "probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)":
        "probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES)",
    'probe.launcher_source(plan["case_id"])': "probe.launcher_source()",
}.items():
    require(_source.count(_old) == 1, "Protected provenance adapter changed")
    _source = _source.replace(_old, _new)
_PROVENANCE = {**vars(common), "ROOT": ROOT, "probe": probe, "RAW_NAMES": RAW_NAMES}
exec(compile(_source, str(Path(common.__file__)), "exec"), _PROVENANCE)
provenance_facts = _PROVENANCE["provenance_facts"]
marker_projection = FunctionType(common.marker_projection.__code__, {**vars(common), "MARKERS": MARKERS})


def synthetic_lines(initial_columns):
    """Exact source-derived lines, including the two-byte soft-wrap suffix.

    CUP places v/w at column N. A document may retain w+x on one logical
    line of N+1 characters even while its visible rectangles span two rows.
    No other token, padding length or repeated suffix is publishable. This
    admits both split and retained source projections, not a resize policy.
    """
    integer(initial_columns, 160, 40)
    padding = " " * (initial_columns-1)
    return {"", "0123456789", "ANCHOR", padding+"v", padding+"w", "x", padding+"wx"}


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1, 1)
    integer(run["tid"], 2**32-1, 1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += (source_root / "research/ui/resize_state_reference.js").read_text(encoding="utf-8")
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


def geometry(value, expected):
    rect = value["window_rect"]
    require(type(rect) is list and len(rect) == 4, "Window rectangle shape differs")
    rect = [integer(item, 1000000, -1000000) for item in rect]
    require(probe.qrect_size(rect) == list(expected) == value["window_size"], "Window geometry differs")
    return {"window_rect": rect, "window_size": list(expected)}


def capture_facts(directory, plan, source_root=ROOT):
    mechanics = probe.audit_mechanics(directory)
    require(read(directory / "mechanics-check.json") == mechanics, "Saved mechanics audit differs")
    peer, result, run, outer = [read(directory / name) for name in
                              ("peer.json", "resize-state-result.json", "run.json", "evidence.json")]
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
    require(type(evidence["evidence_id"]) is str and re.fullmatch(r"ev_[0-9a-f]{64}", evidence["evidence_id"]),
            "Evidence identity differs")
    normalized = evidence["normalized_result"]
    require(normalized["messages_truncated"] is False and normalized["cleanup_error"] is None and
            normalized["source_sha256"] == run["script_sha256"] and normalized["mode"] == "attach" and
            normalized["source_kind"] == "file" and normalized["state"] == "running" and normalized["pid"] == run["pid"] and
            PureWindowsPath(normalized["source_path"]) == PureWindowsPath(plan["out"]) / "probe.js",
            "Provider source/lifecycle differs or is incomplete")
    scripts, messages = normalized["scripts"], normalized["messages"]
    require(type(scripts) is list and len(scripts) == 1 and type(messages) is list and 6 <= len(messages) <= 69,
            "Provider message count differs")
    require(all(row["type"] == "send" and row["script_id"] == scripts[0]["script_id"] and
                type(row["payload"]) is dict for row in messages), "Provider script error or unrelated message")
    payloads = [row["payload"] for row in messages]
    require(payloads[0] == {"kind": "probe_ready", "station_isolated": True, "scenario": "resize-state-v1"},
            "Probe ready identity differs")
    count = len(payloads)-5
    readiness = payloads[1:1+count]
    require(all(row.get("kind") == "readiness" and row["phase"] == "ui" and
                all(type(row[name]) is bool for name in ("ready", "has_tree", "has_engine", "has_editor", "peer_ready"))
                for row in readiness), "Readiness sequence differs")
    observations = payloads[1+count:-1]
    require(all(row.get("kind") == "resize_state_phase" for row in observations) and
            [{key: value for key, value in row.items() if key != "kind"} for row in observations] == result["observations"] and
            payloads[-1] == {"kind": "resize_state_result", **result}, "Provider and saved observations differ")
    grids, freezes = peer["frozen_grids"], peer["freeze_event_counts"]
    require(read(directory / "fixture-ready") == {"grid": grids[0], "event_count": freezes[0]},
            "Initial stable-grid acknowledgment differs")
    for phase in (1, 2):
        require(read(directory / f"resize-request-{phase}") == {"phase": phase, "window": list(probe.WINDOWS[phase])} and
                read(directory / f"resize-ready-{phase}") == {"phase": phase, "grid": grids[phase], "event_count": freezes[phase]},
                "Resize acknowledgment differs")
    exported = []
    for source, row, observed, grid, window in zip(probe.phase_records(grids[0][1], grids[0][0]), peer["records"],
                                                   result["observations"], grids, probe.WINDOWS):
        terminal = observed["terminal"]
        require(all(line in synthetic_lines(grids[0][0]) for line in terminal["lines"]),
                "Only fixed synthetic document text is publishable")
        caret = {name: integer(terminal["caret_api"][name], limit) for name, limit in
                 (("caretLine", 200), ("caretColumn", 500), ("caretPosition", 262144))}
        exported.append({**source, "cursor_reply": [integer(value) for value in row["cursor_reply"]],
                         "response_hex": row["response_hex"], "private_dsr_prefix": row["cursor_reply_private"],
                         "frozen_grid": list(grid), **geometry(observed, window),
                         "document_projection": {"line_count": integer(terminal["line_count"], 200),
                             "capacity": grid[1], "top": None if terminal["top"] is None else integer(terminal["top"], 200),
                             "scroll": integer(terminal["scroll"], 200), "caret_api": caret,
                             "markers": marker_projection(terminal)}})
    checks = [{"phase": index, "expected_size": list(window), **geometry(check, window)}
              for index, (check, window) in enumerate(zip(result["geometry_checks"], probe.WINDOWS))]
    cleanup = read(directory / "cleanup.json")
    return {"phases": exported, "source_bytes": sum(row["source_bytes"] for row in exported), "dsr_request_bytes": 12,
            "application_reply_bytes": len(bytes.fromhex(peer["received_hex"])),
            "window_rect_format": "qt-qrect-inclusive-ltrb", "geometry_checks": checks,
            "frozen_grids": grids, "naws_history": peer["naws_history"], "freeze_event_counts": freezes,
            "resize_boundaries": [{"phase": index, "before_event_count": integer(row["before_event_count"], 64),
                                   "after_event_count": integer(row["after_event_count"], 64),
                                   "requested_window": list(probe.WINDOWS[index])}
                                  for index, row in enumerate(peer["resize_boundaries"], 1)],
            "job_after": {name: integer(cleanup[name], 100) for name in ("active", "total", "terminated")},
            "provider": {"id": "frida", "version": None, "confidence": "observed", "evidence_id": evidence["evidence_id"],
                         "messages": len(messages), "readiness_messages": count, "phase_messages": 3,
                         "messages_truncated": False, "cleanup_error": None}}


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [path.resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    require(all(type(plan.get(key)) is type(value) and plan[key] == value for key, value in probe.fixed_contract().items()),
            "Fixed resize contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    facts = capture_facts(directory, plan, source_root)
    reference_source = Path(plan["source"]) if reference_source is None else reference_source
    provenance = provenance_facts(directory, plan_path, plan, source_root, reference_source.resolve())
    result = {"schema": "termforge-original-resize-state-reference-v1", "issue": 29,
              "authorized_plan_sha256": expected_plan_sha256,
              "offline_export_source_files_sha256": {name: sha(ROOT / name) for name in EXPORT_SOURCES},
              "status": "bounded-direct-protocol-and-geometry-observation", "actual_original_runs": 1, "maximum_original_runs": 1,
              "semantic_acceptance": False, "resize_policy_classification": None, "full_active_grid_verified": False,
              "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
              "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
              "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "capture": facts, "provenance": provenance, "limitations": [
                  "One fresh direct Telnet run changes public QWidget size 1360x749 -> 1000x749 -> 1360x749.",
                  "Only DSR is sent after initial text; later phases add no CUP, redraw or replacement markers.",
                  "Document markers are not a complete active grid; visible rectangles, retained text coordinates and DSR remain separate observations.",
                  "The exporter makes no reflow, clipping, cursor-recovery or policy acceptance decision.",
                  "No local-shell ConPTY, reliability repetition, performance or complete R2 acceptance follows.",
                  "Raw document text, paths, process identifiers, profiles, resources and provider diagnostics remain private."]}
    payload = (json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+"\n").encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as handle:
        handle.write(payload)
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
    print(json.dumps({"exported": True, "phases": len(result["capture"]["phases"]), "sha256": sha(args.destination)}))
