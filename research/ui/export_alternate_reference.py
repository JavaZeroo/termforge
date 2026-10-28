"""Export only synthetic bytes, original text projections and bounded run metadata."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.alternate_reference import phases

# This exporter audits the fixed observation, not a newly executed source tree.
OBSERVATION_BASE_COMMIT = "01673e89a7db718a1a6faf63991a50b2531710fa"
SYNTHETIC_LINE = re.compile(r" *(?:MAIN_A|ALT_ONE|DUP|M|ALT_TWO|ALT_LINE_[0-9]{2}(?:ALT47)?|ALT_LINE_[0-9]ALT1047|Q)? *")


def require(condition, message):
    """Publication gates must remain active under Python optimization."""
    if not condition:
        raise ValueError(message)


def export(directory: Path, destination: Path):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    run, peer, result = read("run.json"), read("peer.json"), read("probe-result.json")
    evidence_record = read("evidence.json")
    evidence = evidence_record["data"]["evidence"]
    normalized = evidence["normalized_result"]
    cleanup, profile = read("cleanup.json"), read("profile-check.json")
    require(evidence_record["ok"] and result["status"] == "observed-all-phases", "Observation is not complete")
    require(not result["error"] and peer["complete"] and not peer.get("error"), "Observation contains an error")
    require(cleanup["active"] == 0 and profile == {"only_synthetic_session": True, "session_count": 1}, "Owned resources or profile scope failed validation")
    require(run["sha256"] == "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0", "Original executable identity changed")
    require(run["files_sha256"] == "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203", "Original package manifest changed")
    require(normalized["source_sha256"] == run["script_sha256"] == sha(directory/"probe.js"), "Instrumented probe identity changed")
    message_phases = [message["payload"] for message in normalized["messages"] if message["type"] == "send" and message.get("payload",{}).get("kind") == "alternate_phase"]
    require([{key:value for key,value in message.items() if key != "kind"} for message in message_phases] == result["observations"], "Provider phase messages differ from saved observations")
    message_results = [message["payload"] for message in normalized["messages"] if message["type"] == "send" and message.get("payload",{}).get("kind") == "alternate_reference_result"]
    require(len(message_results) == 1 and {key:value for key,value in message_results[0].items() if key != "kind"} == result, "Provider final message differs from saved result")
    received = bytes.fromhex(peer["received_hex"])
    matches = list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R", received))
    require(not re.sub(rb"\x1b\[(\??)(\d+);(\d+)R", b"", received), "Unexpected application reply bytes")
    # The original resizes during session startup. The peer freezes its payload
    # at first NAWS, but v1 did not retain that initial negotiation separately.
    # Recover only the generator's row argument from the exact retained source;
    # do not substitute the final grid or rewrite any source bytes.
    matching_rows = [rows for rows in range(10,61) if [source.hex() for _,source in phases(rows)] == [record["source_hex"] for record in peer["records"]]]
    require(len(matching_rows) == 1, "Retained source does not uniquely match the public generator")
    source_generation_rows = matching_rows[0]
    expected = phases(source_generation_rows)
    require(len(matches) == len(expected) == len(result["observations"]) == len(peer["records"]) == 26, "Phase, observation or reply count differs")
    exported = []
    for index, ((label, source), record, observation, reply) in enumerate(zip(expected, peer["records"], result["observations"], matches)):
        require(record["phase"] == observation["phase"] == index, "Phase order differs")
        require(record["label"] == observation["label"] == label, "Phase label differs")
        require(record["source_hex"] == source.hex() and record["source_sha256"] == hashlib.sha256(source).hexdigest(), "Phase source identity differs")
        cursor = [int(reply[2]), int(reply[3])]
        require(observation["cursor_reply"] == record["cursor_reply"] == cursor, "Cursor reply differs across retained records")
        require((directory / f"ack-{index}").read_text() == "observed", "Stage acknowledgment is missing or changed")
        terminal = observation["terminal"]
        require(terminal["protocol"] == "Telnet" and terminal["connected"] is True, "Terminal protocol or connection differs")
        require(terminal["capacity"] == peer["rows"], "Captured UI capacity differs from final NAWS")
        require(terminal["line_count"] == len(terminal["lines"]) <= 200, "Document projection exceeds its declared bound")
        for line in terminal["lines"]:
            require(SYNTHETIC_LINE.fullmatch(line), "Only synthetic fixture text is publishable")
        exported.append({**record, "cursor_reply_hex": reply[0].hex(), "cursor_reply_private_prefix": reply[1].decode("ascii"), "terminal_document_projection": terminal})
    require(not normalized["messages_truncated"] and not normalized["cleanup_error"] and not evidence_record["data"]["cleanup_error"], "Provider messages are truncated or cleanup failed")
    require(not any(message["type"] == "error" for message in normalized["messages"]), "Provider contains a script error")
    result = {
        "schema": "termforge-original-alternate-reference-v1", "issue": 118,
        "scope": "One instrumented original WindTerm terminal run over owned loopback Telnet; no local-shell transport or full-grid/style parity claim.",
        "source_base_commit": OBSERVATION_BASE_COMMIT,
        "export_source_base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "reference_identity": {"exe_sha256": run["sha256"], "files_manifest_sha256": run["files_sha256"], "files_count": 4047},
        "probe_files_sha256": {name: sha(ROOT / name) for name in ("research/ui/alternate_reference.py", "research/ui/alternate_reference.js", "research/ui/export_alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py", "research/ui/qt_r1_text.js")},
        "status": "observed-all-phases", "actual_original_runs": 1, "maximum_original_runs": 1,
        "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60,
        "final_observed_naws": [peer["columns"], peer["rows"]], "geometry_retries": 0,
        "source_generation_rows": source_generation_rows, "source_generation_rows_evidence": "Unique exact match of all retained source_hex records against the public deterministic generator; not a retained first-NAWS event.",
        "initial_naws": None, "naws_transition_history": None,
        "phase_transport": "Each phase is sent once followed by CSI 6n. The peer waits for a UI snapshot acknowledgment before sending the next phase; TCP fragmentation is not prescribed.",
        "dsr_request_hex": "1b5b366e", "dsr_reply_count": len(matches), "unmatched_application_reply_hex": "",
        "phases": exported,
        "provider": {"evidence_id": evidence["evidence_id"], "provider": evidence["provider"], "confidence": evidence["confidence"], "subject": evidence["subject"], "environment": evidence["environment"], "messages_truncated": normalized["messages_truncated"], "cleanup_error": normalized["cleanup_error"], "limitations": evidence["limitations"]},
        "lifecycle": {"outer_job": cleanup, "deliberate_original_process_termination": True, "natural_gui_exit_verified": False, "profile": profile, "stderr_bytes": (directory/"stderr.txt").stat().st_size},
        "raw_files": {name: {"bytes": (directory/name).stat().st_size, "sha256": sha(directory/name)} for name in ("evidence.json", "run.json", "probe.js", "probe-result.json", "peer.json", "cleanup.json", "profile-check.json", "stderr.txt")},
        "local_shell_transport_verified": False, "full_active_grid_verified": False, "terminal_styles_verified": False,
        "scroll_region_verified": False, "resize_verified": False, "windterm_parity": False, "r2_complete": False, "presented_done": None,
        "limitations": [
            "The original direct-terminal Telnet path does not prove WinPTY/ConPTY local-shell semantics or resolve #41/#56.",
            "activeText lineText and visibleLineCount expose a retained document projection including main text during alternate mode, not a complete active grid.",
            "scrollTop, caretLine and caretColumn are raw exposed UI values; caretColumn remains 5 even when DSR changes and is not treated as terminal cursor state.",
            "Cell styles/current style, active-buffer flags, full viewport mapping and pending-wrap are unobserved/null.",
            "Duplicate 1049h homes the protocol cursor and removes prior alternate markers from the returned document projection; full-grid clearing remains unproven.",
            "47 and 1047 are observed after earlier 1049 use; fresh-session or all cross-mode transitions are not covered.",
            "1048l does not restore the cursor in this specific input; this is not a claim that every 1048 sequence is unsupported.",
            "One run is not repeated reliability, bytewise parser coverage, presented-frame verification or a performance measurement.",
            "45 seconds is the REA capture duration. The CLI wait timeout is 60 seconds; the original process resumes before a separate 2-second delay and cleanup follows the CLI wait. No 45-second whole-lifecycle guarantee is established.",
            "The fixture chose source_generation_rows=24 at first NAWS, while final NAWS and all captured UI capacities are 25 rows. Initial negotiation and transition history were not retained; the run does not prove a fixed grid throughout startup.",
            "The original GUI is deliberately terminated by its owned launcher; Job active=0 is cleanup proof, not natural exit proof.",
            "Provider subject/environment are null. Pinned executable/package identity is independently enforced by the existing preparation guard.",
            "Raw instrumentation, absolute paths, process identifiers, copied original resources and synthetic profile remain local."
        ],
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8", newline="\n")
    print(json.dumps({"exported": destination.as_posix(), "phases": len(exported), "dsr_replies": len(matches)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    export(args.directory, args.destination)
