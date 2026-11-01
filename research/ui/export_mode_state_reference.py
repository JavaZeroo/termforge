"""Validate one immutable mode-state recording and export only synthetic observations.

The caller supplies the plan SHA recorded before execution. Source/package roots
may refer to verified local copies after transfer; injected paths are reconstructed
from the original plan, never rewritten in the retained recording.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as probe

FILE_COUNT = 4047
MODULES = ("WindTerm.exe", "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5Qml.dll")
MARKERS = ("MP", "AS", "AI", "AO", "AP", "PS", "PI", "PO", "NW", "SB",
           "R01", "R02", "R03", "R04", "R05", "R06", "R07", "R08")
# All writes start at column 1 or 10. Overwriting R0N with a two-letter marker
# can retain its final digit; this permits those fixed fragments, not arbitrary
# uppercase text or a character-class approximation of private document data.
SYNTHETIC_LINE = re.compile(r" *(?:(?:R0[1-8]|(?:AS|AI|AO|PS|PI|PO|NW|SB)[1-8]?|DONE)(?: +(?:MP|AP))?|MP|AP)? *")
RAW_NAMES = ("evidence.json", "run.json", "probe.js", "peer.json", "mode-state-result.json", "files.json",
             "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
             *(f"ack-{index}" for index in range(probe.PHASE_COUNT)))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    def unique(pairs):
        value = {}
        for key, item in pairs:
            require(key not in value, "Duplicate recording field")
            value[key] = item
        return value
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value), "Invalid identity hash")
    return value


def integer(value, maximum=65535, minimum=0):
    require(type(value) is int and minimum <= value <= maximum, "Unbounded or noninteger observation")
    return value


def identity(path):
    return {"bytes": path.stat().st_size, "sha256": sha(path)}


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1)
    integer(run["tid"], 2**32-1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    base = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    driver = (source_root / "research/ui/mode_state_reference.js").read_text(encoding="utf-8")
    require(driver.count("__MODE_STATE_CASE_JSON__") == 1, "Case template anchor differs")
    template = base + driver.replace("__MODE_STATE_CASE_JSON__", json.dumps(plan["case_id"]))
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    # The frozen Windows launcher uses write_text's native newline translation.
    return generated.encode().replace(b"\n", b"\r\n")


def marker_projection(terminal):
    markers = terminal["markers"]
    require(type(markers) is list and len(markers) <= 256 and all(type(item) is dict for item in markers),
            "Marker list bound differs")
    expected = []
    for row, line in enumerate(terminal["lines"]):
        for label in MARKERS:
            start = line.find(label)
            while start >= 0:
                expected.append((label, row, start))
                start = line.find(label, start+1)
    require([(item.get("label"), item.get("document_line"), item.get("column")) for item in markers] == expected,
            "Marker text binding differs")
    result = []
    for item in markers:
        rect = item["rect"]
        if rect is not None:
            require(type(rect) is dict, "Marker rectangle is not an object")
            rect = {name: rect[name] for name in ("x", "y", "width", "height")}
            require(all(type(value) in (int, float) and math.isfinite(value) and abs(value) <= 1000000
                        for value in rect.values()), "Marker rectangle contains non-scalar/private data")
        result.append({"label": item["label"], "document_line": integer(item["document_line"], 199),
                       "column": integer(item["column"], 1024), "position": integer(item["position"], 262144), "rect": rect})
    return result


def capture_facts(directory, plan, source_root=ROOT):
    case = plan["case_id"]
    mechanics = probe.audit_mechanics(directory, case)
    require(read(directory / "mechanics-check.json") == mechanics, "Saved mechanics audit differs")
    peer, result, run, outer = [read(directory / name) for name in
                              ("peer.json", "mode-state-result.json", "run.json", "evidence.json")]
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
    require(type(scripts) is list and len(scripts) == 1 and type(messages) is list and
            probe.PHASE_COUNT+3 <= len(messages) <= probe.PHASE_COUNT+66, "Provider message count differs")
    require(all(row["type"] == "send" and row["script_id"] == scripts[0]["script_id"] and
                type(row["payload"]) is dict for row in messages), "Provider script error or unrelated message")
    payloads = [row["payload"] for row in messages]
    require(payloads[0] == {"kind": "probe_ready", "station_isolated": True, "scenario": "mode-state-v1", "case": case},
            "Probe ready identity differs")
    count = len(payloads)-probe.PHASE_COUNT-2
    readiness = payloads[1:1+count]
    require(all(row.get("kind") == "readiness" and row["phase"] == "ui" and
                all(type(row[name]) is bool for name in ("ready", "has_tree", "has_engine", "has_editor", "peer_ready"))
                for row in readiness), "Readiness sequence differs")
    phases = payloads[1+count:-1]
    require(all(row.get("kind") == "mode_state_phase" and row.get("case") == case for row in phases) and
            [{key: value for key, value in row.items() if key not in ("kind", "case")} for row in phases] == result["observations"] and
            payloads[-1] == {"kind": "mode_state_result", **result}, "Provider and saved observations differ")
    require((directory / "fixture-ready").read_text() == "stable-NAWS-capacity", "Readiness acknowledgment differs")
    grid = peer["frozen_grid"]
    exported = []
    for source, row, observed in zip(probe.phase_records(case, grid[1], grid[0]), peer["records"], result["observations"]):
        terminal = observed["terminal"]
        require(terminal["cell_styles"] is None and terminal["current_style"] is None, "Unknown style endpoints changed")
        require(all(len(line) <= grid[0] and SYNTHETIC_LINE.fullmatch(line) for line in terminal["lines"]),
                "Only fixed synthetic document text is publishable")
        flags = {name: terminal[name] for name in ("is_alt", "is_main")}
        require(all(value is None or type(value) is bool for value in flags.values()), "Mode getter is not a safe scalar")
        caret = {name: integer(terminal["caret_api"][name], limit) for name, limit in
                 (("caretLine", 200), ("caretColumn", 500), ("caretPosition", 262144))}
        exported.append({**source, "cursor_reply": [integer(value) for value in row["cursor_reply"]],
                         "response_hex": row["response_hex"], "private_dsr_prefix": row["cursor_reply_private"],
                         "document_projection": {"lines": list(terminal["lines"]), "line_count": terminal["line_count"],
                             "capacity": grid[1], "top": None if terminal["top"] is None else integer(terminal["top"], 200),
                             "scroll": integer(terminal["scroll"], 200), "caret_api": caret, "mode_getters": flags,
                             "markers": marker_projection(terminal)}})
    cleanup = read(directory / "cleanup.json")
    return {"phases": exported, "source_bytes": sum(row["source_bytes"] for row in exported),
            "dsr_request_bytes": 4*probe.PHASE_COUNT, "application_reply_bytes": len(bytes.fromhex(peer["received_hex"])),
            "frozen_grid": grid, "naws_history": peer["naws_history"], "freeze_event_count": peer["freeze_event_count"],
            "job_after": {name: integer(cleanup[name], 100) for name in ("active", "total", "terminated")},
            "provider": {"id": "frida", "version": None, "confidence": "observed", "evidence_id": evidence["evidence_id"],
                         "messages": len(messages), "readiness_messages": count, "phase_messages": probe.PHASE_COUNT,
                         "messages_truncated": False, "cleanup_error": None}}


def provenance_facts(directory, plan_path, plan, source_root, reference_source):
    require(plan_path.with_suffix(".attempted").read_text() ==
            "One original launch attempt; retain after failure and never retry this plan.\n", "Single-attempt marker differs")
    require(set(plan["source_files_sha256"]) == set(probe.SOURCE_FILES), "Execution source inventory differs")
    source_hashes = {name: sha(source_root / name) for name in probe.SOURCE_FILES}
    require(source_hashes == plan["source_files_sha256"] == {name: sha(ROOT / name) for name in probe.SOURCE_FILES},
            "Executed/current source bytes differ")
    require(type(plan["source_base_commit"]) is str and re.fullmatch(r"[0-9a-f]{40}", plan["source_base_commit"]),
            "Execution base commit differs")
    recorded_identity = plan["source_identity"]
    if recorded_identity.get("kind") == "source-archive":
        verified = probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)
        require(all(plan.get(key) == value for key, value in verified.items()), "Source archive identity differs")
        source_identity = {"kind": "source-archive", "manifest_sha256": digest(recorded_identity["manifest_sha256"])}
    else:
        require(recorded_identity == {"kind": "git-checkout"}, "Source identity kind differs")
        source_identity = {"kind": "git-checkout"}
    require(hashlib.sha256(probe.launcher_source(plan["case_id"]).encode()).hexdigest() == plan["launcher_source_sha256"],
            "Launcher namespace source differs")
    files = read(directory / "files.json")
    require(type(files) is dict and len(files) == FILE_COUNT and
            hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest() == probe.MANIFEST_SHA256,
            "Complete distribution manifest differs")
    for name, expected in files.items():
        relative = PurePosixPath(name)
        require(type(name) is str and not relative.is_absolute() and ".." not in relative.parts and "\\" not in name and ":" not in name,
                "Distribution path is not a safe relative path")
        digest(expected)
        for base in (directory / "app", reference_source):
            path = base / name
            require(not path.is_symlink() and path.resolve().is_relative_to(base.resolve()) and sha(path) == expected,
                    "Original distribution file changed")
    require(all(name in files for name in MODULES), "Required original modules missing")
    require(files["WindTerm.exe"] == probe.EXE_SHA256, "Copied original executable differs")
    require(read(directory / "run.json")["fixture_sha256"] == source_hashes["research/ui/r1_vt_fixture.py"], "Fixture metadata differs")
    sessions = read(directory / "profile/.wind/profiles/default.v10/terminal/user.sessions")
    expected_session = {"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK", "session.protocol": "Telnet",
                        "session.target": "127.0.0.1", "session.uuid": "00000000-0000-4000-8000-000000000001"}
    require(type(sessions) is list and len(sessions) == 1 and
            set(sessions[0]) == {*expected_session, "session.port"} and
            all(sessions[0].get(name) == value for name, value in expected_session.items()), "Actual profile scope differs")
    integer(sessions[0]["session.port"], 65535, 1)
    tool = plan["rea_prepared_identity"]
    require(tool["version"] == "6.7.0", "REA version differs")
    require(type(plan["windows_version"]) is str and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", plan["windows_version"]), "Windows scalar differs")
    version = re.match(r"[0-9]+\.[0-9]+\.[0-9]+", plan["python_version"])
    require(version is not None, "Python scalar differs")
    return {"source_identity": source_identity, "execution_source_base_commit": plan["source_base_commit"],
            "execution_source_files_sha256": source_hashes, "launcher_namespace_sha256": digest(plan["launcher_source_sha256"]),
            "reference_identity": {"exe_sha256": probe.EXE_SHA256, "manifest_sha256": probe.MANIFEST_SHA256,
                                   "files_verified": FILE_COUNT, "module_files_sha256": {name: files[name] for name in MODULES}},
            "launch_host": {"windows_version": plan["windows_version"], "python_version_number": version[0],
                            "python_exe_sha256": digest(plan["python_sha256"]), "node_exe_sha256": digest(tool["node_executable_sha256"]),
                            "rea_package_version": "6.7.0", "rea_shim_sha256": digest(tool["shim_sha256"]),
                            "rea_package_json_sha256": digest(tool["package_json_sha256"]), "rea_entry_sha256": digest(tool["entry_sha256"]),
                            "runtime_identity_source": "recorded execution plan; exporter does not execute the captured runtimes"},
            "raw_files": {name: identity(directory / name) for name in RAW_NAMES}, "authorized_plan": identity(plan_path)}


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [path.resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    probe.validate_fixed_contract(plan)
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    facts = capture_facts(directory, plan, source_root)
    reference_source = Path(plan["source"]) if reference_source is None else reference_source
    provenance = provenance_facts(directory, plan_path, plan, source_root, reference_source.resolve())
    result = {"schema": "termforge-original-mode-state-reference-v1", "issue": 27, "case_id": plan["case_id"],
              "authorized_plan_sha256": expected_plan_sha256, "offline_exporter_sha256": sha(Path(__file__)),
              "status": "bounded-direct-protocol-and-document-observation", "actual_original_runs": 1, "maximum_original_runs": 1,
              "semantic_acceptance": False, "origin_state_classification": None, "margin_restoration_classification": None,
              "full_active_grid_verified": False, "terminal_styles_verified": False, "local_shell_transport_verified": False,
              "presented_done": None, "performance_gate": "not_evaluated", "r2_complete": False,
              "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "capture": facts, "provenance": provenance,
              "limitations": probe.fixed_contract(plan["case_id"])["interpretation_notes"] + [
                  "One direct Telnet run does not establish local-shell ConPTY behavior, reliability repetition or performance.",
                  "DSR, document marker locations and mode getter values remain separate observations; no shared/per-buffer origin decision is exported.",
                  "The fixed script enforces WindTerm/QtCore/QtGui/QtWidgets module paths; complete package and module file hashes are audited separately.",
                  "Raw paths, process identifiers, profiles, resources and provider diagnostics remain private."]}
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
    print(json.dumps({"exported": True, "case_id": result["case_id"], "phases": len(result["capture"]["phases"]), "sha256": sha(args.destination)}))
