"""Export the fixed partial style observation; unmatched painting stays unknown."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.alternate_style_reference import phases, require

SYNTHETIC_LINE = re.compile(r" *(?:(?:TFM[0-2]|TFA[0-4]|TFC[0-3])+|WX?|X|VZ?|Z)? *")
MARKERS = {f"TF{kind}{index}" for kind, count in (("M",3),("A",5),("C",4)) for index in range(count)}


def export(directory: Path, plan_path: Path, destination: Path):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    read = lambda name: json.loads((directory/name).read_text(encoding="utf-8"))
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    run, peer, captured = read("run.json"), read("peer.json"), read("style-probe-result.json")
    outer = read("evidence.json")
    evidence = outer["data"]["evidence"]
    normalized = evidence["normalized_result"]
    cleanup, profile = read("cleanup.json"), read("profile-check.json")
    require(plan["issue"] == 123 and plan["maximum_original_runs"] == 1, "Unexpected observation plan")
    require(plan["source_base_commit"] == "d91eb589f04be6b54a29d237b87c5b05ad243908", "Observation base differs")
    require(plan["rea_capture_seconds"] == 45 and plan["cli_wait_timeout_seconds"] == 60 and plan["whole_lifecycle_bound_seconds"] is None and plan["phase_count"] == 11, "Plan durations or phase count differ")
    require(set(plan["probe_files_sha256"]) == {"research/ui/alternate_style_reference.py","research/ui/alternate_style_reference.js","research/ui/alternate_reference.py","research/ui/r1_launch.py","research/ui/qt_r1_text.js"}, "Probe source inventory differs")
    for name, expected in plan["probe_files_sha256"].items():
        require(name in {"research/ui/alternate_style_reference.py","research/ui/alternate_style_reference.js","research/ui/alternate_reference.py","research/ui/r1_launch.py","research/ui/qt_r1_text.js"}, "Unexpected probe source path")
        require(sha(ROOT/name) == expected, "Executed probe source changed after its plan")
    require(outer["ok"] and not outer["data"]["cleanup_error"], "REA capture failed")
    require(captured["status"] == "observed-all-style-phases" and not captured["error"], "Input phases did not finish")
    require(peer["complete"] and not peer.get("error"), "Owned peer did not finish")
    require(run["sha256"] == plan["reference_exe_sha256"] == "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0", "Original executable identity differs")
    require(run["files_sha256"] == plan["reference_files_manifest_sha256"] == "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203", "Original package identity differs")
    require(normalized["source_sha256"] == run["script_sha256"] == sha(directory/"probe.js"), "Injected script identity differs")
    require(not normalized["messages_truncated"] and not normalized["cleanup_error"], "Capture is truncated or cleanup failed")
    require(not any(message["type"] == "error" for message in normalized["messages"]), "Capture contains a script error")
    messages = [message["payload"] for message in normalized["messages"] if message["type"] == "send" and isinstance(message.get("payload"),dict)]
    observed = [{key:value for key,value in message.items() if key != "kind"} for message in messages if message.get("kind") == "style_phase"]
    finals = [{key:value for key,value in message.items() if key != "kind"} for message in messages if message.get("kind") == "style_reference_result"]
    require(observed == captured["observations"] and finals == [captured], "Provider and retained observations differ")
    require(cleanup["active"] == 0 and profile == {"only_synthetic_session":True,"session_count":1}, "Owned cleanup/profile scope failed")
    columns, rows = peer["frozen_grid"]
    require([columns,rows] == [peer["columns"],peer["rows"]] == [91,25], "Frozen observation grid differs")
    require(peer["naws_history"] == [[91,24],[91,25]], "Retained NAWS history differs")
    require((directory/"fixture-ready").read_text() == "stable-NAWS-capacity", "UI source-freeze acknowledgment differs")
    received = bytes.fromhex(peer["received_hex"])
    replies = list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R",received))
    require(not re.sub(rb"\x1b\[(\??)(\d+);(\d+)R",b"",received), "Unexpected application reply bytes")
    expected = phases(rows,columns)
    require(len(expected) == len(replies) == len(observed) == len(peer["records"]) == 11, "Phase/reply count differs")
    exported = []
    for index, ((label,source), record, observation, reply) in enumerate(zip(expected,peer["records"],observed,replies)):
        require(record["phase"] == observation["phase"] == index and record["label"] == observation["label"] == label, "Phase identity differs")
        require(record["source_hex"] == source.hex() and record["source_sha256"] == hashlib.sha256(source).hexdigest(), "Synthetic source differs")
        cursor = [int(reply[2]),int(reply[3])]
        require(cursor == record["cursor_reply"] == observation["cursor_reply"], "Cursor reply differs")
        require(reply[1] == b"" and (directory/f"ack-{index}").read_text() == "observed", "Reply prefix/stage acknowledgment differs")
        terminal = observation["terminal"]
        require(terminal["protocol"] == "Telnet" and terminal["connected"] is True and terminal["capacity"] == rows, "Terminal identity/capacity differs")
        require(terminal["line_count"] == len(terminal["lines"]) <= 200, "Document projection bound differs")
        require(all(isinstance(line,str) and SYNTHETIC_LINE.fullmatch(line) for line in terminal["lines"]), "Only fixed synthetic text is publishable")
        for mode in (terminal["is_alt"],terminal["is_main"]):
            require(mode == {"callable":True,"value":mode["value"],"error":None} and type(mode["value"]) is bool, "Mode getter was not successfully called")
        require(terminal["is_alt"]["value"] != terminal["is_main"]["value"], "Mode getter values are not complementary")
        marker_values = []
        for marker in terminal["markers"]:
            require(marker["marker"] in MARKERS, "Unexpected marker")
            line, column = marker["document_line"], marker["column"]
            require(type(line) is int and 0 <= line < terminal["line_count"] and type(column) is int and column >= 0, "Marker location bound differs")
            require(terminal["lines"][line][column:column+4] == marker["marker"], "Marker is not bound to retained text")
            require(type(marker["position"]) is int and 0 <= marker["position"] <= 32768, "Marker position bound differs")
            rectangle = marker["rect"]
            require(set(rectangle) == {"x","y","width","height","left","right","top","bottom"}, "Unexpected rectangle fields")
            require(all(type(value) in (int,float) and math.isfinite(value) and abs(value) <= 4096 for value in rectangle.values()), "Marker rectangle bound differs")
            require(rectangle["width"] > 0 and rectangle["height"] > 0, "Invalid marker rectangle")
            marker_values.append({"marker":marker["marker"],"document_line":line,"column":column,"position":marker["position"],"rect":dict(rectangle)})
        exported.append({"phase":index,"label":label,"source_hex":source.hex(),"source_bytes":len(source),"source_sha256":hashlib.sha256(source).hexdigest(),"dsr_suffix_hex":"1b5b366e","cursor_reply":cursor,"cursor_reply_hex":reply[0].hex(),"cursor_reply_private_prefix":"","document_projection":{"lines":terminal["lines"],"line_count":terminal["line_count"],"scroll":terminal["scroll"],"capacity":terminal["capacity"],"markers":marker_values},"is_alt":dict(terminal["is_alt"]),"is_main":dict(terminal["is_main"]),"paint_requested":observation["paint_requested"],"matching_marker_draws":observation["matching_marker_draws"]})
    # This frozen sample found no matching draw/device path. Preserve this failed
    # observation surface instead of publishing an empty list as style success.
    require(captured["draws"] == [] and captured["fills"] == [] and captured["paint_calls_seen"] == 0, "Frozen unmatched-paint result differs")
    require(all(row["matching_marker_draws"] == 0 and row["paint_requested"] is True for row in exported), "Paint-attempt records differ")
    raw_names = ("evidence.json","run.json","probe.js","style-probe-result.json","peer.json","fixture-ready","cleanup.json","profile-check.json","stderr.txt")
    summary = {
        "schema":"termforge-original-alternate-style-reference-v1","issue":123,
        "status":"partial-mode-and-wrap-observation","source_base_commit":plan["source_base_commit"],
        "scope":"One original direct-terminal Telnet observation. Mode getter calls and text/cursor behavior observed; the attempted QPainter style surface did not match.",
        "reference_identity":{"exe_sha256":run["sha256"],"files_manifest_sha256":run["files_sha256"],"files_count":4047},
        "actual_original_runs":1,"maximum_original_runs":1,"rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,
        "naws_history":peer["naws_history"],"frozen_grid":peer["frozen_grid"],"naws_changed_after_freeze":False,
        "source_freeze":"Owned UI acknowledgment after stable NAWS/capacity; any later NAWS change fails the peer.",
        "plan_sha256":sha(plan_path),"probe_files_sha256":dict(plan["probe_files_sha256"]),"exporter_sha256":sha(Path(__file__)),
        "phase_transport":"Each exact synthetic source is followed by CSI 6n and an independent UI acknowledgment; TCP chunking is not prescribed.",
        "dsr_request_hex":"1b5b366e","dsr_reply_count":len(replies),"unmatched_application_reply_hex":"","phases":exported,
        "mode_getters_observed":True,"marker_rectangles_observed":True,
        "paint_observation_attempt":{"surface":"Exact QPainter::drawText(QPointF,QString) and four fillRect exports, filtered to owned viewport/editor QPaintDevice identities","matching_device_text_calls":0,"matching_marker_draws":0,"matching_fills":0,"result":"unmatched","entry_style_inheritance":None,"exit_style_restoration":None,"repeated_entry_style":None,"cell_colors":None},
        "pending_wrap_sample":{"enter_phase":8,"exit_and_write_phase":9,"exit_reply":exported[9]["cursor_reply"],"direct_control_phase":10,"direct_control_reply":exported[10]["cursor_reply"],"dsr_between_W_and_enter":False,"dsr_between_exit_and_X":False},
        "provider":{"evidence_id":evidence["evidence_id"],"provider":evidence["provider"],"confidence":evidence["confidence"],"subject":evidence["subject"],"environment":evidence["environment"],"messages_truncated":False,"cleanup_error":None,"limitations":evidence["limitations"]},
        "lifecycle":{"outer_job":cleanup,"profile":profile,"deliberate_original_termination":True,"natural_gui_exit_verified":False,"stderr_bytes":(directory/"stderr.txt").stat().st_size},
        "raw_files":{name:{"bytes":(directory/name).stat().st_size,"sha256":sha(directory/name)} for name in raw_names},
        "terminal_style_contract_verified":False,"raster_image_verified":False,"full_active_grid_verified":False,"local_shell_transport_verified":False,"presented_done":None,"windterm_parity":False,"r2_complete":False,
        "limitations":["Unmatched QPainter hooks do not imply that the original has no styles; overload, redirected device, repaint scheduling or another path remain unseparated possibilities.","This run did not read pixels or obtain an image. Paint-call attempts cannot prove presented frames or pixel equivalence.","Mode getter booleans and marker rectangles establish the sampled original states; retained document lines are not a complete active grid.","Four corner markers disappear from the retained projection after duplicate1049h; entire-grid clearing and blank-cell styles remain unknown.","Pending-wrap evidence is limited to this exact 91x25 direct-terminal input and its control, not all mode/resize crossings.","The attempted entry/current-style/exit restoration observations remain null. No generic-standard behavior substitutes for them.","Telnet does not establish local-shell WinPTY/ConPTY semantics or resolve41/56.","45seconds is REA capture duration,60seconds is CLI wait timeout; target launch precedes a separate2-second delay and cleanup follows CLI wait. No whole-lifecycle bound is asserted.","Instrumentation is correctness-only. No latency, throughput, presented endpoint, reliability repetition or performance claim.","Provider subject/environment remain null; executable/package identity is independently enforced by preparation and module-path guards.","The original GUI is deliberately ended by the owned launcher. Job active0 is cleanup proof, not natural exit proof.","Copied original resources, raw probes, paths, identifiers and synthetic profile remain local; only fixed synthetic inputs and audited scalar observations are published."]
    }
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"exported":destination.as_posix(),"status":summary["status"],"phases":len(exported),"styles_verified":False}))


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--directory",type=Path,required=True)
    parser.add_argument("--plan",type=Path,required=True)
    parser.add_argument("--destination",type=Path,required=True)
    args=parser.parse_args()
    export(args.directory,args.plan,args.destination)
