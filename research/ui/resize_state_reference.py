"""Three-state original resize observation; prepare is offline, run is explicitly gated."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as common

require, sha = common.require, common.sha
EXE_SHA256, MANIFEST_SHA256 = common.EXE_SHA256, common.MANIFEST_SHA256
WINDOWS = ((1360, 749), (1000, 749), (1360, 749))
SOURCE_FILES = ("research/ui/resize_state_reference.py", "research/ui/resize_state_reference.js",
                "research/ui/mode_state_reference.py", "research/ui/alternate_reference.py",
                "research/ui/r1_launch.py", "research/ui/launch.py", "research/ui/qt_r1_text.js",
                "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")


def phases(rows, columns):
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled initial grid differs")
    esc = b"\x1b["
    source = (esc+b"?6l"+esc+b"?7h"+esc+b"r"+esc+b"0m"+esc+b"2J"+
              esc+b"2;1H0123456789"+esc+b"3;1HANCHOR"+
              esc+b"?7l"+f"\x1b[8;{columns}H".encode()+b"uv"+
              esc+b"?7h"+f"\x1b[10;{columns}H".encode()+b"wx")
    # After the initial text, resize is the only state-changing action. In
    # particular, neither later DSR is preceded by CUP or a new text marker.
    return [("initial-body", source), ("after-width-shrink", b""), ("after-width-restore", b"")]


def phase_records(rows=20, columns=91):
    return [{"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content),
             "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, content) in enumerate(phases(rows, columns))]


def peer_class():
    """Keep the protected Telnet parser/lifecycle; replace only this plan's gates."""
    from research.ui import alternate_reference
    original = Path(alternate_reference.__file__)
    source = original.read_text(encoding="utf-8")
    replacements = {
        'if self.state["rows"] and selected is None:':
            'if self.state["rows"] and selected is None and (self.directory / "fixture-ready").exists():',
        'selected = phases(self.state["rows"])':
            'self._freeze_initial()\n                    selected = phases(self.state["rows"], self.state["columns"])',
        'self.state["rows"] = int.from_bytes(sub[3:5], "big")':
            'self.state["rows"] = int.from_bytes(sub[3:5], "big")\n                                self._on_naws()',
        '                if index >= 0:\n                    matches = list(re.finditer(rb"\\x1b\\[(\\??)(\\d+);(\\d+)R", bytes(received[reply_start:])))\n                    self.state["records"][index]["cursor_reply"] = [int(matches[-1][2]),int(matches[-1][3])] if matches else None':
            '                if index >= 0:\n                    raw = bytes(received[reply_start:])\n                    self.state["records"][index]["response_hex"] = raw.hex()\n                    observed = reply_observation(raw)\n                    if observed is not None:\n                        self.state["records"][index].update(observed)',
        'if selected and (index == -1 or (self.directory / f"ack-{index}").exists()):':
            'if selected and self._phase_ready(index):',
        '"dsr_suffix_hex": "1b5b366e", "cursor_reply": None})':
            '"dsr_suffix_hex": "1b5b366e", "cursor_reply": None, "frozen_grid": list(self.state["frozen_grid"])})',
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected peer source changed")
        source = source.replace(old, new)
    namespace = {"__file__": str(original), "__name__": "termforge_resize_state_peer"}
    exec(compile(source, str(original), "exec"), namespace)
    namespace.update(phases=phases, reply_observation=common.reply_observation)

    class BoundPeer(namespace["Peer"]):
        def _freeze_initial(self):
            grid = [self.state["columns"], self.state["rows"]]
            phases(grid[1], grid[0])
            history = self.state.get("naws_history", [])
            require(history and history[-1] == grid, "Initial NAWS history missing")
            ready = json.loads((self.directory / "fixture-ready").read_text(encoding="utf-8"))
            require(ready == {"grid": grid, "event_count": len(history)}, "Initial stable snapshot is stale")
            self.state.update(frozen_grid=grid, frozen_grids=[grid], freeze_event_counts=[len(history)],
                              resize_armed=None, resize_boundaries=[])

        def _on_naws(self):
            grid = [self.state["columns"], self.state["rows"]]
            history = self.state.setdefault("naws_history", [])
            require(len(history) < 64 and all(type(n) is int and 0 <= n <= 65535 for n in grid), "NAWS event bound")
            history.append(grid)
            self._record()
            if self.state.get("frozen_grid"):
                require(grid[1] == self.state["frozen_grids"][0][1], "Resize changed row count")
                require(self.state.get("resize_armed") is not None or grid == self.state["frozen_grid"],
                        "NAWS changed outside an armed resize")

        def _phase_ready(self, index):
            if index == -1:
                return True
            ack = self.directory / f"ack-{index}"
            if not ack.exists() or self.state["records"][index]["cursor_reply"] is None:
                return False
            require(ack.read_text(encoding="utf-8") == "observed", "UI acknowledgement differs")
            if index == 2:
                return True
            target = index + 1
            request = self.directory / f"resize-request-{target}"
            if not request.exists():
                return False
            require(json.loads(request.read_text(encoding="utf-8")) == {"phase": target, "window": list(WINDOWS[target])},
                    "Resize request differs from immutable window sequence")
            if self.state["resize_armed"] is None:
                self.state["resize_armed"] = target
                self.state["resize_boundaries"].append({"phase": target,
                    "before_event_count": len(self.state["naws_history"]), "requested_window": list(WINDOWS[target])})
                self._record()
                return False
            require(self.state["resize_armed"] == target, "Resize phase differs")
            ready = self.directory / f"resize-ready-{target}"
            if not ready.exists():
                return False
            value = json.loads(ready.read_text(encoding="utf-8"))
            grid, history = [self.state["columns"], self.state["rows"]], self.state["naws_history"]
            require(value == {"phase": target, "grid": grid, "event_count": len(history)}, "Resize ready snapshot is stale")
            before = self.state["resize_boundaries"][-1]["before_event_count"]
            initial = self.state["frozen_grids"][0]
            require(len(history) > before and history[-1] == grid and grid[1] == initial[1], "Resize NAWS boundary missing")
            require((40 <= grid[0] < initial[0]) if target == 1 else grid[0] == initial[0],
                    "Resize did not shrink/restore the initial columns")
            self.state["resize_boundaries"][-1]["after_event_count"] = len(history)
            self.state["frozen_grid"] = grid
            self.state["frozen_grids"].append(grid)
            self.state["freeze_event_counts"].append(len(history))
            self.state["resize_armed"] = None
            return True
    return BoundPeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def launcher_source():
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_state_reference import peer_class\n        Peer = peer_class()",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")':
            'Path(__file__).with_name("resize_state_reference.js").read_text(encoding="utf-8")',
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract():
    return {"schema": "termforge-resize-state-plan-v1", "issue": 29, "maximum_original_runs": 1,
            "actual_original_runs": 0, "phase_count": 3, "windows": [list(v) for v in WINDOWS],
            "window_rect_format": "qt-qrect-inclusive-ltrb",
            "window_sequence_reason": "A prior fresh attempt requested 1360x860 but measured 1360x749 and stopped before source phase 0. This new plan fixes height at the observed 749 and varies only width; exact geometry checks remain required.",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "example_grid": [91, 20], "example_phases": phase_records(),
            "reference_exe_sha256": EXE_SHA256, "reference_files_manifest_sha256": MANIFEST_SHA256,
            "semantic_acceptance": False, "post_initial_terminal_input": "DSR only; no CUP, redraw or replacement text",
            "scope": "Original Telnet, public Qt resize, DSR and marker rectangles; no ConPTY or full active grid"}


