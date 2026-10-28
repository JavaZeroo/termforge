"""One bounded original-terminal style observation, with owned Qt paint hooks."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def phases(rows: int, columns: int):
    esc = b"\x1b["
    cup = lambda row, column: f"\x1b[{row};{column}H".encode("ascii")
    return [
        ("main-red-blue", esc+b"0m"+esc+b"2J"+esc+b"H"+esc+b"?7h"+cup(2,3)+esc+b"31;44mTFM0"),
        ("1049-enter-inherited-style", esc+b"?1049hTFA0"),
        ("alternate-green-magenta", cup(4,5)+esc+b"32;45mTFA1"),
        ("1049-exit-main-style", esc+b"?1049lTFM1"),
        ("1049-second-enter-style", esc+b"?1049hTFA2"),
        ("alternate-four-corners", cup(1,1)+esc+b"33;46mTFC0"+cup(2,1)+b"TFC1"+cup(rows-1,1)+b"TFC2"+cup(rows,columns-3)+b"TFC3"),
        ("1049-duplicate-enter-style", esc+b"?1049hTFA3"),
        ("1049-second-exit-main-style", esc+b"?1049lTFM2"),
        # No inserted DSR between the last-column W and the alternate entry.
        ("main-pending-wrap-enter", cup(3,columns)+esc+b"31;44mW"+esc+b"?1049hTFA4"),
        # No inserted DSR between the alternate exit and the following X.
        ("main-pending-wrap-exit-write", esc+b"?1049lX"),
        ("main-direct-wrap-control", cup(6,columns)+esc+b"31;44mVZ"),
    ]


def peer_class():
    """Reuse the fixed Telnet parser; delay source freezing until UI readiness."""
    from research.ui import alternate_reference
    original = Path(alternate_reference.__file__)
    source = original.read_text(encoding="utf-8")
    replacements = {
        'if self.state["rows"] and selected is None:': 'if self.state["rows"] and selected is None and (self.directory / "fixture-ready").exists():',
        'selected = phases(self.state["rows"])': 'self.state["frozen_grid"] = [self.state["columns"], self.state["rows"]]\n                    selected = phases(self.state["rows"], self.state["columns"])',
        'if len(received) > 65536:': 'if self.state.get("frozen_grid") and self.state["frozen_grid"] != [self.state["columns"], self.state["rows"]]:\n                    raise RuntimeError("NAWS changed after fixture freeze")\n                if len(received) > 65536:',
        'def _record(self):\n        temporary': 'def _record(self):\n        grid = [self.state["columns"], self.state["rows"]]\n        history = self.state.setdefault("naws_history", [])\n        if grid[0] and (not history or history[-1] != grid):\n            history.append(grid)\n        temporary',
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Existing protected peer source changed")
        source = source.replace(old, new)
    namespace = {"__file__": str(original), "__name__": "termforge_style_peer"}
    exec(compile(source, str(original), "exec"), namespace)
    namespace["phases"] = phases
    return namespace["Peer"]


Peer = peer_class()


def run(source: Path, out: Path, rea: Path):
    from research.ui import r1_launch
    launcher = Path(r1_launch.__file__)
    text = launcher.read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer": "from research.ui.alternate_style_reference import Peer",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': 'Path(__file__).with_name("alternate_style_reference.js").read_text(encoding="utf-8")',
    }
    for old, new in replacements.items():
        require(text.count(old) == 1, "Existing protected launcher source changed")
        text = text.replace(old, new)
    namespace = {"__file__": str(launcher), "__name__": "termforge_style_launcher"}
    exec(compile(text, str(launcher), "exec"), namespace)
    namespace["run"](source, out, rea, 45, False, "menu-wrap")


def write_plan(destination: Path):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    plan = {
        "issue": 123, "maximum_original_runs": 1,
        "reference_exe_sha256": "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0",
        "reference_files_manifest_sha256": "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203",
        "source_base_commit": subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60,
        "whole_lifecycle_bound_seconds": None,
        "route": "Original terminal, owned loopback Telnet, isolated station/desktop/profile and OwnedJob",
        "source_freeze": "UI confirms stable NAWS/capacity before releasing the peer; any later NAWS change fails the run",
        "phase_count": len(phases(25,91)),
        "example_grid": [91,25],
        "example_sources": [{"phase":index,"label":label,"hex":content.hex(),"sha256":hashlib.sha256(content).hexdigest()} for index,(label,content) in enumerate(phases(25,91))],
        "source_suffix": "Each phase is followed by CSI 6n and an independent UI acknowledgment",
        "probe_files_sha256": {name:sha(ROOT/name) for name in ("research/ui/alternate_style_reference.py","research/ui/alternate_style_reference.js","research/ui/alternate_reference.py","research/ui/r1_launch.py","research/ui/qt_r1_text.js")},
        "actions": ["Open only synthetic loopback session on owned non-input station", "Call isAltScreen/isMainScreen and record actual returns", "Observe known-marker drawText and same-device fillRect colors using verified Qt5 exports", "Request an owned viewport update to collect paint calls", "Record source, DSR, document projection and marker rectangles", "Stop on failure and reclaim only owned resources; no retry"],
        "limitations": ["QPainter calls are correctness evidence, not pixels or presented frames", "Unknown/unmatched paint paths remain null", "No clipboard, user desktop, personal profile or DPI changes", "No inserted DSR between pending W/1049h or 1049l/X", "45 is capture duration; CLI wait is60 and target starts before the separate2-second delay; no whole-lifecycle45-second guarantee", "Complete clear-grid/pending-wrap equivalence requires evidence, not assumed standards"],
    }
    destination.parent.mkdir(parents=True,exist_ok=True)
    require(not destination.exists(),"Plan output must be new")
    destination.write_text(json.dumps(plan,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"plan":destination.as_posix(),"phases":plan["phase_count"],"maximum_original_runs":1}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan",type=Path)
    parser.add_argument("--source",type=Path)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--rea",type=Path)
    args = parser.parse_args()
    if args.plan:
        write_plan(args.plan)
    else:
        require(args.source and args.out and args.rea,"Source, output and REA are required")
        run(args.source,args.out,args.rea)
