"""Independent raw-capture-v1; classification is offline and never advances the source."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.alternate_style_reference import peer_class, require

MODULE_SHA256={
    "WindTerm.exe":"7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0",
    "Qt5Core.dll":"8d2ff4ce9096ddccc4f4cd62c2e41fc854cfd1b0d6e8d296645a7f5fd4ae565a",
    "Qt5Gui.dll":"5e7d2d41b8b92a880e83b8cc0ca173f5da61218604186196787ee1600956be1e",
    "Qt5Widgets.dll":"3788c669d4b645e5a576de9fc77fca776bf516d43c89143dc2ca28291ba14358",
}
SOURCE_FILES=("research/ui/raster_raw_reference.py","research/ui/raster_raw_reference.js","research/ui/alternate_style_reference.py","research/ui/alternate_reference.py","research/ui/r1_launch.py","research/ui/launch.py","research/ui/qt_r1_text.js")
MANIFEST_SHA256="e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"


def phases(rows,columns):
    require(10<=rows<=60 and 40<=columns<=160,"Controlled grid out of bounds")
    return [
        ("default-and-main-calibration",b"\x1b[0m\x1b[2J\x1b[H\x1b[2;3HD0 H H\x1b[4;3H\x1b[31;44mM0 H H"),
        ("alternate-entry-with-controls",b"\x1b[?1049hA0 H\x1b[31;44m A0S1 H\x1b[0m A0D0 H"),
        ("alternate-reference",b"\x1b[4;5H\x1b[32;45mA1 H H"),
        ("alternate-repeat-with-controls",b"\x1b[?1049hA2 H\x1b[0m A2D0 H\x1b[31;44m A2S1 H\x1b[32;45m A2S2 H"),
        ("main-return-with-controls",b"\x1b[?1049l M1 H\x1b[31;44m M1S1 H\x1b[32;45m M1S2 H\x1b[0m M1D0 H"),
    ]


_BasePeer=peer_class()
_BasePeer._serve.__globals__["phases"]=phases


class Peer(_BasePeer):
    def _record(self):
        if self.state.get("frozen_grid") and "freeze_event_count" not in self.state:
            self.state["freeze_event_count"]=len(self.state.get("naws_history",[]))
        super()._record()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_full_hashes(directory,identities=MODULE_SHA256):
    measured={name:sha(directory/name) for name in identities}
    require(measured==identities,"Pinned executable or Qt DLL full hash differs")
    return measured


def prepare_plan(source,out,rea,destination,execution_authorized=False):
    source,out,rea,destination=[path.resolve() for path in (source,out,rea,destination)]
    require(not out.exists() and not destination.exists(),"Plan/output must be new")
    require(out.is_relative_to(ROOT/"local") and destination.is_relative_to(ROOT/"local"),"Plan/output must belong to this worktree local directory")
    require(rea.is_file(),"Locked REA CLI is missing")
    package=rea.parent.parent/"rea-agents/package.json"
    require(json.loads(package.read_text(encoding="utf-8"))["version"]=="6.7.0","REA version differs")
    measured=check_full_hashes(source)
    plan={"schema":"termforge-raster-raw-plan-v1","issue":129,"case_id":"raw-capture-v1","maximum_original_runs":1,"execution_authorized":execution_authorized,
          "source_base_commit":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
          "source":str(source),"out":str(out),"rea":str(rea),"module_sha256":measured,"files_manifest_sha256":MANIFEST_SHA256,
          "rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,
          "source_files_sha256":{name:sha(ROOT/name) for name in SOURCE_FILES},
          "phases":[{"phase":index,"label":label,"source_hex":content.hex(),"source_bytes":len(content),"source_sha256":hashlib.sha256(content).hexdigest(),"dsr_suffix_hex":"1b5b366e"} for index,(label,content) in enumerate(phases(25,91))],
          "phase_count":5,"maximum_grabs_per_phase":1,"maximum_grabs":5,
          "scope":"Five-stage raw H/space capture before offline histogram/signature analysis; no internal pen, presented or performance claim",
          "selection_guard":"Known nonnegative safe-integer selectionCaretP equals selectionAnchorP; selectionText is retained only as type/length",
          "stop":"Structure, identity, empty image, DPR/coordinates, endpoints or caret failure stops; color ties, nonuniform backgrounds and missing ink stay offline unknown and do not stop the next phase; no shifted ROI, extra grab or retry"}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(plan,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"prepared":True,"phase_count":5,"maximum_original_runs":1,"plan":destination.as_posix()}))


def run_plan(plan_path):
    plan_path=plan_path.resolve()
    plan=json.loads(plan_path.read_text(encoding="utf-8"))
    require(plan["issue"]==129 and plan.get("case_id")=="raw-capture-v1" and plan["maximum_original_runs"]==1,"Unexpected raster plan")
    require(plan.get("execution_authorized") is True,"Offline proposal only; new original execution has not been authorized")
    require(plan["source_base_commit"]==subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),"Source commit changed after plan")
    require(plan["module_sha256"]==MODULE_SHA256,"Plan module identity differs")
    require(plan["source_files_sha256"]=={name:sha(ROOT/name) for name in SOURCE_FILES},"Probe source changed after plan")
    require(plan["phases"]==[{"phase":index,"label":label,"source_hex":content.hex(),"source_bytes":len(content),"source_sha256":hashlib.sha256(content).hexdigest(),"dsr_suffix_hex":"1b5b366e"} for index,(label,content) in enumerate(phases(25,91))],"Plan source differs")
    source,out,rea=[Path(plan[key]) for key in ("source","out","rea")]
    check_full_hashes(source)
    require(not out.exists(),"This run output already exists")
    # Exclusive marker is deliberately retained after every attempted launch.
    with plan_path.with_suffix(".attempted").open("x",encoding="utf-8") as marker:
        marker.write("One authorized original launch attempt; never retry this plan.\n")
    from research.ui import r1_launch
    launcher=Path(r1_launch.__file__)
    code=launcher.read_text(encoding="utf-8")
    replacements={"from research.ui.r1_vt_fixture import Peer":"from research.ui.raster_raw_reference import Peer",
                  'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")':'Path(__file__).with_name("raster_raw_reference.js").read_text(encoding="utf-8")'}
    for old,new in replacements.items():
        require(code.count(old)==1,"Protected launcher source changed")
        code=code.replace(old,new)
    namespace={"__file__":str(launcher),"__name__":"termforge_raster_launcher"}
    exec(compile(code,str(launcher),"exec"),namespace)
    namespace["run"](source,out,rea,45,False,"menu-wrap")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--plan",type=Path,required=True)
    parser.add_argument("--prepare",action="store_true")
    parser.add_argument("--authorize-execution",action="store_true",help="Use only after a separate explicit authorization of this new run")
    parser.add_argument("--source",type=Path)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--rea",type=Path)
    args=parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea,"Source/output/REA required")
        prepare_plan(args.source,args.out,args.rea,args.plan,args.authorize_execution)
    else:
        run_plan(args.plan)
