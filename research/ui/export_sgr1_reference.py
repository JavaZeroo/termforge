"""Validate one fixed SGR1 capture and publish only reviewed synthetic scalars/hashes."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.sgr1_reference import CASE_ID, MODULE_SHA256, MANIFEST_SHA256, SOURCE_FILES, AUDIT_FILES, TARGETS, phase_records, build_probe_fragment
from research.ui.sgr1_analysis import audit_capture, frame_comparisons, require
from research.ui.export_raster_raw_reference import REASONS

TOKENS = sorted({row["label"] for target in TARGETS for row in target["markers"]})
SYNTHETIC_LINE = re.compile(r" *(?:(?:" + "|".join(re.escape(token) for token in TOKENS) + r") *)*")
COMPLETE, PARTIAL = "captured-five-sgr1-raster-phases", "partial-sgr1-raster-capture"


def export(directory, plan_path, destination):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    require(plan["issue"] == 146 and plan["case_id"] == CASE_ID and plan["execution_authorized"] is True and plan["maximum_original_runs"] == 1, "Plan scope or execution authorization differs")
    require((plan["phase_count"], plan["maximum_grabs_per_phase"], plan["maximum_grabs"], plan["rea_capture_seconds"], plan["cli_wait_timeout_seconds"], plan["whole_lifecycle_bound_seconds"]) == (5, 1, 5, 45, 60, None), "Plan capture/lifecycle bounds differ")
    require(re.fullmatch(r"[0-9a-f]{40}", plan["source_base_commit"]) is not None, "Plan source identity missing")
    require(plan["module_sha256"] == MODULE_SHA256 and plan["files_manifest_sha256"] == MANIFEST_SHA256 and plan["phases"] == phase_records(), "Plan fixed identities or inputs differ")
    tool = plan["rea_prepared_identity"]
    require(set(tool) == {"version", "shim_sha256", "package_json_sha256", "entry_sha256"} and tool["version"] == "6.7.0" and all(re.fullmatch(r"[0-9a-f]{64}", tool[key]) for key in ("shim_sha256", "package_json_sha256", "entry_sha256")), "Prepared REA identity differs")
    for key, names in (("source_files_sha256", SOURCE_FILES), ("offline_audit_files_sha256", AUDIT_FILES)):
        require(set(plan[key]) == set(names) and plan[key] == {name: sha(ROOT / name) for name in names}, "Source inventory or bytes differ")
    generated = hashlib.sha256(build_probe_fragment().encode("utf-8")).hexdigest()
    require(plan["generated_capture_sha256"] == generated, "Generated capture differs")
    run, peer, captured, outer = [read(name) for name in ("run.json", "peer.json", "sgr1-raster-result.json", "evidence.json")]
    evidence = outer["data"]["evidence"]; normalized = evidence["normalized_result"]
    require(run["sha256"] == MODULE_SHA256["WindTerm.exe"] and run["files_sha256"] == MANIFEST_SHA256, "Observed package identity differs")
    template = (ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0] + build_probe_fragment()
    require(run["template_sha256"] == hashlib.sha256(template.encode("utf-8")).hexdigest(), "Executed capture template differs")
    require(normalized["source_sha256"] == run["script_sha256"] == sha(directory / "probe.js"), "Injected source differs")
    require(outer["ok"] and not outer["data"]["cleanup_error"] and not normalized["cleanup_error"] and not normalized["messages_truncated"], "Provider/cleanup incomplete")
    require(not any(row["type"] == "error" for row in normalized["messages"]), "Provider script error")
    require(evidence["provider"] == {"id": "frida", "name": "Frida", "version": None} and evidence["confidence"] == "observed" and evidence["subject"] is None and evidence["environment"] is None, "Provider identity differs")
    require(re.fullmatch(r"ev_[0-9a-f]{64}", evidence["evidence_id"]) is not None, "Provider evidence identity missing")
    require(all(value in {"Artifact identity is unavailable for this observation.", "The target operating system and architecture were not reported by this observation."} for value in evidence["limitations"]), "Unexpected provider limitation text")
    messages = [row["payload"] for row in normalized["messages"] if row["type"] == "send" and isinstance(row.get("payload"), dict)]
    observed = [{key: value for key, value in row.items() if key != "kind"} for row in messages if row.get("kind") == "sgr1_raster_phase"]
    finals = [{key: value for key, value in row.items() if key != "kind"} for row in messages if row.get("kind") == "sgr1_raster_result"]
    require(observed == captured["captures"] and finals == [captured], "Provider messages and raw file disagree")
    require(captured["status"] in (COMPLETE, PARTIAL) and captured["reason"] in REASONS, "Capture status/reason differs")
    guards = captured["module_guards"]
    require(len(guards) == 4 and {row["name"] for row in guards} == set(MODULE_SHA256) and all(row == {"name": row["name"], "path_guard_passed": True} for row in guards), "Module path guard failed")
    count = len(observed); require(0 <= count <= 5, "Too many phases")
    grid, freeze, history = peer.get("frozen_grid"), peer.get("freeze_event_count"), peer.get("naws_history", [])
    require(len(history) <= 64 and all(isinstance(row, list) and len(row) == 2 and all(type(n) is int and 0 < n <= 65535 for n in row) for row in history), "NAWS shape differs")
    if grid is None:
        require(count == 0 and captured["status"] == PARTIAL and freeze is None, "Missing grid after source release")
    else:
        require(grid == [peer["columns"], peer["rows"]] and 40 <= peer["columns"] <= 160 and 10 <= peer["rows"] <= 60, "Frozen grid differs")
        require(type(freeze) is int and 0 <= freeze <= len(history), "NAWS event bound differs")
        require(all(row == grid for row in history[freeze:]), "NAWS changed after freeze")
    wire = bytes.fromhex(peer["received_hex"])
    replies = list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R", wire))
    require(len(wire) <= 65536 and not re.sub(rb"\x1b\[(\??)(\d+);(\d+)R", b"", wire), "Unexpected application bytes")
    require(len(peer["records"]) == len(replies) == count, "Source/reply/capture counts differ")
    require(captured["grab_phases"] == [row["phase"] for row in observed if row["grab_attempted"]] and len(set(captured["grab_phases"])) == len(captured["grab_phases"]), "Extra grab")
    public_phases = []
    for index, (capture, record, reply) in enumerate(zip(observed, peer["records"], replies)):
        expected = phase_records(peer["rows"], peer["columns"])[index]
        require(capture["phase"] == record["phase"] == index and capture["label"] == TARGETS[index]["label"], "Phase identity differs")
        require(all(record[key] == expected[key] for key in ("label", "source_hex", "source_sha256")), "Exact source differs")
        cursor = [int(reply[2]), int(reply[3])]
        require(reply[1] == b"" and cursor == record["cursor_reply"] == capture["cursor_reply"], "DSR differs")
        require(capture["reason"] in REASONS and capture["module_guards"] == guards, "Capture reason/module differs")
        terminal = capture["terminal"]
        require(terminal["protocol"] == "Telnet" and terminal["connected"] is True and terminal["capacity"] == peer["rows"], "Terminal binding differs")
        require(terminal["line_count"] == len(terminal["lines"]) <= 200 and all(SYNTHETIC_LINE.fullmatch(line) for line in terminal["lines"]), "Non-synthetic terminal text")
        require(type(terminal["is_alt"]) is bool and type(terminal["is_main"]) is bool and terminal["is_alt"] != terminal["is_main"], "Mode observation missing")
        require(terminal["is_alt"] == (index in (1, 2, 3)), "Unexpected active buffer for fixed source")
        value = audit_capture(capture)
        value.update({"source_label": record["label"], "source_hex": record["source_hex"], "source_sha256": record["source_sha256"],
                      "dsr_request_hex": "1b5b366e", "cursor_reply_hex": reply[0].hex(), "is_alt": terminal["is_alt"], "is_main": terminal["is_main"]})
        public_phases.append(value)
        ack = directory / f"ack-{index}"
        if capture["capture_valid"]:
            require(ack.read_text() == "captured", "Capture acknowledgment missing")
        else:
            require(index == count-1 and not ack.exists(), "Invalid capture advanced source")
    complete = captured["status"] == COMPLETE
    require(not complete or (count == 5 and peer["complete"] is True and all(row["capture_valid"] for row in observed)), "Incomplete capture promoted")
    require(complete or captured["reason"] is not None, "Partial capture has no reason")
    cleanup, profile = read("cleanup.json"), read("profile-check.json")
    require(set(cleanup) == {"active", "total", "terminated"} and all(type(n) is int and n >= 0 for n in cleanup.values()) and cleanup["active"] == 0, "Owned Job did not finish")
    require(profile == {"only_synthetic_session": True, "session_count": 1}, "Synthetic profile failed")
    require((directory / "stderr.txt").stat().st_size == 0, "Unexpected original stderr")
    raw_names = ("run.json", "probe.js", "peer.json", "sgr1-raster-result.json", "evidence.json", "cleanup.json", "profile-check.json", "stderr.txt")
    summary = {"schema": "termforge-original-sgr1-reference-v1", "issue": 146, "case_id": CASE_ID,
               "status": captured["status"], "reason": captured["reason"], "raw_capture_complete": complete,
               "source_base_commit": plan["source_base_commit"], "source_files_sha256": plan["source_files_sha256"],
               "offline_audit_files_sha256": plan["offline_audit_files_sha256"], "generated_capture_sha256": generated,
               "rea_prepared_identity": tool,
               "plan_sha256": sha(plan_path), "module_sha256": MODULE_SHA256, "files_manifest_sha256": MANIFEST_SHA256,
               "maximum_original_runs": 1, "actual_original_runs": 1, "rea_capture_seconds": 45,
               "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
               "frozen_grid": grid, "naws_history": history, "freeze_event_count": freeze,
               "planned_phase_count": 5, "observed_phase_count": count, "grab_phases": captured["grab_phases"],
               "phases": public_phases, "same_frame_signature_comparisons": frame_comparisons(public_phases),
               "runtime_module_path_guards": guards,
               "provider": {key: evidence[key] for key in ("evidence_id", "provider", "confidence", "subject", "environment", "limitations")},
               "lifecycle": {"outer_job": cleanup, "profile": profile, "deliberate_original_termination": True, "natural_gui_exit_verified": False},
               "raw_files": {name: {"bytes": (directory/name).stat().st_size, "sha256": sha(directory/name)} for name in raw_names},
               "internal_bold_verified": False, "full_style_verified": False, "full_grid_verified": False,
               "local_shell_transport_verified": False, "presented_done": None, "r2_complete": False, "windterm_parity": False,
               "limitations": ["Only exact same-frame SGR0/SGR1 output signatures are compared; this does not read an internal pen or prove general font weight.",
                               "Each group must repeat exactly with equal ROI shapes/counts. Indistinguishable or inconsistent controls stay unknown. Color differences/ties do not invalidate an exact output signature but never establish a font-weight mechanism.",
                               "Only the five fixed sources and one grab per phase are covered; no ROI movement, extra grab or retry.",
                               "QWidget raster and direct Telnet do not prove physical presentation, local-shell transport or performance.",
                               "Capture45/CLI60 exclude startup and later cleanup; no whole-lifecycle deadline is claimed.",
                               "Raw pixels, paths, PIDs, copied original resources and synthetic profile stay local. Unknowns do not establish a reset or inherited internal style."]}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("directory", "plan", "destination"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination)
    print(json.dumps({"exported": True, "status": result["status"], "observed_phases": result["observed_phase_count"]}))
