"""Fixed requested-height geometry observations without terminal application input."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui import mode_state_reference as common
require,sha=common.require,common.sha
EXE_SHA256,MANIFEST_SHA256=common.EXE_SHA256,common.MANIFEST_SHA256
CASE_ID="height-geometry"
CASES={CASE_ID:None}
WINDOWS=((1360,749),(1360,569))
RESULT_FILE="resize-geometry-result.json"
ATTEMPT="One original resize-geometry launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES=("research/ui/resize_geometry_reference.py","research/ui/resize_geometry_reference.js",
 "research/ui/mode_state_reference.py","research/ui/alternate_reference.py","research/ui/r1_launch.py",
 "research/ui/launch.py","research/ui/qt_r1_text.js","research/ui/r1_vt_fixture.py","experiments/conpty/run.py")
WIDGET_ERRORS=("widget_enumeration_failed","terminal_not_unique","editor_not_unique","viewport_not_unique",
 "parent_chain_bound","widget_geometry_failed","widget_node_bound")
ERROR_CODES=("window_read_failed","terminal_read_failed","initial_geometry_differs","peer_failed","unexpected_axis",
 "unarmed_naws","naws_bound","snapshot_bound","deadline","controller_fault","geometry_not_ready","phase_order")


def validate_case(case):
    require(case==CASE_ID and type(case) is str,"Unknown fixed geometry case")
    return case


def windows(case=CASE_ID):
    validate_case(case);return list(WINDOWS)


def phases(case=CASE_ID):
    validate_case(case);return [("initial-geometry",b""),("requested-height-geometry",b"")]


def qrect_size(value):
    require(type(value) is list and len(value)==4 and all(type(v) is int and abs(v)<=1000000 for v in value),"QRect bound")
    require(value[2]>=value[0] and value[3]>=value[1],"QRect extent")
    return [value[2]-value[0]+1,value[3]-value[1]+1]


def peer_class(case=CASE_ID):
    validate_case(case)
    path=ROOT/"research/ui/r1_vt_fixture.py"
    source=path.read_text(encoding="utf-8")
    begin=source.index('                if self.state["columns"] and not sent:')
    end=source.index('                self.state["received_hex"] = received.hex()',begin)
    source=source[:begin]+'                self._geometry_gate()\n'+source[end:]
    replacements={
      'deadline = time.monotonic() + 150':'deadline = time.monotonic() + 43',
      'self.state["rows"] = int.from_bytes(sub[3:5], "big")':'self.state["rows"] = int.from_bytes(sub[3:5], "big")\n                                self._on_naws()',
      'if len(received) > 65536:':'if received:',
      'raise RuntimeError("Input bound")':'raise ValueError("Unexpected application input")',
      '"sent_sha256": None}':'"sent_sha256": None, "application_sent_bytes": 0, "naws_history": [], "phase": None, "resize_armed": False, "case_id": "height-geometry"}',
    }
    for old,new in replacements.items():
        require(source.count(old)==1,"Protected geometry peer anchor changed")
        source=source.replace(old,new)
    namespace={"__file__":str(path),"__name__":"termforge_geometry_peer"}
    exec(compile(source,str(path),"exec"),namespace)
    class GeometryPeer(namespace["Peer"]):
        def _on_naws(self):
            grid=[self.state["columns"],self.state["rows"]]
            history=self.state["naws_history"]
            require(len(history)<64,"NAWS event bound")
            history.append(grid);self._record()
            if self.state.get("frozen_grid"):
                require(grid==self.state["frozen_grid"] or self.state["resize_armed"],"Unarmed NAWS")
                require(grid[0]==91 and 1<=grid[1]<=200,"Single-axis NAWS bound")
        def _geometry_gate(self):
            grid=[self.state["columns"],self.state["rows"]];count=len(self.state["naws_history"])
            ready=self.directory/"fixture-ready"
            if self.state.get("frozen_grid") is None:
                if not ready.exists():return
                require(json.loads(ready.read_text())=={"grid":grid,"event_count":count} and grid==[91,20],"Initial geometry acknowledgment")
                self.state.update(frozen_grid=grid,freeze_event_count=count,phase=0)
                self._record()
            request=self.directory/"resize-request-1"
            if self.state["phase"]==0 and not self.state["resize_armed"] and request.exists():
                require((self.directory/"ack-0").read_text()=="observed","Initial snapshot not acknowledged")
                require(json.loads(request.read_text())=={"phase":1,"window":[1360,569]},"Fixed window request")
                self.state.update(resize_armed=True,before_event_count=count);self._record()
            ready=self.directory/"geometry-ready-1"
            if self.state["resize_armed"] and ready.exists():
                require(json.loads(ready.read_text())=={"phase":1,"grid":grid,"event_count":count},"Stale geometry acknowledgment")
                require(count>=self.state["before_event_count"] and grid[0]==91 and 1<=grid[1]<=200,"Final grid bounds")
                require((self.directory/"ack-1").read_text()=="observed","Final snapshot not acknowledged")
                self.state.update(resize_armed=False,after_event_count=count,frozen_grid=grid,phase=1,complete=True);self._record()
    return GeometryPeer


def source_identity():return common.source_identity(root=ROOT,source_files=SOURCE_FILES)


def controller_contract(case=CASE_ID):
    validate_case(case)
    return {"case_id":case,"windows":[list(v) for v in WINDOWS],"initial_grid":[91,20],"maximum_snapshots":96,
            "widget_errors":list(WIDGET_ERRORS),"error_codes":list(ERROR_CODES)}


def probe_fragment(case=CASE_ID):
    source=(ROOT/"research/ui/resize_geometry_reference.js").read_text(encoding="utf-8")
    require(source.count("__RESIZE_GEOMETRY_CASE_JSON__")==1,"Controller anchor")
    return source.replace("__RESIZE_GEOMETRY_CASE_JSON__",json.dumps(controller_contract(case)))


def launcher_source(case=CASE_ID):
    validate_case(case)
    source=(ROOT/"research/ui/r1_launch.py").read_text(encoding="utf-8")
    changes={"from research.ui.r1_vt_fixture import Peer":"from research.ui.resize_geometry_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
      'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")':"probe_fragment("+repr(case)+")"}
    for old,new in changes.items():
        require(source.count(old)==1,"Protected launcher anchor");source=source.replace(old,new)
    return source


def fixed_contract(case=CASE_ID):
    validate_case(case)
    return {"schema":"termforge-resize-geometry-plan-v1","issue":29,"related_issue":27,"case_id":case,
      "maximum_original_runs":1,"actual_original_runs":0,"phase_count":2,"windows":[list(v) for v in WINDOWS],
      "initial_grid":[91,20],"required_final_rows":None,"terminal_input_bytes":0,"DSR_requests":0,"semantic_acceptance":False,
      "reference_exe_sha256":EXE_SHA256,"reference_files_manifest_sha256":MANIFEST_SHA256,"controller_contract":controller_contract(case),
      "rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,
      "scope":"One fixed requested-height change; record actual window/NAWS/capacity before judging; optional widget geometry never destroys independent observations",
      "stop":"No calibration/retry/second target/restore; preserve measurements on fixed safety or deadline failure"}


def bounded_int(value,low,high):
    return type(value) is int and low<=value<=high


def validate_snapshot(value,index):
    require(type(value) is dict and bounded_int(value.get("sequence"),0,95) and value["sequence"]==index and bounded_int(value.get("phase"),0,1),"Snapshot identity")
    phase=value["phase"]
    require(value.get("requested_window")==list(WINDOWS[phase]) and bounded_int(value.get("elapsed_ms"),0,42000),"Snapshot request/time")
    require(value.get("reason") in ("initial-stable","request-returned","naws-change","final-stable","fault"),"Snapshot reason")
    window=value["window"]
    require(window.get("error") in (None,"window_read_failed"),"Window error code")
    if window["error"] is None:
        size=qrect_size(window["rect"]);require(size==window["size"] and all(1<=n<=4096 for n in size),"Window size bound")
    else:require(window["rect"] is None and window["size"] is None,"Unavailable window shape")
    peer=value["peer"]
    require(bounded_int(peer.get("event_count"),0,64) and type(peer.get("resize_armed")) is bool and peer.get("error") in (None,"OSError","RuntimeError","ValueError"),"Peer observation")
    if peer["grid"] is not None:require(type(peer["grid"]) is list and len(peer["grid"])==2 and all(bounded_int(n,0,65535) for n in peer["grid"]),"Observed NAWS bound")
    terminal=value["terminal"]
    require(terminal.get("error") in (None,"terminal_read_failed"),"Terminal error code")
    require(terminal.get("connected") is None or type(terminal["connected"]) is bool,"Connection scalar")
    require(terminal.get("protocol") in (None,"Telnet"),"Protocol scalar")
    for key in ("capacity","line_count"):
        require(terminal.get(key) is None or bounded_int(terminal[key],0,200),"Terminal dimension bound")
    widgets=value["widgets"]
    roles=widgets.get("roles")
    require(type(roles) is dict and set(roles)=={"terminal","editor","viewport"},"Widget roles")
    if widgets.get("status")=="unavailable":
        require(widgets.get("error") in WIDGET_ERRORS and widgets.get("nodes")==[] and all(v is None for v in roles.values()),"Optional unavailable widget shape")
    else:
        require(widgets.get("status")=="observed" and widgets.get("error") is None and type(widgets.get("nodes")) is list and 3<=len(widgets["nodes"])<=64,"Widget observation bound")
        nodes=widgets["nodes"];ids=[n["id"] for n in nodes]
        require(ids==list(range(len(nodes))) and all(bounded_int(v,0,len(nodes)-1) for v in roles.values()) and len(set(roles.values()))==3,"Widget identity")
        for n in nodes:
            require(n["parent_id"] is None or bounded_int(n["parent_id"],0,len(nodes)-1) and n["parent_id"]!=n["id"],"Widget parent")
            require(n["role"] in ("terminal","editor","viewport","ancestor","splitter-child") and all(type(n[k]) is bool for k in ("is_splitter","is_handle","visible")),"Widget role/types")
            require(qrect_size(n["rect"])==n["size"] and all(1<=v<=4096 for v in n["size"]),"Widget dimensions")
            require(type(n["global_origin"]) is list and len(n["global_origin"])==2 and all(bounded_int(v,-1000000,1000000) for v in n["global_origin"]),"Widget origin")
        for role,index in roles.items():require(nodes[index]["role"]==role,"Widget role binding")
        for n in nodes:
            seen=set();current=n
            while current["parent_id"] is not None:
                require(current["id"] not in seen,"Widget parent cycle");seen.add(current["id"]);current=nodes[current["parent_id"]]


def audit_mechanics(directory,case=CASE_ID):
    validate_case(case)
    read=lambda name:json.loads((directory/name).read_text(encoding="utf-8"))
    peer,result,cleanup,profile=[read(n) for n in ("peer.json",RESULT_FILE,"cleanup.json","profile-check.json")]
    require((directory/"stderr.txt").read_bytes()==b"" and not (directory/"launcher-failure.json").exists(),"Launcher failure")
    require(result.get("schema")=="termforge-resize-geometry-result-v1" and result.get("status")=="observation" and result.get("error") is None and result.get("case_id")==case and result.get("semantic_acceptance") is False and result.get("terminal_input_bytes")==0,"Complete geometry observation required")
    require(result.get("requested_windows")==[list(v) for v in WINDOWS],"Fixed requests")
    snapshots=result["snapshots"];observations=result["observations"]
    require(type(snapshots) is list and 3<=len(snapshots)<=96 and len(observations)==2,"Geometry snapshot count")
    for index,value in enumerate(snapshots):validate_snapshot(value,index)
    require([v["elapsed_ms"] for v in snapshots]==sorted(v["elapsed_ms"] for v in snapshots),"Snapshot time order")
    require([v["phase"] for v in snapshots]==sorted(v["phase"] for v in snapshots),"Snapshot phase order")
    require(observations[0] in snapshots and observations[1] in snapshots and [v["reason"] for v in observations]==["initial-stable","final-stable"] and [v["phase"] for v in observations]==[0,1],"Stable observations")
    first,last=observations
    require(first["window"]["size"]==[1360,749] and first["peer"]["grid"]==[91,20] and first["terminal"]["capacity"]==20,"Initial baseline")
    for v in observations:
        require(v["window"]["error"] is None and v["terminal"]["error"] is None and v["terminal"]["connected"] is True and v["terminal"]["protocol"]=="Telnet" and v["peer"]["grid"][0]==91 and 1<=v["peer"]["grid"][1]<=200 and v["terminal"]["capacity"]==v["peer"]["grid"][1],"Stable geometry fields")
    require(type(result.get("widget_geometry_complete")) is bool and result["widget_geometry_complete"]==all(v["widgets"]["status"]=="observed" for v in observations),"Widget completeness flag")
    require(peer.get("connected") is True and peer.get("closed") is True and peer.get("complete") is True and not peer.get("error") and peer.get("received_hex")=="" and type(peer.get("application_sent_bytes")) is int and peer["application_sent_bytes"]==0 and peer.get("sent_sha256") is None,"No application input peer")
    history=peer["naws_history"]
    require(1<=len(history)<=64 and all(type(v) is list and len(v)==2 and v[0]==91 and bounded_int(v[1],1,200) for v in history),"NAWS history")
    require(peer["case_id"]==case and peer["phase"]==1 and peer["resize_armed"] is False and peer["frozen_grid"]==last["peer"]["grid"],"Peer terminal state")
    before,after=peer["before_event_count"],peer["after_event_count"]
    require(1<=peer["freeze_event_count"]<=before<=after==len(history) and history[peer["freeze_event_count"]-1]==[91,20] and history[-1]==last["peer"]["grid"] and all(v==[91,20] for v in history[peer["freeze_event_count"]-1:before]),"Armed boundary")
    require(all(bounded_int(peer[k],1,len(history)) for k in ("freeze_event_count","before_event_count","after_event_count")),"Event counts")
    require(first["peer"]["event_count"]==peer["freeze_event_count"] and last["peer"]["event_count"]==after,"Stable event counts")
    for value in snapshots:
        count=value["peer"]["event_count"]
        require(count<=len(history) and (value["peer"]["grid"]==history[count-1] if count else value["peer"]["grid"] is None),"Snapshot NAWS binding")
    requests=[v for v in snapshots if v["reason"]=="request-returned"]
    require(len(requests)==1 and requests[0]["phase"]==1 and first["sequence"]<requests[0]["sequence"]<last["sequence"] and last["elapsed_ms"]-requests[0]["elapsed_ms"]>=600,"Single request/stable time")
    require(read("fixture-ready")=={"grid":[91,20],"event_count":peer["freeze_event_count"]} and read("resize-request-1")=={"phase":1,"window":[1360,569]} and read("geometry-ready-1")=={"phase":1,"grid":last["peer"]["grid"],"event_count":after},"Geometry acknowledgments")
    require(all((directory/f"ack-{i}").read_text()=="observed" for i in (0,1)),"Observation ack")
    require(cleanup.get("active")==0 and type(cleanup.get("active")) is int and all(bounded_int(cleanup.get(k),0,100) for k in ("total","terminated")) and profile=={"only_synthetic_session":True,"session_count":1},"Cleanup/profile")
    return {"schema":"termforge-resize-geometry-mechanics-v1","case_id":case,"observed_phases":2,"snapshot_count":len(snapshots),"terminal_input_bytes":0,"DSR_requests":0,"semantic_acceptance":False,"widget_geometry_complete":result["widget_geometry_complete"],"job_active":0}


def prepare_plan(source, out, rea, destination, case, execution_authorized=False):
    validate_case(case)
    source, out, rea, destination = [Path(path).resolve() for path in (source, out, rea, destination)]
    require(type(execution_authorized) is bool, "Execution authorization must be boolean")
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out != ROOT / "local" and out.is_relative_to(ROOT / "local") and destination.is_relative_to(ROOT / "local"),
            "Plan/output must belong to this worktree local directory")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    plan = {**fixed_contract(case), **source_identity(), **common.host_identity(), "execution_authorized": execution_authorized,
            "source": str(source), "out": str(out), "rea": str(rea), "rea_prepared_identity": common.rea_identity(rea),
            "launcher_source_sha256": hashlib.sha256(launcher_source(case).encode()).hexdigest()}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(plan, indent=2)+"\n")
    return plan


def run_plan(path):
    path = Path(path).resolve(); plan = json.loads(path.read_text(encoding="utf-8"))
    require(plan.get("execution_authorized") is True, "Offline proposal only; execution is not authorized")
    case = validate_case(plan.get("case_id"))
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed geometry contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Geometry source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Geometry run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_geometry_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Geometry source identity changed during run")
    mechanics = audit_mechanics(out, case)
    (out / "mechanics-check.json").write_text(json.dumps(mechanics, indent=2)+"\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--case", choices=list(CASES))
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--"+name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea and args.case, "Source/output/REA/case required")
        prepare_plan(args.source, args.out, args.rea, args.plan, args.case, args.authorize_execution)
        print(json.dumps({"prepared": True, "case_id": args.case, "phase_count": len(phases(args.case)), "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution and args.case is None and args.source is None and args.out is None and args.rea is None,
                "Execution uses only the immutable plan")
        run_plan(args.plan)
