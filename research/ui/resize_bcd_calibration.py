"""Bounded geometry-only calibration, no body or CUP payload.

The initial 80x24 body puts B/C/D at source rows 7/9/10. The fixed 80x24 ->
60x12 resize is followed by a pre-write body snapshot, then one `CSI 12;1H`
RESIZED write and DSR, then a restore snapshot. The direct-Telnet observation
is WindTerm protocol/display behavior, not Windows local-shell/native-grid proof;
preparation never starts the original program.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as common
from research.ui import resize_state_reference as baseline

require, sha = common.require, common.sha
qrect_size = baseline.qrect_size
EXE_SHA256, MANIFEST_SHA256 = common.EXE_SHA256, common.MANIFEST_SHA256

# 80x24 initial body: B/C/D at source rows 7/9/10; the fixed action is CUP 12;1
# RESIZED after a 60x12 shrink, followed by a restore phase.
RESET = b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"

BODY = b""
CASES = {"calibrate": b""}
WINDOWS = ((1228, 839), (988, 509), (988, 509), (1228, 839))
GRIDS = ((80, 24), (60, 12), (60, 12), (80, 24))
RESIZE_PHASES = (1, 3)
# The shared controller writes this file name and message kinds; both probes use
# the BCD-specific qt_r1_text controller.
RESULT_FILE = "bcd-calibration-result.json"
CONTROLLER = "research/ui/resize_bcd_calibration.js"
CONTROLLER_ANCHOR = "__RESIZE_BCD_CALIBRATION_JSON__"
ATTEMPT = "One original BCD pre-write/address observation attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_bcd_calibration.py", CONTROLLER,
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")
BASE_LINES = ("", " " * 79 + "B", " " * 79 + "C", "D")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed BCD-addressing case")
    return case


def phases(case, rows=24, columns=80):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == list(GRIDS[0]), "Fixed initial grid differs")
    return [("initial-80x24", b""), ("shrunk-60x12", b""),
            ("shrunk-60x12-second", b""), ("restored-80x24", b"")]


def phase_records(case, rows=24, columns=80):
    return [{"phase": index, "label": label, "source_hex": value.hex(), "source_bytes": len(value),
             "source_sha256": hashlib.sha256(value).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, value) in enumerate(phases(case, rows, columns))]


def peer_class(case):
    validate_case(case)
    base = baseline.peer_class()
    # baseline creates a fresh module namespace for each class, not a shared peer.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(case, rows, columns)

    class BcdPeer(base):
        """Calibration peer for the dual-axis 80x24 -> 60x12 -> 60x12 -> 80x24 plan.

        The inherited baseline contract freezes the initial row count and only
        allows width shrinks, which contradicts this plan's 24 -> 12 -> 12 -> 24
        rows; the calibration contract below therefore overrides the NAWS gate
        without touching the baseline class. Size changes must follow exactly
        the plan sequence 80x24 -> 60x12 -> 80x24: phases 1/3 are resize events
        and each armed resize must be acknowledged by a new post-request NAWS at
        the target grid (stale or same-size events never count); phase 2 is a
        same-size observation checkpoint with no resize action and no new NAWS
        required, where duplicate same-size events are recorded only and any
        unexpected size change fails. Expectations always come from the fixed
        plan constants, never from observed ranges.
        """

        def _freeze_initial(self):
            super()._freeze_initial()
            require(self.state["frozen_grid"] == list(GRIDS[0]), "Fixed initial grid differs")
            self.state["case_id"] = case
            self.state["initial_grid"] = list(self.state["frozen_grid"])
            self.state["expected_size_change_sequence"] = [list(GRIDS[0]), list(GRIDS[1]), list(GRIDS[3])]
            self.state["phase2_new_naws_required"] = False
            self.state["phase_ready_events"] = []
            self.state.setdefault("naws_size_changes", [])
            self.state["frozen_grid_history"] = self.state["frozen_grids"]

        def _on_naws(self):
            grid = [self.state["columns"], self.state["rows"]]
            history = self.state.setdefault("naws_history", [])
            require(len(history) < 64 and all(type(n) is int and 0 <= n <= 65535 for n in grid), "NAWS event bound")
            history.append(grid)
            self._record()
            changes = self.state.setdefault("naws_size_changes", [])
            if not changes or changes[-1] != grid:
                changes.append(list(grid))
            if self.state.get("frozen_grid"):
                armed = self.state.get("resize_armed")
                if grid == self.state["frozen_grid"]:
                    # Same-size repeat (phase-2 checkpoint or a duplicate during an
                    # armed resize): record only, never a new-size acknowledgement.
                    return
                require(armed is not None, "NAWS changed outside an armed resize")
                require(grid == list(GRIDS[armed]), "Unexpected resized grid")

        def _phase_ready(self, index):
            if index == -1:
                return True
            ack = self.directory / f"ack-{index}"
            if not ack.exists() or self.state["records"][index]["cursor_reply"] is None:
                return False
            require(ack.read_text(encoding="utf-8") == "observed", "UI acknowledgement differs")
            if index == 3:
                # The phase-3 transition was already logged when the restore resize
                # was acknowledged (target == 3 below); this is only the final
                # acceptance of the fourth observation, not a new freeze.
                return True
            target = index + 1
            if target == 2:
                require(self.state["resize_armed"] is None and self.state["frozen_grid"] == list(GRIDS[2]),
                        "Action was not released from the fixed shrunk grid")
                self.state["frozen_grids"].append(list(self.state["frozen_grid"]))
                self.state["freeze_event_counts"].append(len(self.state["naws_history"]))
                self.state["phase_ready_events"].append({"phase": 2, "frozen_grid": list(self.state["frozen_grid"]),
                                                         "event_count": len(self.state["naws_history"]),
                                                         "new_naws_required": False})
                return True
            request = self.directory / f"resize-request-{target}"
            if not request.exists():
                return False
            require(json.loads(request.read_text(encoding="utf-8")) == {"phase": target, "window": list(WINDOWS[target])},
                    "Resize request differs from immutable window sequence")
            if self.state["resize_armed"] is None:
                self.state["resize_armed"] = target
                boundary = {"phase": target, "before_event_count": len(self.state["naws_history"]),
                            "requested_window": list(WINDOWS[target])}
                if target == 3:
                    # Same-size checkpoint observations recorded between the phase-1
                    # freeze and this arming; duplicates only, not acknowledgements.
                    boundary["phase2_observed_naws"] = [list(event) for event in
                                                        self.state["naws_history"][self.state["freeze_event_counts"][1]:]]
                self.state["resize_boundaries"].append(boundary)
                self._record()
                return False
            require(self.state["resize_armed"] == target, "Resize phase differs")
            ready = self.directory / f"resize-ready-{target}"
            if not ready.exists():
                return False
            grid, history = [self.state["columns"], self.state["rows"]], self.state["naws_history"]
            require(json.loads(ready.read_text(encoding="utf-8")) ==
                    {"phase": target, "grid": grid, "event_count": len(history)}, "Resize ready snapshot is stale")
            boundary = self.state["resize_boundaries"][-1]
            require(len(history) > boundary["before_event_count"] and history[-1] == grid == list(GRIDS[target]),
                    "Fixed resized grid or NAWS boundary differs")
            boundary["after_event_count"] = len(history)
            self.state.update(frozen_grid=grid, resize_armed=None)
            self.state["frozen_grids"].append(grid)
            self.state["freeze_event_counts"].append(len(history))
            self.state["phase_ready_events"].append({"phase": target, "frozen_grid": list(grid),
                                                     "event_count": len(history), "new_naws": True})
            return True
    return BcdPeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def probe_fragment(case):
    validate_case(case)
    source = (ROOT / CONTROLLER).read_text(encoding="utf-8")
    require(source.count(CONTROLLER_ANCHOR) == 1, "Protected case template changed")
    return source.replace(CONTROLLER_ANCHOR, json.dumps(case))


def launcher_source(case):
    validate_case(case)
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_bcd_calibration import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-bcd-calibration-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": 4,
            "windows": [list(v) for v in WINDOWS], "required_grids": [list(v) for v in GRIDS],
            "resize_phases": list(RESIZE_PHASES), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "action_phase": 2, "pre_action_phases": [0, 1], "post_initial_terminal_input":
            "DSR at every phase; pre-write body snapshot is phase 1; exactly one declared CUP+RESIZED action after it is acknowledged",
            "scope": "Fresh direct Telnet process per case; WindTerm protocol/display evidence only, not local-shell/native-grid proof; document, DSR and public marker rectangles remain separate",
            "stop": "Wrong fixed geometry/grid, unarmed NAWS, stale snapshots, source/reply/phase mismatch, bounds or deadline failure; no retries"}


def synthetic_lines(case, phase):
    """Finite source-derived text candidates, not predictions of the original policy.

    The initial body plus the single CUP 12;1 RESIZED prefix overwrite on one fixed
    line. The marker is only at column 0 and keeps any tail after its seven bytes.
    """
    validate_case(case)
    require(type(phase) is int and 0 <= phase < 4, "Phase bound")
    originals = set(BASE_LINES)
    if phase < 2:
        return originals
    result = set(originals)
    for line in originals:
        result.add(("RESIZED"+line[7:]) if len(line) >= 7 else "RESIZED")
    return result


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    require(all(type(line) is str and line in synthetic_lines(case, phase) for line in terminal["lines"]),
            "Only fixed action-derived synthetic document lines are allowed")
    require(sum(line.count("RESIZED") for line in terminal["lines"]) <= (0 if phase < 2 else 1),
            "Repeated or premature action marker")


def audit_mechanics(directory, case):
    validate_case(case)
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "BCD peer did not complete cleanly")
    require(result.get("status") == "observed-four-bcd-calibration-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "BCD UI observation incomplete")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(k)) is int and cleanup[k] >= 0 for k in ("total", "terminated")) and
            profile == {"only_synthetic_session": True, "session_count": 1}, "Owned cleanup/profile differs")
    grids, freezes, history = peer["frozen_grids"], peer["freeze_event_counts"], peer["naws_history"]
    require(grids == [list(v) for v in GRIDS] and len(freezes) == 4 and type(history) is list and 1 <= len(history) <= 64 and
            all(type(grid) is list and len(grid) == 2 and all(type(n) is int and 0 <= n <= 65535 for n in grid) for grid in history) and
            all(type(n) is int and 1 <= n <= len(history) for n in freezes), "Fixed grids/freeze/NAWS bounds differ")
    require(len(peer["resize_boundaries"]) == 2 and freezes[1] <= freezes[2], "Resize boundary count differs")
    for index, boundary in zip(RESIZE_PHASES, peer["resize_boundaries"]):
        before, after = boundary["before_event_count"], boundary["after_event_count"]
        require(type(before) is int and type(after) is int and boundary["phase"] == index and
                boundary["requested_window"] == list(WINDOWS[index]) and freezes[index-1] <= before < after == freezes[index] <= len(history) and
                all(grid == grids[index-1] for grid in history[freezes[index-1]-1:before]) and
                history[after-1] == grids[index], "NAWS resize boundary differs")
    require(all(grid == grids[1] for grid in history[freezes[1]-1:freezes[2]]) and
            all(grid == grids[3] for grid in history[freezes[3]-1:]), "Unarmed action/final NAWS change retained")
    require(peer.get("initial_grid") == list(GRIDS[0]) and peer.get("phase2_new_naws_required") is False and
            peer.get("frozen_grid_history") == grids, "Calibration checkpoint fields differ")
    require(peer.get("expected_size_change_sequence") == [list(GRIDS[0]), list(GRIDS[1]), list(GRIDS[3])] and
            peer.get("naws_size_changes") == peer["expected_size_change_sequence"], "Calibration size-change sequence differs")
    events = peer.get("phase_ready_events")
    require(type(events) is list and [event.get("phase") for event in events] == [1, 2, 3] and
            events[1].get("new_naws_required") is False and
            all(event.get("new_naws") is True for event in (events[0], events[2])), "Calibration phase-ready events differ")
    checkpoint = peer["resize_boundaries"][1].get("phase2_observed_naws")
    require(type(checkpoint) is list and all(event == list(GRIDS[2]) for event in checkpoint),
            "Phase-2 checkpoint observations differ")
    records, observations, checks = peer["records"], result["observations"], result["geometry_checks"]
    require(len(records) == len(observations) == len(checks) == 4 and peer["phase"] == 3 and peer["phase_count"] == 4,
            "Four phases incomplete")
    replies = bytearray()
    for index, (expected, actual, observed, check) in enumerate(zip(phase_records(case), records, observations, checks)):
        grid, window = list(GRIDS[index]), list(WINDOWS[index])
        require(all(actual.get(k) == v for k, v in expected.items()) and actual["frozen_grid"] == grid, "Exact action source differs")
        raw = bytes.fromhex(actual["response_hex"]); reply = common.reply_observation(raw)
        require(reply is not None and all(actual.get(k) == v for k, v in reply.items()), "DSR differs")
        require(all(observed.get(k) == actual[k] for k in ("phase", "label", "frozen_grid", *reply)), "Peer/UI phase differs")
        require(check.get("phase") == index and check.get("expected_size") == window and check.get("window_size") == window and
                baseline.qrect_size(check.get("window_rect")) == window and observed.get("window_size") == window and
                baseline.qrect_size(observed.get("window_rect")) == window, "Exact geometry differs")
        terminal = observed["terminal"]
        require(terminal.get("connected") is True and terminal.get("protocol") == "Telnet" and
                type(terminal.get("capacity")) is int and terminal["capacity"] == GRIDS[index][1], "UI binding differs")
        validate_document(case, index, terminal)
        require(type(terminal.get("markers")) is list and len(terminal["markers"]) <= 256, "Marker bound")
        require((directory / f"ack-{index}").read_text(encoding="utf-8") == "observed", "Phase acknowledgment missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-bcd-calibration-mechanics-v1", "case_id": case, "stage_count": 4, "dsr_count": 4,
            "pre_action_stage_count": 2, "frozen_grids": grids, "naws_events": len(history), "job_active": 0,
            "semantic_acceptance": False, "presented_done": None}


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
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed addressing contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Address source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Address run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_reflow_addressing_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Address source identity changed during run")
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
        print(json.dumps({"prepared": True, "case_id": args.case, "phase_count": 4, "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution and args.case is None and args.source is None and args.out is None and args.rea is None,
                "Execution uses only the immutable plan")
        run_plan(args.plan)
