"""Four-stage, one-action CUP addressing observation across an earlier reflowing line.

The initial body contains a 123-character soft-wrapped line *before* the target
rows. Narrowing 91 -> 61 columns makes that line occupy one extra display row,
which shifts later display rows by one while retained source-fragment rows keep
their original numbering. The single action `CSI 8;2H Q` therefore lands on the
source-fragment row 6 ("6") or the reflowed display row 5 ("5"), and composition
never runs a target. Preparation never starts the original program.
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

# 91x20 initial body: a 123-character line on source rows 4-5 (soft continuation),
# then single-row markers on source rows 6..10, ending with the cursor on row 11.
RESET = b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
REFLOW_LINE = b"r" * 122 + b"v"
BODY = (RESET + b"\x1b[2;1H0123456789\x1b[3;1HANCHOR" + b"\x1b[5;1H" + REFLOW_LINE +
        b"\x1b[7;1H5\x1b[8;1H6\x1b[9;1H7\x1b[10;1H8\x1b[11;1H9\x1b[12;1H")
CASES = {"shrink-reflow-cup": b"\x1b[8;2HQ"}
WINDOWS = ((1360, 749), (1000, 749), (1000, 749), (1360, 749))
GRIDS = ((91, 20), (61, 20), (61, 20), (91, 20))
RESIZE_PHASES = (1, 3)
# The shared controller writes this file name and message kinds; both probes use
# the same qt_r1_text/resize_address_reference.js controller.
RESULT_FILE = "resize-address-result.json"
CONTROLLER = "research/ui/resize_address_reference.js"
CONTROLLER_ANCHOR = "__RESIZE_ADDRESS_CASE_JSON__"
ATTEMPT = "One original post-reflow addressing launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_addressing_reflow_reference.py", CONTROLLER,
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")
BASE_LINES = ("", "0123456789", "ANCHOR", REFLOW_LINE.decode("ascii"), "5", "6", "7", "8", "9")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed reflow-addressing case")
    return case


def phases(case, rows=20, columns=91):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == list(GRIDS[0]), "Fixed initial grid differs")
    return [("initial-reflow-body", BODY), ("after-shrink-before-action", b""),
            ("after-" + case, CASES[case]), ("after-restore-after-action", b"")]


def phase_records(case, rows=20, columns=91):
    return [{"phase": index, "label": label, "source_hex": value.hex(), "source_bytes": len(value),
             "source_sha256": hashlib.sha256(value).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, value) in enumerate(phases(case, rows, columns))]


def peer_class(case):
    validate_case(case)
    base = baseline.peer_class()
    # baseline creates a fresh module namespace for each class, not a shared peer.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(case, rows, columns)

    class ReflowPeer(base):
        def _freeze_initial(self):
            super()._freeze_initial()
            require(self.state["frozen_grid"] == list(GRIDS[0]), "Fixed initial grid differs")
            self.state["case_id"] = case

        def _phase_ready(self, index):
            if index == -1:
                return True
            ack = self.directory / f"ack-{index}"
            if not ack.exists() or self.state["records"][index]["cursor_reply"] is None:
                return False
            require(ack.read_text(encoding="utf-8") == "observed", "UI acknowledgement differs")
            if index == 3:
                return True
            target = index + 1
            if target == 2:
                require(self.state["resize_armed"] is None and self.state["frozen_grid"] == list(GRIDS[2]),
                        "Action was not released from the fixed shrunk grid")
                self.state["frozen_grids"].append(list(self.state["frozen_grid"]))
                self.state["freeze_event_counts"].append(len(self.state["naws_history"]))
                return True
            request = self.directory / f"resize-request-{target}"
            if not request.exists():
                return False
            require(json.loads(request.read_text(encoding="utf-8")) == {"phase": target, "window": list(WINDOWS[target])},
                    "Resize request differs from immutable window sequence")
            if self.state["resize_armed"] is None:
                self.state["resize_armed"] = target
                self.state["resize_boundaries"].append({"phase": target, "before_event_count": len(self.state["naws_history"]),
                                                        "requested_window": list(WINDOWS[target])})
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
            return True
    return ReflowPeer


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
            "from research.ui.resize_addressing_reflow_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-reflow-addressing-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": 4,
            "windows": [list(v) for v in WINDOWS], "required_grids": [list(v) for v in GRIDS],
            "resize_phases": list(RESIZE_PHASES), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "action_phase": 2, "pre_action_phases": [0, 1], "post_initial_terminal_input":
            "DSR at every phase; exactly one declared CUP+Q action after the shrunk pre-action observation is acknowledged",
            "scope": "Fresh direct Telnet process per case; document, DSR and public marker rectangles remain separate",
            "stop": "Wrong fixed geometry/grid, unarmed NAWS, stale snapshots, source/reply/phase mismatch, bounds or deadline failure; no retries"}


def synthetic_lines(case, phase):
    """Finite source-derived text candidates, not predictions of the original policy.

    The initial body plus append/replace of the single action marker at a bounded
    column of one fixed line; the reflowing 123-character line keeps its logical
    text because only the display projection changes width.
    """
    validate_case(case)
    require(type(phase) is int and 0 <= phase < 4, "Phase bound")
    originals = set(BASE_LINES)
    if phase < 2:
        return originals
    result = set(originals)
    for line in originals:
        for column in range(len(line)+1):
            result.add(line[:column].ljust(column)+"Q"+line[column+1:])
    return result


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    require(all(type(line) is str and line in synthetic_lines(case, phase) for line in terminal["lines"]),
            "Only fixed action-derived synthetic document lines are allowed")
    require(sum(line.count("Q") for line in terminal["lines"]) <= (0 if phase < 2 else 1),
            "Repeated or premature action marker")


def audit_mechanics(directory, case):
    validate_case(case)
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "Reflow peer did not complete cleanly")
    require(result.get("status") == "observed-four-resize-address-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "Reflow UI observation incomplete")
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
                all(grid[1] == 20 for grid in history[before:after]) and history[after-1] == grids[index], "NAWS resize boundary differs")
    require(all(grid == grids[1] for grid in history[freezes[1]-1:freezes[2]]) and
            all(grid == grids[3] for grid in history[freezes[3]-1:]), "Unarmed action/final NAWS change retained")
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
                type(terminal.get("capacity")) is int and terminal["capacity"] == 20, "UI binding differs")
        validate_document(case, index, terminal)
        require(type(terminal.get("markers")) is list and len(terminal["markers"]) <= 256, "Marker bound")
        require((directory / f"ack-{index}").read_text(encoding="utf-8") == "observed", "Phase acknowledgment missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-reflow-addressing-mechanics-v1", "case_id": case, "stage_count": 4, "dsr_count": 4,
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