def qrect_size(value):
    """Decode the protected Qt5 helper's inclusive left/top/right/bottom fields."""
    require(type(value) is list and len(value) == 4 and all(type(n) is int for n in value) and
            value[2] >= value[0] and value[3] >= value[1], "Invalid public window QRect")
    return [value[2] - value[0] + 1, value[3] - value[1] + 1]


def audit_mechanics(directory):
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in
        ("peer.json", "resize-state-result.json", "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error"),
            "Resize peer did not complete cleanly")
    require(result.get("status") == "observed-three-resize-states" and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and
            result.get("presented_done") is None and result.get("full_active_grid_verified") is False and
            result.get("local_shell_transport_verified") is False, "Resize UI observation incomplete")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(k)) is int and cleanup[k] >= 0 for k in ("total", "terminated")) and
            profile.get("only_synthetic_session") is True and profile.get("session_count") == 1,
            "Owned cleanup/profile differs")
    grids, freezes, history = peer["frozen_grids"], peer["freeze_event_counts"], peer["naws_history"]
    require(type(grids) is list and len(grids) == 3 and all(type(grid) is list and len(grid) == 2 and
            all(type(n) is int and 0 <= n <= 65535 for n in grid) for grid in grids), "Frozen grid shape differs")
    require(type(history) is list and 1 <= len(history) <= 64 and all(type(grid) is list and len(grid) == 2 and
            all(type(n) is int and 0 <= n <= 65535 for n in grid) for grid in history) and
            type(freezes) is list and len(freezes) == 3 and all(type(n) is int and 1 <= n <= len(history) for n in freezes),
            "NAWS event/freeze shape differs")
    require(len(grids) == len(freezes) == 3 and grids[0] == grids[2] and
            grids[1][1] == grids[0][1] and 40 <= grids[1][0] < grids[0][0], "Frozen resize grids differ")
    require(1 <= len(history) <= 64 and len(peer["resize_boundaries"]) == 2, "NAWS history incomplete")
    for index, boundary in enumerate(peer["resize_boundaries"], 1):
        before, after = boundary["before_event_count"], boundary["after_event_count"]
        require(boundary["phase"] == index and boundary["requested_window"] == list(WINDOWS[index]) and
                freezes[index-1] <= before < after == freezes[index] <= len(history) and
                all(grid == grids[index-1] for grid in history[freezes[index-1]-1:before]) and
                all(grid[1] == grids[0][1] for grid in history[before:after]) and history[after-1] == grids[index],
                "NAWS resize boundary differs")
    require(all(grid == grids[2] for grid in history[freezes[2]-1:]), "Unarmed final NAWS change retained")
    records, observations = peer["records"], result["observations"]
    require(len(records) == len(observations) == 3 and peer["phase"] == 2 and peer["phase_count"] == 3,
            "Resize phases incomplete")
    replies = bytearray()
    checks = result.get("geometry_checks")
    require(type(checks) is list and len(checks) == 3, "Window geometry checks missing")
    for phase, (check, window) in enumerate(zip(checks, WINDOWS)):
        require(check.get("phase") == phase and check.get("expected_size") == list(window) and
                check.get("window_size") == list(window) and qrect_size(check.get("window_rect")) == list(window),
                "Window geometry check differs")
    for expected, actual, observed, grid, window in zip(phase_records(grids[0][1], grids[0][0]), records, observations, grids, WINDOWS):
        require(all(actual.get(k) == v for k, v in expected.items()) and actual["frozen_grid"] == grid,
                "Resize source/phase geometry differs")
        raw = bytes.fromhex(actual["response_hex"]); reply = common.reply_observation(raw)
        require(reply is not None and all(actual.get(k) == v for k, v in reply.items()), "Resize DSR differs")
        require(all(observed.get(k) == actual[k] for k in ("phase", "label", "frozen_grid", *reply)),
                "Resize peer/UI phase differs")
        terminal = observed["terminal"]
        require(terminal["connected"] is True and terminal["protocol"] == "Telnet" and
                type(terminal["capacity"]) is int and terminal["capacity"] == grid[1] and
                observed.get("window_size") == list(window) and qrect_size(observed.get("window_rect")) == list(window),
                "UI geometry differs")
        require(type(terminal["line_count"]) is int and 0 <= terminal["line_count"] <= 200 and
                type(terminal["lines"]) is list and len(terminal["lines"]) == terminal["line_count"] and
                all(type(line) is str and len(line) <= 1024 for line in terminal["lines"]) and
                type(terminal["markers"]) is list and len(terminal["markers"]) <= 256,
                "Resize document observation bounds differ")
        require((directory / f'ack-{actual["phase"]}').read_text(encoding="utf-8") == "observed", "Resize ack missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-state-mechanics-v1", "stage_count": 3, "dsr_count": 3,
            "frozen_grids": grids, "naws_events": len(history), "job_active": 0,
            "semantic_acceptance": False, "presented_done": None}


