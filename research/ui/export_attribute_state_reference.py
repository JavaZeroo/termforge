"""Audit one frozen attribute recording; export synthetic scalars, never pixels.

The plan SHA must have been retained before execution. A transferred recording
can use verified source/package copies; injected paths always come from the plan.
This offline exporter is separately identified, not retroactively added to the
original execution plan's frozen source inventory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import attribute_state_reference as probe
from research.ui.export_raster_raw_reference import REASONS

FILE_COUNT = 4047
MODULE_SHA256 = probe.MODULE_SHA256
MANIFEST_SHA256 = probe.MANIFEST_SHA256
ATTEMPT = "One authorized original launch attempt; never retry this plan.\n"
RAW_NAMES = ("run.json", "probe.js", "peer.json", probe.RESULT_FILE, "evidence.json",
             "files.json", "cleanup.json", "profile-check.json", "stderr.txt")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    require(path.stat().st_size <= 32 * 1024 * 1024, "Recording file exceeds bound")
    def unique(pairs):
        value = {}
        for key, item in pairs:
            require(key not in value, "Duplicate recording field")
            value[key] = item
        return value
    def invalid_constant(_):
        raise ValueError("Nonfinite JSON value")
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique, parse_constant=invalid_constant)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def digest(value):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value), "Invalid identity hash")
    return value


def integer(value, maximum=65535, minimum=0):
    require(type(value) is int and minimum <= value <= maximum, "Unbounded or noninteger observation")
    return value


def identity(path):
    return {"bytes": path.stat().st_size, "sha256": sha(path)}


def validate_plan(plan):
    attribute = plan.get("attribute")
    require(plan.get("schema") == "termforge-attribute-state-plan-v1" and plan.get("issue") == 27 and
            plan.get("case_id") == probe.case_id(attribute) and plan.get("requested_sgr") == probe.attribute_code(attribute),
            "Fixed attribute plan differs")
    require(plan.get("execution_authorized") is True and plan.get("maximum_original_runs") == 1 and
            plan.get("actual_original_runs") == 0, "Single execution authorization differs")
    for name in ("maximum_original_runs", "actual_original_runs", "phase_count", "maximum_grabs_per_phase",
                 "maximum_grabs", "rea_capture_seconds", "cli_wait_timeout_seconds"):
        integer(plan.get(name), 60)
    require(tuple(plan.get(key) for key in ("phase_count", "maximum_grabs_per_phase", "maximum_grabs",
            "rea_capture_seconds", "cli_wait_timeout_seconds", "whole_lifecycle_bound_seconds")) == (5, 1, 5, 45, 60, None),
            "Plan capture/lifecycle bounds differ")
    require(plan["phases"] == probe.phase_records(attribute) and plan["classification"] == probe.classification_contract(attribute),
            "Fixed source or classification differs")
    require(plan["module_sha256"] == MODULE_SHA256 and plan["files_manifest_sha256"] == MANIFEST_SHA256,
            "Plan package identity differs")
    require(plan["generated_capture_sha256"] == hashlib.sha256(probe.build_probe_fragment(attribute).encode()).hexdigest(),
            "Generated capture differs")
    tool = plan["rea_prepared_identity"]
    require(set(tool) == {"version", "shim_sha256", "package_json_sha256", "entry_sha256"} and tool["version"] == "6.7.0",
            "Prepared provider identity differs")
    for name in ("shim_sha256", "package_json_sha256", "entry_sha256"):
        digest(tool[name])


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1, 1)
    integer(run["tid"], 2**32-1, 1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    base = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template = base + probe.build_probe_fragment(plan["attribute"])
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


def provenance(directory, plan_path, plan, source_root, reference_source):
    require(plan_path.with_suffix(".attempted").read_text() == ATTEMPT, "Single-attempt marker differs")
    require(type(plan["source_base_commit"]) is str and re.fullmatch(r"[0-9a-f]{40}", plan["source_base_commit"]),
            "Execution base commit differs")
    hashes = {name: sha(source_root / name) for name in probe.SOURCE_FILES}
    require(hashes == plan["source_files_sha256"] == {name: sha(ROOT / name) for name in probe.SOURCE_FILES},
            "Execution source inventory/bytes differ")
    require(plan["offline_audit_files_sha256"] == {name: hashes[name] for name in probe.AUDIT_FILES},
            "Frozen audit inventory/bytes differ")
    recorded = plan["source_identity"]
    if recorded.get("kind") == "source-archive":
        actual = probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)
        require(all(plan.get(key) == value for key, value in actual.items()), "Source archive identity differs")
        source_identity = {"kind": "source-archive", "manifest_sha256": digest(recorded["manifest_sha256"])}
    else:
        require(recorded == {"kind": "git-checkout"}, "Source identity kind differs")
        source_identity = {"kind": "git-checkout"}
    files = read(directory / "files.json")
    require(type(files) is dict and len(files) == FILE_COUNT and
            hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest() == MANIFEST_SHA256,
            "Complete distribution manifest differs")
    for name, expected in files.items():
        relative = PurePosixPath(name)
        require(type(name) is str and name and str(relative) == name and not relative.is_absolute() and
                ".." not in relative.parts and "\\" not in name and ":" not in name, "Unsafe distribution path")
        digest(expected)
        for base in (directory / "app", reference_source):
            path = base / name
            require(not path.is_symlink() and path.resolve().is_relative_to(base.resolve()) and sha(path) == expected,
                    "Original distribution file changed")
    require(all(files.get(name) == expected for name, expected in MODULE_SHA256.items()), "Pinned modules differ")
    require(read(directory / "run.json")["fixture_sha256"] == hashes["research/ui/r1_vt_fixture.py"], "Fixture metadata differs")
    sessions = read(directory / "profile/.wind/profiles/default.v10/terminal/user.sessions")
    expected_session = {"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK", "session.protocol": "Telnet",
                        "session.target": "127.0.0.1", "session.uuid": "00000000-0000-4000-8000-000000000001"}
    require(type(sessions) is list and len(sessions) == 1 and set(sessions[0]) == {*expected_session, "session.port"} and
            all(sessions[0].get(key) == value for key, value in expected_session.items()), "Actual profile scope differs")
    integer(sessions[0]["session.port"], 65535, 1)
    require(read(directory / "profile-check.json") == {"only_synthetic_session": True, "session_count": 1}, "Profile audit differs")
    cleanup = read(directory / "cleanup.json")
    require(set(cleanup) == {"active", "total", "terminated"} and cleanup["active"] == 0, "Owned Job did not finish")
    cleanup = {key: integer(value, 100) for key, value in cleanup.items()}
    require((directory / "stderr.txt").stat().st_size == 0 and not (directory / "launcher-failure.json").exists(),
            "Original launcher failed or emitted stderr")
    return {"source_identity": source_identity, "source_base_commit": plan["source_base_commit"],
            "source_files_sha256": hashes, "offline_audit_files_sha256": plan["offline_audit_files_sha256"],
            "generated_capture_sha256": plan["generated_capture_sha256"], "rea_prepared_identity": plan["rea_prepared_identity"],
            "runtime_identity_limit": "Prepared REA identity was checked by the frozen launcher; no OS/Python/Node binary identity was recorded in this plan.",
            "module_sha256": MODULE_SHA256, "files_manifest_sha256": MANIFEST_SHA256, "files_verified": FILE_COUNT,
            "authorized_plan": identity(plan_path), "raw_files": {name: identity(directory / name) for name in RAW_NAMES},
            "outer_job": cleanup}


def provider_facts(directory, plan, source_root, captured):
    run, outer = read(directory / "run.json"), read(directory / "evidence.json")
    require(run["sha256"] == MODULE_SHA256["WindTerm.exe"] and run["files_sha256"] == MANIFEST_SHA256 and
            run["scenario"] == "menu-wrap" and run["profile"] == "synthetic-local-only" and
            run["clipboard"] == "owned-window-station", "Launch identity/scope differs")
    require((directory / "probe.js").read_bytes() == expected_probe(plan, run, source_root) and
            sha(directory / "probe.js") == run["script_sha256"], "Injected probe bytes differ")
    require(outer["ok"] is True and outer["data"]["cleanup_error"] is None, "Provider outer result failed")
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
            "Provider source/lifecycle differs")
    messages, scripts = normalized["messages"], normalized["scripts"]
    count = len(captured["captures"])
    require(type(scripts) is list and len(scripts) == 1 and type(messages) is list and count+2 <= len(messages) <= count+66,
            "Provider message count differs")
    require(all(row["type"] == "send" and row["script_id"] == scripts[0]["script_id"] and type(row["payload"]) is dict
                for row in messages), "Provider script error or unrelated message")
    payloads = [row["payload"] for row in messages]
    require(payloads[0] == {"kind": "probe_ready", "station_isolated": True, "scenario": probe.case_id(plan["attribute"]),
                            "module_guards": captured["module_guards"]}, "Ready probe identity differs")
    readiness = payloads[1:len(payloads)-count-1]
    keys = {"kind", "phase", "ready", "has_tree", "has_engine", "has_editor", "peer_ready"}
    require(all(set(row) == keys and row["kind"] == "readiness" and row["phase"] == "ui" and
                all(type(row[name]) is bool for name in keys-{"kind", "phase"}) for row in readiness), "Readiness differs")
    require(payloads[-count-1:-1] == [{"kind": "attribute_state_raster_phase", **row} for row in captured["captures"]] and
            payloads[-1] == {"kind": "attribute_state_raster_result", **captured}, "Provider and saved capture differ")
    return {"id": "frida", "version": None, "confidence": "observed", "evidence_id": evidence["evidence_id"],
            "messages": len(messages), "readiness_messages": len(readiness), "phase_messages": count,
            "messages_truncated": False, "cleanup_error": None}


def capture_facts(directory, plan, captured):
    attribute = plan["attribute"]
    require(captured["status"] in (probe.COMPLETE, probe.PARTIAL) and captured["reason"] in REASONS and
            captured["presented_done"] is None, "Capture status/reason differs")
    observed, guards = captured["captures"], captured["module_guards"]
    require(type(observed) is list and 0 <= len(observed) <= 5, "Phase count exceeds bound")
    require(type(guards) is list and len(guards) == 4 and {row["name"] for row in guards} == set(MODULE_SHA256) and
            all(row == {"name": row["name"], "path_guard_passed": True} for row in guards), "Module path guard differs")
    peer = read(directory / "peer.json")
    require(peer.get("error") is None, "Peer reported failure")
    grid, freeze, history = peer.get("frozen_grid"), peer.get("freeze_event_count"), peer.get("naws_history", [])
    require(type(history) is list and len(history) <= 64 and
            all(type(row) is list and len(row) == 2 and all(type(n) is int and 0 < n <= 65535 for n in row) for row in history),
            "NAWS observation exceeds bound")
    if grid is None:
        require(not observed and captured["status"] == probe.PARTIAL and freeze is None, "Missing grid after source release")
    else:
        require(grid == [peer["columns"], peer["rows"]] and 40 <= integer(peer["columns"]) <= 160 and
                10 <= integer(peer["rows"]) <= 60, "Frozen grid differs")
        integer(freeze, len(history), 1)
        require(history[freeze-1] == grid and all(row == grid for row in history[freeze:]), "NAWS changed after freeze")
        require((directory / "fixture-ready").read_text() == "stable-NAWS-capacity", "Readiness acknowledgment differs")
    wire = bytes.fromhex(peer["received_hex"])
    replies = list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R", wire))
    require(len(wire) <= 65536 and not re.sub(rb"\x1b\[(\??)(\d+);(\d+)R", b"", wire), "Unexpected application bytes")
    require(len(peer["records"]) == len(replies) == len(observed), "Source/reply/capture counts differ")
    require(captured["grab_phases"] == [row["phase"] for row in observed if row["grab_attempted"]] and
            len(set(captured["grab_phases"])) == len(captured["grab_phases"]), "Extra grab")
    tokens = sorted({row["label"] for target in probe.targets(attribute) for row in target["markers"]})
    synthetic = re.compile(r" *(?:(?:" + "|".join(map(re.escape, tokens)) + r")(?: +|$))*")
    public = []
    for index, (capture, record, reply) in enumerate(zip(observed, peer["records"], replies)):
        expected = probe.phase_records(attribute, peer["rows"], peer["columns"])[index]
        require(capture["phase"] == index and type(capture["phase"]) is int and
                capture["label"] == probe.targets(attribute)[index]["label"] and
                all(record.get(key) == value for key, value in expected.items()), "Exact source/phase differs")
        integer(record["phase"], 4)
        integer(record["source_bytes"], 1024, 1)
        cursor = [integer(int(reply[2]), peer["rows"], 1), integer(int(reply[3]), peer["columns"], 1)]
        require(reply[1] == b"" and cursor == record["cursor_reply"] == capture["cursor_reply"] and
                all(type(n) is int for n in capture["cursor_reply"]), "DSR differs")
        require(capture["reason"] in REASONS and capture["module_guards"] == guards and type(capture["grab_attempted"]) is bool,
                "Capture reason/module differs")
        terminal = capture["terminal"]
        require(terminal["protocol"] == "Telnet" and terminal["connected"] is True and terminal["capacity"] == peer["rows"],
                "Terminal binding differs")
        integer(terminal["capacity"], 60, 10)
        require(type(terminal["lines"]) is list and type(terminal["line_count"]) is int and
                terminal["line_count"] == len(terminal["lines"]) <= 200 and
                all(type(line) is str and len(line) <= peer["columns"] and synthetic.fullmatch(line) for line in terminal["lines"]),
                "Only complete fixed synthetic tokens are publishable")
        require(type(terminal["is_alt"]) is bool and type(terminal["is_main"]) is bool and
                terminal["is_alt"] != terminal["is_main"] and terminal["is_alt"] == (index in (1, 2, 3)), "Active buffer differs")
        require(type(capture["rois"]) is list and len(capture["rois"]) <= 6 and
                type(terminal["markers"]) is list and len(terminal["markers"]) <= 6, "ROI/marker count exceeds bound")
        audited = probe.audit_capture(attribute, capture)
        require(not capture["capture_valid"] or capture["reason"] is None, "Valid capture has a failure reason")
        ack = directory / f"ack-{index}"
        if capture["capture_valid"]:
            require(ack.read_text() == "captured", "Capture acknowledgment differs")
        else:
            require(index == len(observed)-1 and capture["reason"] is not None and not ack.exists(), "Invalid capture advanced source")
        public.append({**expected, "label": capture["label"], "source_label": expected["label"], "cursor_reply": cursor,
                       "cursor_reply_hex": reply[0].hex(), "capture_valid": capture["capture_valid"], "reason": capture["reason"],
                       "grab_attempted": capture["grab_attempted"], "image": audited["image"],
                       "synthetic_document_lines": terminal["lines"], "is_alt": terminal["is_alt"], "is_main": terminal["is_main"],
                       "rois": [{"ordinal": row["ordinal"], "group": row["group"], "sample": row["sample"],
                                 "signature": row["raw_signature"]} for row in audited["rois"]],
                       "attribute_signature_comparison": audited["attribute_signature_comparison"]})
    for index in range(len(observed), 5):
        require(not (directory / f"ack-{index}").exists(), "Acknowledgment beyond observed prefix")
    complete = captured["status"] == probe.COMPLETE
    require(not complete or (len(observed) == 5 and peer["complete"] is True and captured["reason"] is None and
                            all(row["capture_valid"] for row in observed)), "Incomplete capture promoted")
    require(complete or captured["reason"] is not None, "Partial capture has no reason")
    return {"frozen_grid": grid, "naws_history": history, "freeze_event_count": freeze,
            "raw_capture_complete": complete, "observed_phase_count": len(observed), "grab_phases": captured["grab_phases"],
            "phases": public, "same_frame_signature_comparisons": probe.frame_comparisons(public),
            "runtime_module_path_guards": guards}


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    validate_plan(plan)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance_facts = provenance(directory, plan_path, plan, source_root, reference_source.resolve())
    captured = read(directory / probe.RESULT_FILE)
    facts = capture_facts(directory, plan, captured)
    provider = provider_facts(directory, plan, source_root, captured)
    result = {"schema": "termforge-original-attribute-state-reference-v1", "issue": 27, "case_id": plan["case_id"],
              "attribute": plan["attribute"], "requested_sgr": plan["requested_sgr"], "status": captured["status"], "reason": captured["reason"],
              "authorized_plan_sha256": expected_plan_sha256, "offline_exporter_sha256": sha(Path(__file__)),
              "offline_reason_allowlist_source_sha256": sha(ROOT / "research/ui/export_raster_raw_reference.py"),
              "maximum_original_runs": 1, "actual_original_runs": 1, "planned_phase_count": 5,
              "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              **facts, "provider": provider, "provenance": provenance_facts,
              "internal_attribute_verified": False, "full_style_verified": False, "full_grid_verified": False,
              "local_shell_transport_verified": False, "presented_done": None, "performance_gate": "not_evaluated",
              "semantic_acceptance": False, "r2_complete": False, "windterm_parity": False,
              "deliberate_original_termination": True, "natural_gui_exit_verified": False,
              "limitations": [
                  "Exact same-frame ordered H/space output signatures compare SGR0 with one requested SGR; no internal pen, attribute flag or mechanism is read.",
                  "Two repeats and equal ROI shapes/counts are required. Identical controls, inconsistent repeats and unmatched samples stay unknown; they do not establish unsupported attributes or resets.",
                  "No visible ink is required for exact signature matching, including hidden output. No cross-frame or cross-attribute pixel comparisons are made.",
                  "Only five fixed sources, one run and at most one grab per phase are covered; no shifted ROI, extra grab or retry.",
                  "QWidget raster and direct Telnet do not establish physical presentation, ConPTY transport, reliability repetition, performance or complete WindTerm parity.",
                  "Capture45/CLI60 exclude startup and later cleanup; no whole-lifecycle deadline is claimed.",
                  "Raw pixels, paths, process identifiers, original resources, profiles and provider diagnostics stay private.",
                  "The offline exporter was added after capture and has a separate identity; it was not part of the frozen execution inventory."]}
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
    print(json.dumps({"exported": True, "case_id": result["case_id"], "phases": result["observed_phase_count"], "sha256": sha(args.destination)}))
