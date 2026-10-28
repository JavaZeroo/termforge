"""Audit the one fixed origin/wrap capture and publish a closed synthetic projection."""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import origin_wrap_reference as probe

EXECUTED_SOURCE = "72e661d964b771eae226e3bc301ed213c5e55ce8"
PLAN_SHA256 = "0bb60918f4236c36349d9b87f2cc4c0c71c5147a89a12ddeff1db7b8d6959dc6"
SNAPSHOT_SHA256 = "e5cb4a124a18774faad93bd02238a4bef7c2e031b0f9ab1add9395ac8244d437"
FALSE_PLANS = (
    ("proposal.json", "initial", "e64ca5343021e25abb3f8dc530da8c8afaa75fa3f08b00347fca93a8a371dcf1", "e133b5122136c7309d4af6e2c9ce5bd72f91ee8d"),
    ("proposal-node-bound.json", "node-bound", "18b7fcfc2963ca38d13e29e3df6f97303471209473ccfdf67f7849ca83c22679", "21fc745d5a25339a4ce8f11ba1e1090a81fa1cab"),
    ("proposal-stability-bound.json", "stability-bound", "50546257a4fde7e868dfb2d041d572bc632c6eca60578a12694b5683d35d7bdd", EXECUTED_SOURCE),
)
RAW_NAMES = ("evidence.json", "run.json", "probe.js", "peer.json", "origin-wrap-result.json", "files.json",
             "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
             *(f"ack-{index}" for index in range(25)))
SYNTHETIC_LINE = re.compile(r"(?: *(?:FT|D|O|TP|BT|R[0-3]|NW|UP|DN|DONE|R2 M|UP   R|A|H)? *| {90}(?:AB|EF|IJ))")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bounded_integer(value, maximum, message):
    require(type(value) is int and 0 <= value <= maximum, message)
    return value


def file_identity(path):
    return {"bytes": path.stat().st_size, "sha256": sha(path)}