def prepare_plan(source, out, rea, destination, execution_authorized=False):
    source, out, rea, destination = [path.resolve() for path in (source, out, rea, destination)]
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out != ROOT / "local" and out.is_relative_to(ROOT / "local") and destination.is_relative_to(ROOT / "local"),
            "Plan/output must belong to this worktree local directory")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    plan = {**fixed_contract(), **source_identity(), **common.host_identity(), "execution_authorized": execution_authorized,
            "source": str(source), "out": str(out), "rea": str(rea), "rea_prepared_identity": common.rea_identity(rea),
            "launcher_source_sha256": hashlib.sha256(launcher_source().encode()).hexdigest()}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(plan, indent=2) + "\n")
    return plan


def run_plan(path):
    path = path.resolve(); plan = json.loads(path.read_text(encoding="utf-8"))
    require(plan.get("execution_authorized") is True, "Offline proposal only; execution is not authorized")
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract().items()), "Fixed resize contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Resize source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source().encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Resize run plan/output is not new/local")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One original resize launch attempt; preserve failures and never retry this plan.\n")
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_state_launcher"}
    exec(compile(launcher_source(), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"],
                                           str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Resize source identity changed during run")
    mechanics = audit_mechanics(out)
    (out / "mechanics-check.json").write_text(json.dumps(mechanics, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea, "Source/output/REA required")
        prepare_plan(args.source, args.out, args.rea, args.plan, args.authorize_execution)
        print(json.dumps({"prepared": True, "phase_count": 3, "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution and args.source is None and args.out is None and args.rea is None,
                "Execution uses only the immutable plan")
        run_plan(args.plan)
