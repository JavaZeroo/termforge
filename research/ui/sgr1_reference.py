"""One fixed SGR1 output-signature case using the immutable raw Qt capture boundary."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.alternate_style_reference import peer_class, require
from research.ui.raster_raw_reference import MODULE_SHA256, MANIFEST_SHA256, check_full_hashes

CASE_ID = "sgr1-capture-v1"
SOURCE_FILES = ("research/ui/sgr1_reference.py", "research/ui/raster_raw_reference.py",
                "research/ui/raster_raw_reference.js", "research/ui/alternate_style_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py",
                "research/ui/launch.py", "research/ui/qt_r1_text.js")
AUDIT_FILES = ("research/ui/sgr1_analysis.py", "research/ui/export_sgr1_reference.py",
               "research/ui/raster_raw_analysis.py", "research/ui/raster_selection_analysis.py",
               "research/ui/export_raster_raw_reference.py")


def pair(label, group):
    return [{"label": label, "offset": len(label)-3+2*sample, "group": group, "sample": sample}
            for sample in (0, 1)]


TARGETS = [{"label": "main SGR0/1 controls", "markers": pair("N0 H H", "control-normal") + pair("B0 H H", "control-sgr1")}]
for title, sample, normal, heavy in (("entry SGR1 signature", "E0 H H", "EN H H", "EB H H"),
                                    ("known alternate SGR1 signature", "A0 H H", "AN H H", "AB H H"),
                                    ("repeat SGR1 signature", "R0 H H", "RN H H", "RB H H"),
                                    ("return SGR1 signature", "M1 H H", "MN H H", "MB H H")):
    TARGETS.append({"label": title, "markers": pair(sample, "sample") + pair(normal, "control-normal") + pair(heavy, "control-sgr1")})


def phases(rows, columns):
    require(10 <= rows <= 60 and 40 <= columns <= 160, "Controlled grid out of bounds")
    return [
        ("main-controls-end-sgr1", b"\x1b[0m\x1b[2J\x1b[H\x1b[2;3HN0 H H \x1b[1mB0 H H"),
        ("1049-entry-with-controls", b"\x1b[?1049hE0 H H \x1b[0mEN H H \x1b[1mEB H H"),
        ("alternate-end-sgr1", b"\x1b[4;3H\x1b[0mA0 H H AN H H \x1b[1mAB H H"),
        ("1049-repeat-with-controls-end-sgr0", b"\x1b[?1049hR0 H H \x1b[0mRN H H \x1b[1mRB H H\x1b[0m"),
        ("1049-return-with-controls", b"\x1b[?1049l M1 H H \x1b[0mMN H H \x1b[1mMB H H"),
    ]


def build_probe_fragment():
    """Keep the reviewed native ABI/lifecycle body; substitute only fixed case data and names."""
    source = (ROOT / "research/ui/raster_raw_reference.js").read_text(encoding="utf-8")
    start, end = "const started=Date.now(),captures=[],grabs=[],targets=[", ";\nfunction modulePathMatches"
    require(source.count(start) == source.count(end) == 1, "Protected capture descriptor boundary changed")
    left, right = source.index(start), source.index(end)
    source = source[:left] + "const started=Date.now(),captures=[],grabs=[],targets=" + json.dumps(TARGETS, separators=(",", ":")) + source[right:]
    for old, new, count in (("raster-raw-result.json", "sgr1-raster-result.json", 1),
                            ("raw_raster_result", "sgr1_raster_result", 1), ("raw_raster_phase", "sgr1_raster_phase", 1),
                            ("five-stage-raw-raster-capture-v1", "five-stage-sgr1-capture-v1", 1),
                            ("captured-five-raw-raster-phases", "captured-five-sgr1-raster-phases", 1),
                            ("partial-raw-raster-capture", "partial-sgr1-raster-capture", 3)):
        require(source.count(old) == count, "Protected capture case names changed")
        source = source.replace(old, new)
    return source


_BasePeer = peer_class()  # Its independent namespace does not alter old peer classes.
_BasePeer._serve.__globals__["phases"] = phases


class Peer(_BasePeer):
    def _record(self):
        if self.state.get("frozen_grid") and "freeze_event_count" not in self.state:
            self.state["freeze_event_count"] = len(self.state.get("naws_history", []))
        super()._record()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rea_identity(rea):
    package = rea.parent.parent / "rea-agents/package.json"
    metadata = json.loads(package.read_text(encoding="utf-8"))
    require(rea.is_file() and metadata["version"] == "6.7.0" and metadata["bin"]["rea"] == "scripts/rea.mjs", "REA version/entry differs")
    return {"version": "6.7.0", "shim_sha256": sha(rea), "package_json_sha256": sha(package),
            "entry_sha256": sha(package.parent / "scripts/rea.mjs")}


def phase_records(rows=25, columns=91):
    return [{"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content),
             "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, content) in enumerate(phases(rows, columns))]


def prepare_plan(source, out, rea, destination, execution_authorized=False):
    source, out, rea, destination = [path.resolve() for path in (source, out, rea, destination)]
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out.is_relative_to(ROOT / "local") and destination.is_relative_to(ROOT / "local"), "Plan/output must belong to this worktree local directory")
    tool_identity = rea_identity(rea)
    plan = {"schema": "termforge-sgr1-plan-v1", "issue": 146, "case_id": CASE_ID,
            "maximum_original_runs": 1, "actual_original_runs": 0, "execution_authorized": execution_authorized,
            "source_base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "source": str(source), "out": str(out), "rea": str(rea),
            "rea_prepared_identity": tool_identity,
            "module_sha256": check_full_hashes(source), "files_manifest_sha256": MANIFEST_SHA256,
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "source_files_sha256": {name: sha(ROOT / name) for name in SOURCE_FILES},
            "offline_audit_files_sha256": {name: sha(ROOT / name) for name in AUDIT_FILES},
            "generated_capture_sha256": hashlib.sha256(build_probe_fragment().encode("utf-8")).hexdigest(),
            "phases": phase_records(), "phase_count": 5, "maximum_grabs_per_phase": 1, "maximum_grabs": 5,
            "scope": "Exact same-frame SGR0/SGR1 output signatures only; no internal pen, general bold, full-grid, local-shell, presented or performance claim",
            "stop": "Structural/identity/empty-image/ROI/DPR/selection/caret failures stop. Equal/inconsistent controls or incomparable shapes leave output classification unknown; color differences/ties leave the font-weight mechanism unknown, even when the exact output uniquely matches. No moved ROI, extra grab or retry."}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8", newline="\n")
    return plan


def run_plan(path):
    path = path.resolve()
    plan = json.loads(path.read_text(encoding="utf-8"))
    require(plan["issue"] == 146 and plan.get("case_id") == CASE_ID and plan["maximum_original_runs"] == 1, "Unexpected SGR1 plan")
    require(plan.get("execution_authorized") is True, "Offline proposal only; new original execution has not been authorized")
    require(plan["source_base_commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(), "Source commit changed after plan")
    require(plan["module_sha256"] == MODULE_SHA256 and plan["files_manifest_sha256"] == MANIFEST_SHA256, "Plan module/package identity differs")
    require(plan["source_files_sha256"] == {name: sha(ROOT / name) for name in SOURCE_FILES}, "Probe source changed after plan")
    require(plan["offline_audit_files_sha256"] == {name: sha(ROOT / name) for name in AUDIT_FILES}, "Offline audit source changed after plan")
    require(plan["generated_capture_sha256"] == hashlib.sha256(build_probe_fragment().encode()).hexdigest(), "Generated capture differs")
    require(plan["phases"] == phase_records(), "Plan source differs")
    require((plan["phase_count"], plan["maximum_grabs_per_phase"], plan["maximum_grabs"], plan["rea_capture_seconds"], plan["cli_wait_timeout_seconds"], plan["whole_lifecycle_bound_seconds"]) == (5, 1, 5, 45, 60, None), "Plan capture bounds differ")
    source, out, rea = [Path(plan[key]).resolve() for key in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out.is_relative_to(ROOT / "local") and not out.exists(), "This run plan/output is not new/local")
    require(plan["rea_prepared_identity"] == rea_identity(rea), "Prepared REA entry changed")
    check_full_hashes(source)
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One authorized original launch attempt; never retry this plan.\n")
    from research.ui import r1_launch
    launcher = Path(r1_launch.__file__)
    code = launcher.read_text(encoding="utf-8")
    replacements = {"from research.ui.r1_vt_fixture import Peer": "from research.ui.sgr1_reference import Peer, build_probe_fragment",
                    'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "build_probe_fragment()"}
    for old, new in replacements.items():
        require(code.count(old) == 1, "Protected launcher source changed")
        code = code.replace(old, new)
    namespace = {"__file__": str(launcher), "__name__": "termforge_sgr1_launcher"}
    exec(compile(code, str(launcher), "exec"), namespace)
    namespace["run"](source, out, rea, 45, False, "menu-wrap")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea, "Source/output/REA required")
        prepare_plan(args.source, args.out, args.rea, args.plan, args.authorize_execution)
        print(json.dumps({"prepared": True, "phase_count": 5, "maximum_original_runs": 1}))
    else:
        run_plan(args.plan)