def expected_probe(directory, run):
    bounded_integer(run["pid"], 2**32-1, "Process identity is not an integer")
    bounded_integer(run["tid"], 2**32-1, "Thread identity is not an integer")
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]), "Owned station identity differs")
    template = (ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += (ROOT / "research/ui/origin_wrap_reference.js").read_text(encoding="utf-8")
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(directory.as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    # The executed Windows write_text used its default LF-to-CRLF translation.
    return generated.encode().replace(b"\n", b"\r\n")


def capture_facts(directory):
    mechanics = probe.audit_mechanics(directory)
    require(read(directory / "mechanics-check.json") == mechanics, "Saved mechanics audit differs")
    peer, result, run, outer = [read(directory / name) for name in
        ("peer.json", "origin-wrap-result.json", "run.json", "evidence.json")]
    require(run["sha256"] == probe.EXE_SHA256 and run["files_sha256"] == probe.MANIFEST_SHA256, "Original identity differs")
    require(run["scenario"] == "menu-wrap" and run["profile"] == "synthetic-local-only" and
            run["clipboard"] == "owned-window-station", "Launch scope differs")
    require((directory / "probe.js").read_bytes() == expected_probe(directory, run) and
            sha(directory / "probe.js") == run["script_sha256"], "Runtime probe bytes differ")
    require(outer["ok"] is True and outer["data"]["cleanup_error"] is None, "REA outer result failed")
    evidence = outer["data"]["evidence"]
    normalized = evidence["normalized_result"]
    require(evidence["provider"] == {"id": "frida", "name": "Frida", "version": None} and
            evidence["confidence"] == "observed" and evidence["operation"] == "instrument_with_frida" and
            evidence["subject"] is None and evidence["environment"] is None and evidence["raw_result"] is None,
            "Provider observation identity differs")
    require(type(evidence["evidence_id"]) is str and re.fullmatch(r"ev_[0-9a-f]{64}", evidence["evidence_id"]), "Evidence identity differs")
    require(normalized["messages_truncated"] is False and normalized["cleanup_error"] is None and
            normalized["source_sha256"] == run["script_sha256"] and normalized["mode"] == "attach" and
            normalized["source_kind"] == "file" and normalized["state"] == "running" and
            normalized["pid"] == run["pid"] and Path(normalized["source_path"]).resolve() == directory / "probe.js",
            "Provider source/lifecycle differs or is incomplete")
    scripts, messages = normalized["scripts"], normalized["messages"]
    require(type(scripts) is list and len(scripts) == 1 and type(messages) is list and len(messages) == 30,
            "Provider script/message count differs")
    require(all(message["type"] == "send" and message["script_id"] == scripts[0]["script_id"] and
                type(message["payload"]) is dict for message in messages), "Provider contains an error or unrelated script")
    payloads = [message["payload"] for message in messages]
    require([row.get("kind") for row in payloads] == ["probe_ready", *(["readiness"]*3), *(["origin_wrap_phase"]*25), "origin_wrap_result"],
            "Provider message sequence differs")
    require(payloads[0] == {"kind": "probe_ready", "station_isolated": True, "scenario": probe.CASE_ID}, "Probe ready identity differs")
    for readiness in payloads[1:4]:
        require(readiness["phase"] == "ui" and all(type(readiness[key]) is bool for key in
                ("ready", "has_tree", "has_engine", "has_editor", "peer_ready")), "Readiness record differs")
    require([{key: value for key, value in row.items() if key != "kind"} for row in payloads[4:29]] == result["observations"] and
            {key: value for key, value in payloads[-1].items() if key != "kind"} == result, "Provider and saved observations differ")
    require(peer["frozen_grid"] == [91, 25] and peer["naws_history"] == [[91, 24], [91, 25]] and
            peer["freeze_event_count"] == 2 and (directory / "fixture-ready").read_text() == "stable-NAWS-capacity",
            "Fixed observation NAWS/freeze identity differs")
    phases = []
    for source, row, observation in zip(probe.phase_records(), peer["records"], result["observations"]):
        terminal = observation["terminal"]
        require(terminal["top"] is None and terminal["cell_styles"] is None and terminal["current_style"] is None,
                "An unknown terminal endpoint changed")
        require(all(type(line) is str and len(line) <= 92 and SYNTHETIC_LINE.fullmatch(line) for line in terminal["lines"]),
                "Only fixed synthetic document text is publishable")
        scroll = bounded_integer(terminal["scroll"], 200, "Document scroll is not a bounded integer")
        caret = {key: bounded_integer(terminal["caret_api"][key], maximum, "Caret API value is not a bounded integer")
                 for key, maximum in (("caretLine", 200), ("caretColumn", 500), ("caretPosition", 32768))}
        cursor = [bounded_integer(n, 65535, "DSR coordinate is not a bounded integer") for n in row["cursor_reply"]]
        require(row["cursor_reply_private"] is False, "Fixed observation DSR prefix differs")
        phases.append({**source, "cursor_reply": cursor, "response_hex": row["response_hex"], "private_dsr_prefix": False,
                       "document_projection": {"lines": list(terminal["lines"]), "line_count": len(terminal["lines"]),
                           "top": None, "scroll": scroll, "capacity": 25, "caret_api": caret}})
    cleanup = read(directory / "cleanup.json")
    account = {key: bounded_integer(cleanup[key], 100, "Job account is not a bounded integer") for key in ("active", "total", "terminated")}
    require(account == {"active": 0, "total": 3, "terminated": 0}, "Fixed observation Job account differs")
    return {"phases": phases, "source_bytes": sum(row["source_bytes"] for row in phases), "dsr_request_bytes": 100,
            "application_reply_bytes": len(bytes.fromhex(peer["received_hex"])), "naws_history": [[91, 24], [91, 25]],
            "frozen_grid": [91, 25], "freeze_event_count": 2, "job_after": account,
            "provider": {"id": "frida", "version": None, "confidence": "observed", "evidence_id": evidence["evidence_id"],
                         "subject": None, "environment": None, "messages": 30, "phase_messages": 25,
                         "messages_truncated": False, "cleanup_error": None}}


def provenance_facts(directory, plan_path):
    require(sha(plan_path) == PLAN_SHA256, "Authorized plan bytes differ")
    plan = read(plan_path)
    probe.validate_fixed_contract(plan)
    require(plan["execution_authorized"] is True and plan["source_base_commit"] == EXECUTED_SOURCE and
            directory == Path(plan["out"]).resolve(), "Executed plan/source/output identity differs")
    require(plan_path.with_suffix(".attempted").read_text() == "One original launch attempt; retain after failure and never retry this plan.\n",
            "Single-attempt marker differs")
    require(set(plan["source_files_sha256"]) == set(probe.SOURCE_FILES), "Execution source inventory differs")
    source_hashes = {}
    for name in probe.SOURCE_FILES:
        blob = subprocess.check_output(["git", "show", f"{EXECUTED_SOURCE}:{name}"], cwd=ROOT)
        require(sha(ROOT / name) == hashlib.sha256(blob).hexdigest() == plan["source_files_sha256"][name], "Executed/current source bytes differ")
        source_hashes[name] = sha(ROOT / name)
    require(hashlib.sha256(probe.launcher_source().encode()).hexdigest() == plan["launcher_source_sha256"], "Launcher namespace source differs")
    require(probe.rea_identity(Path(plan["rea"])) == plan["rea_prepared_identity"] and
            all(plan[key] == value for key, value in probe.host_identity().items()), "Current runtime/host bytes differ from plan")
    require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", plan["windows_version"]), "Windows version is not a safe scalar")
    python_version = re.match(r"[0-9]+\.[0-9]+\.[0-9]+", plan["python_version"])
    require(python_version is not None, "Python version is not a safe scalar")
    files = read(directory / "files.json")
    canonical = json.dumps(files, sort_keys=True, separators=(",", ":")).encode()
    require(type(files) is dict and len(files) == 4047 and hashlib.sha256(canonical).hexdigest() == probe.MANIFEST_SHA256,
            "Copied distribution manifest differs")
    for name, expected in files.items():
        require(sha(directory / "app" / name) == sha(Path(plan["source"]) / name) == expected, "Original distribution file changed")
    require(read(directory / "run.json")["fixture_sha256"] == source_hashes["research/ui/r1_vt_fixture.py"], "Legacy fixture metadata tag differs")
    sessions = read(directory / "profile/.wind/profiles/default.v10/terminal/user.sessions")
    require(type(sessions) is list and len(sessions) == 1 and all(sessions[0].get(key) == value for key, value in
        {"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK", "session.protocol": "Telnet",
         "session.target": "127.0.0.1", "session.uuid": "00000000-0000-4000-8000-000000000001"}.items()), "Actual profile scope differs")
    folder = ROOT / "local/origin-wrap-preflight"
    snapshot_path = folder / "post-run-originals-before-export.json"
    require(sha(snapshot_path) == SNAPSHOT_SHA256, "Preserved original inventory changed")
    snapshot = read(snapshot_path)
    require(len(snapshot) == 43 and all(file_identity(ROOT / name) == identity for name, identity in snapshot.items()), "An original observation/proposal artifact changed")
    false_plans = []
    for index, (name, label, expected, source) in enumerate(FALSE_PLANS):
        path = folder / name
        require(sha(path) == expected, "An unexecuted proposal changed")
        value = read(path)
        require(value["execution_authorized"] is False and value["source_base_commit"] == source and
                not path.with_suffix(".attempted").exists(), "A false proposal execution state differs")
        shared = Path(value["out"]).resolve() == directory
        require(shared is (index == 0) and Path(value["out"]).exists() is shared, "False proposal output alias differs")
        false_plans.append({"label": label, **file_identity(path), "source_base_commit": source,
                            "execution_authorized": False, "attempt_present": False, "output_present": shared,
                            "output_shared_with_authorized_plan": shared})
    wrapper = read(folder / "authorized-run-exit.json")
    require(wrapper["schema"] == "termforge-private-origin-wrap-launch-exit-v1" and type(wrapper["exit_code"]) is int and
            wrapper["exit_code"] == 0 and wrapper["source_commit"] == EXECUTED_SOURCE and wrapper["plan_sha256"] == PLAN_SHA256,
            "Outer launch result differs")
    for key in ("started_utc", "finished_utc"):
        require(type(wrapper[key]) is str and re.fullmatch(r"[0-9]{4}(?:-[0-9]{2}){2}T[0-9]{2}(?::[0-9]{2}){2}\.[0-9]{7}Z", wrapper[key]), "Wrapper timestamp differs")
    seconds = (datetime.fromisoformat(wrapper["finished_utc"].replace("Z", "+00:00")) -
               datetime.fromisoformat(wrapper["started_utc"].replace("Z", "+00:00"))).total_seconds()
    require(seconds > 0, "Wrapper interval is invalid")
    tool = plan["rea_prepared_identity"]
    return {"execution_source_files_sha256": source_hashes, "launcher_namespace_sha256": plan["launcher_source_sha256"],
            "reference_identity": {"exe_sha256": probe.EXE_SHA256, "manifest_sha256": probe.MANIFEST_SHA256, "files_verified": 4047},
            "launch_host": {"windows_version": plan["windows_version"], "python_version_number": python_version[0],
                            "python_version_text_sha256": hashlib.sha256(plan["python_version"].encode()).hexdigest(),
                            "python_exe_sha256": plan["python_sha256"], "node_exe_sha256": tool["node_executable_sha256"],
                            "node_process_version_observed_in_original_run": None,
                            "rea_package_version": "6.7.0", "rea_shim_sha256": tool["shim_sha256"],
                            "rea_package_json_sha256": tool["package_json_sha256"], "rea_entry_sha256": tool["entry_sha256"]},
            "wrapper": {"exit_code": 0, "started_utc": wrapper["started_utc"], "finished_utc": wrapper["finished_utc"],
                        "observed_wall_seconds": seconds, "whole_lifecycle_bound_seconds": None},
            "preserved_original_inventory": {"files": 43, "sha256": SNAPSHOT_SHA256}, "unexecuted_proposals": false_plans,
            "raw_files": {name: file_identity(directory / name) for name in RAW_NAMES},
            "authorized_plan": file_identity(plan_path), "outer_launch_log": file_identity(folder / "authorized-run.log"),
            "outer_launch_exit": file_identity(folder / "authorized-run-exit.json")}


def export(directory, plan_path, destination):
    directory, plan_path, destination = [path.resolve() for path in (directory, plan_path, destination)]
    require(not destination.exists(), "Export destination must be new")
    facts = capture_facts(directory)
    provenance = provenance_facts(directory, plan_path)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "Export source commit is invalid")
    result = {"schema": "termforge-original-origin-wrap-reference-v1", "issue": 151,
              "execution_source_commit": EXECUTED_SOURCE, "offline_export_source_commit": commit,
              "offline_exporter_sha256": sha(Path(__file__)), "authorized_plan_sha256": PLAN_SHA256,
              "status": "bounded-direct-protocol-and-document-observation", "actual_original_runs": 1, "maximum_original_runs": 1,
              "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "semantic_acceptance": False, "full_active_grid_verified": False, "terminal_styles_verified": False,
              "local_shell_transport_verified": False, "presented_done": None, "performance_gate": "not_evaluated",
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "synthetic_profile_verified": True, "stderr_bytes": 0, "r2_complete": False,
              "capture": facts, "provenance": provenance}
    payload = (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as handle:
        handle.write(payload)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "plan", "destination"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination)
    print(json.dumps({"exported": True, "phases": len(result["capture"]["phases"]), "sha256": sha(args.destination)}))
