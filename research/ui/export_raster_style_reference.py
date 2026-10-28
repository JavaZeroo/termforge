"""Explicit offline publication gate for the single #126 raster observation."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.raster_style_reference import MODULE_SHA256,SOURCE_FILES,MANIFEST_SHA256,phases
from research.ui.raster_style_analysis import audit_capture,comparisons,require

SYNTHETIC_LINE=re.compile(r" *(?:(?:M0 H H|A0 H|A1 H|A2 H|M1 H))* *")
REASONS={None,"pinned-module-path","pinned-export-rva","invalid-marker-rectangle","noninteger-roi-boundary","roi-out-of-bounds","space-background-ambiguous","no-marker-ink","dominant-ink-ambiguous","owned-viewport-ambiguous","viewport-dimensions","mode-getters-unavailable","selection-overlaps-observation","marker-binding-ambiguous","extra-grab-for-phase","pixmap-return-address","image-return-address","empty-image","dpr-inconsistent","image-viewport-dpr-inconsistent","caret-location-unknown","marker-text-mismatch","caret-overlaps-roi","h-space-size-inconsistent","calibration-H-colors-differ","reference-styles-not-distinguishable","native-or-observation-error","object-destruction-failed","first-run-guard","scenario-deadline","peer-failed","driver-observation-error","bounded-observation-timer"}


def export(directory,plan_path,destination):
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    read=lambda name:json.loads((directory/name).read_text(encoding="utf-8"))
    plan=json.loads(plan_path.read_text(encoding="utf-8"))
    run,peer,captured=read("run.json"),read("peer.json"),read("raster-result.json")
    outer=read("evidence.json");evidence=outer["data"]["evidence"];normalized=evidence["normalized_result"]
    require(plan["issue"]==126 and plan["maximum_original_runs"]==1 and plan["phase_count"]==5,"Plan scope differs")
    require(plan["maximum_grabs_per_phase"]==1 and plan["maximum_grabs"]==5 and re.fullmatch(r"[0-9a-f]{40}",plan["source_base_commit"]),"Plan capture/source bound differs")
    require(plan["rea_capture_seconds"]==45 and plan["cli_wait_timeout_seconds"]==60 and plan["whole_lifecycle_bound_seconds"] is None,"Plan duration differs")
    require(plan["module_sha256"]==MODULE_SHA256 and plan["files_manifest_sha256"]==MANIFEST_SHA256,"Module/package plan identity differs")
    require(run["sha256"]==MODULE_SHA256["WindTerm.exe"] and run["files_sha256"]==MANIFEST_SHA256,"Observed package identity differs")
    require(set(plan["source_files_sha256"])==set(SOURCE_FILES),"Executed source inventory differs")
    require(all(sha(ROOT/name)==expected for name,expected in plan["source_files_sha256"].items()),"Executed source changed")
    require(normalized["source_sha256"]==run["script_sha256"]==sha(directory/"probe.js"),"Injected source differs")
    require(outer["ok"] and not outer["data"]["cleanup_error"] and not normalized["cleanup_error"] and not normalized["messages_truncated"],"Capture incomplete or cleanup failed")
    require(not any(message["type"]=="error" for message in normalized["messages"]),"Provider script error")
    require(evidence["provider"]=={"id":"frida","name":"Frida","version":None} and evidence["confidence"]=="observed" and evidence["subject"] is None and evidence["environment"] is None,"Provider identity differs")
    require(re.fullmatch(r"ev_[0-9a-f]{64}",evidence["evidence_id"]),"Provider evidence identity differs")
    require(all(value in {"Artifact identity is unavailable for this observation.","The target operating system and architecture were not reported by this observation."} for value in evidence["limitations"]),"Unexpected provider limitation text")
    messages=[message["payload"] for message in normalized["messages"] if message["type"]=="send" and isinstance(message.get("payload"),dict)]
    captured_phases=[{key:value for key,value in message.items() if key!="kind"} for message in messages if message.get("kind")=="raster_phase"]
    finals=[{key:value for key,value in message.items() if key!="kind"} for message in messages if message.get("kind")=="raster_result"]
    require(captured_phases==captured["captures"] and finals==[captured],"Private captures differ from provider messages")
    require(captured["status"] in ("observed-five-raster-phases","partial-raster-observation") and captured["reason"] in REASONS,"Unknown capture status/reason")
    guards=captured["module_guards"]
    require({guard["name"] for guard in guards}==set(MODULE_SHA256) and len(guards)==4 and all(guard=={"name":guard["name"],"path_guard_passed":True} for guard in guards),"Runtime module path guard failed")
    count=len(captured_phases);require(0<=count<=5,"Too many captures")
    expected=phases(*reversed(peer["frozen_grid"]))
    require(peer["frozen_grid"]==[peer["columns"],peer["rows"]],"Frozen grid changed")
    freeze=peer["freeze_event_count"]
    require(type(freeze) is int and 0<=freeze<=len(peer["naws_history"])<=64,"NAWS event bound differs")
    require(all(grid==peer["frozen_grid"] for grid in peer["naws_history"][freeze:]),"NAWS changed after freeze")
    require(all(isinstance(grid,list) and len(grid)==2 and all(type(value) is int and 0<value<=65535 for value in grid) for grid in peer["naws_history"]),"NAWS history bound differs")
    replies=list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R",bytes.fromhex(peer["received_hex"])))
    require(not re.sub(rb"\x1b\[(\??)(\d+);(\d+)R",b"",bytes.fromhex(peer["received_hex"])),"Unexpected application bytes")
    require(len(peer["records"])==len(replies)==count,"Source/reply/capture count differs")
    require(captured["grab_phases"]==[capture["phase"] for capture in captured_phases if capture["grab_attempted"]] and len(set(captured["grab_phases"]))==len(captured["grab_phases"]),"Extra raster capture")
    public_phases=[]
    for index,(capture,record,reply) in enumerate(zip(captured_phases,peer["records"],replies)):
        label,source=expected[index]
        require(capture["phase"]==record["phase"]==index and record["label"]==label,"Phase identity differs")
        require(capture["label"]==["M0 H H","A0 H","A1 H","A2 H","M1 H"][index],"Unexpected marker label")
        require(record["source_hex"]==source.hex() and record["source_sha256"]==hashlib.sha256(source).hexdigest(),"Exact source differs")
        cursor=[int(reply[2]),int(reply[3])];require(reply[1]==b"" and cursor==capture["cursor_reply"]==record["cursor_reply"],"Reply differs")
        require(capture["reason"] in REASONS and capture["module_guards"]==guards,"Capture reason/module differs")
        terminal=capture["terminal"]
        require(terminal["protocol"]=="Telnet" and terminal["connected"] is True and terminal["capacity"]==peer["frozen_grid"][1],"Terminal binding differs")
        require(terminal["line_count"]==len(terminal["lines"])<=200 and all(SYNTHETIC_LINE.fullmatch(line) for line in terminal["lines"]),"Non-synthetic terminal text")
        require(type(terminal["is_alt"]) is bool and type(terminal["is_main"]) is bool and terminal["is_alt"]!=terminal["is_main"],"Mode observation missing")
        value=audit_capture(capture)
        value.update({"source_label":label,"source_hex":source.hex(),"source_sha256":hashlib.sha256(source).hexdigest(),"dsr_request_hex":"1b5b366e","cursor_reply_hex":reply[0].hex(),"is_alt":terminal["is_alt"],"is_main":terminal["is_main"]})
        public_phases.append(value)
        if capture["calibration"]:
            require((directory/f"ack-{index}").read_text()=="observed","Phase acknowledgment missing")
        else:
            require(index==count-1 and not (directory/f"ack-{index}").exists(),"Failed calibration advanced the source")
    complete=captured["status"]=="observed-five-raster-phases"
    require(not complete or (count==5 and peer["complete"] and all(capture["calibration"] for capture in captured_phases)),"Incomplete observation promoted to complete")
    require(complete or captured["reason"] is not None,"Partial observation has no reason")
    cleanup,profile=read("cleanup.json"),read("profile-check.json")
    require(cleanup["active"]==0 and profile=={"only_synthetic_session":True,"session_count":1},"Owned resources/profile failed")
    raw_names=("run.json","probe.js","peer.json","raster-result.json","evidence.json","cleanup.json","profile-check.json","stderr.txt")
    summary={"schema":"termforge-original-raster-style-reference-v1","issue":126,"status":captured["status"],"reason":captured["reason"],
             "scope":"Five-stage owned direct-terminal QWidget raster experiment; only H/space output-color scalars, not internal pen state or presented frames",
             "source_base_commit":plan["source_base_commit"],"source_files_sha256":plan["source_files_sha256"],"exporter_sha256":sha(Path(__file__)),"analysis_sha256":sha(ROOT/"research/ui/raster_style_analysis.py"),
             "module_sha256":plan["module_sha256"],"files_manifest_sha256":MANIFEST_SHA256,"runtime_module_path_guards":guards,
             "plan_sha256":sha(plan_path),"actual_original_runs":1,"maximum_original_runs":1,"rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,
             "frozen_grid":peer["frozen_grid"],"naws_history":peer["naws_history"],"freeze_event_count":freeze,"naws_history_scope":"Each complete valid NAWS event parsed by the owned peer, including repeats","naws_changed_after_freeze":False,
             "planned_phase_count":5,"observed_phase_count":count,"grab_phases":captured["grab_phases"],"maximum_grabs_per_phase":1,"phases":public_phases,
             "sample_color_comparisons":comparisons(captured_phases) if complete else None,
             "provider":{"evidence_id":evidence["evidence_id"],"provider":evidence["provider"],"confidence":evidence["confidence"],"subject":evidence["subject"],"environment":evidence["environment"],"messages_truncated":False,"cleanup_error":None,"limitations":evidence["limitations"]},
             "lifecycle":{"outer_job":cleanup,"profile":profile,"deliberate_original_termination":True,"natural_gui_exit_verified":False},
             "raw_files":{name:{"bytes":(directory/name).stat().st_size,"sha256":sha(directory/name)} for name in raw_names},
             "controlled_raster_observed":any(capture["image"] and not capture["image"]["is_null"] for capture in captured_phases),"sample_colors_verified":complete,"internal_current_style_verified":False,"full_grid_verified":False,"local_shell_transport_verified":False,"presented_done":None,"r2_complete":False,"windterm_parity":False,
             "limitations":["QWidget grab is a controlled raster, not physical display/presented proof.","Dominant ink/background are sampled output colors of one H glyph; they are not the internal QPainter pen or full terminal style state.","Only the declared five sources and one grab per phase are covered. Empty image, coordinate/DPR/ink/caret/selection failures stay unknown without shifted ROI, additional grab or retry.","Two references must actually be distinguishable; SGR indices do not establish palette colors.","Telnet direct-terminal observations do not establish WinPTY/ConPTY local-shell behavior or resolve margins.","45seconds is capture duration and60seconds is CLI wait timeout; target starts before a separate2-second delay and cleanup follows. No whole-lifecycle bound or performance conclusion.","Raw image/ROI pixels, paths, PIDs, copied original resources and profile stay local; only audited color/size scalars and hashes are published.","Provider subject/environment may remain null. Preparation enforces full module/package hashes and the injected script checks actual module paths and exact export RVAs.","Owned launcher deliberately terminates the original GUI; Job active0 is cleanup, not natural-exit proof."]}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"exported":destination.as_posix(),"status":summary["status"],"observed_phases":count,"sample_colors_verified":complete}))


if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--directory",type=Path,required=True);parser.add_argument("--plan",type=Path,required=True);parser.add_argument("--destination",type=Path,required=True);args=parser.parse_args();export(args.directory,args.plan,args.destination)
