"""Bounded edge/edit observations with independent preparation and no target in prepare."""
from __future__ import annotations
import argparse
import hashlib
import json
from functools import lru_cache
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as common
from research.ui import resize_state_reference as baseline

require, sha = common.require, common.sha
qrect_size = baseline.qrect_size
EXE_SHA256, MANIFEST_SHA256 = common.EXE_SHA256, common.MANIFEST_SHA256
CASES = {"right-edge-wrap": (b"Q", b"R"), "erase-right": (b"\x1b[0K",),
         "erase-whole-line": (b"\x1b[2K",), "insert-char": (b"\x1b[1@",), "delete-char": (b"\x1b[1P",)}
RESULT_FILE = "resize-edit-result.json"
ATTEMPT = "One original resize-edit launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_edit_reference.py", "research/ui/resize_edit_reference.js",
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed resize-edit case")
    return case


def windows(case):
    validate_case(case)
    return [(1360, 749), *([(1000, 749)] * (len(CASES[case])+2)), (1360, 749)]


def required_grids(case):
    return [(91, 20), *([(61, 20)] * (len(windows(case))-2)), (91, 20)]


def resize_phases(case):
    return (1, len(windows(case))-1)


def preparation(case):
    validate_case(case)
    return (b"\x1b[8;1HL" if case.startswith("erase-") else b"") + b"\x1b[8;61H"


def phases(case, rows=20, columns=91):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == [91, 20], "Fixed initial grid differs")
    return [("initial-body", baseline.phases(rows, columns)[0][1]),
            ("after-shrink-before-preparation", b""), ("after-preparation-before-edit", preparation(case)),
            *[("after-"+("Q" if index == 0 else "R") if case == "right-edge-wrap" else "after-"+case, action)
              for index, action in enumerate(CASES[case])], ("after-restore-after-edit", b"")]


def phase_records(case, rows=20, columns=91):
    return [{"phase": index, "label": label, "source_hex": value.hex(), "source_bytes": len(value),
             "source_sha256": hashlib.sha256(value).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, value) in enumerate(phases(case, rows, columns))]


def peer_class(case):
    validate_case(case)
    base = baseline.peer_class()
    # baseline creates a fresh module namespace for each class, not a shared peer.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(case, rows, columns)

    class EditPeer(base):
        def _freeze_initial(self):
            super()._freeze_initial()
            require(self.state["frozen_grid"] == [91, 20], "Fixed initial grid differs")
            self.state["case_id"] = case

        def _phase_ready(self, index):
            if index == -1:
                return True
            ack = self.directory / f"ack-{index}"
            if not ack.exists() or self.state["records"][index]["cursor_reply"] is None:
                return False
            require(ack.read_text(encoding="utf-8") == "observed", "UI acknowledgement differs")
            if index == len(phases(case))-1:
                return True
            target = index + 1
            if target not in resize_phases(case):
                require(self.state["resize_armed"] is None and self.state["frozen_grid"] == [61, 20],
                        "Action was not released from the fixed shrunk grid")
                self.state["frozen_grids"].append(list(self.state["frozen_grid"]))
                self.state["freeze_event_counts"].append(len(self.state["naws_history"]))
                return True
            request = self.directory / f"resize-request-{target}"
            if not request.exists():
                return False
            require(json.loads(request.read_text(encoding="utf-8")) == {"phase": target, "window": list(windows(case)[target])},
                    "Resize request differs from immutable window sequence")
            if self.state["resize_armed"] is None:
                self.state["resize_armed"] = target
                self.state["resize_boundaries"].append({"phase": target, "before_event_count": len(self.state["naws_history"]),
                                                        "requested_window": list(windows(case)[target])})
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
            require(len(history) > boundary["before_event_count"] and history[-1] == grid == list(required_grids(case)[target]),
                    "Fixed resized grid or NAWS boundary differs")
            boundary["after_event_count"] = len(history)
            self.state.update(frozen_grid=grid, resize_armed=None)
            self.state["frozen_grids"].append(grid)
            self.state["freeze_event_counts"].append(len(history))
            return True
    return EditPeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def probe_fragment(case):
    validate_case(case)
    source = (ROOT / "research/ui/resize_edit_reference.js").read_text(encoding="utf-8")
    require(source.count("__RESIZE_EDIT_CASE_JSON__") == 1, "Protected case template changed")
    return source.replace("__RESIZE_EDIT_CASE_JSON__", json.dumps(case))


def launcher_source(case):
    validate_case(case)
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_edit_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-edit-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": len(phases(case)),
            "windows": [list(v) for v in windows(case)], "required_grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case)), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "action_phases": list(range(3, len(phases(case))-1)), "pre_action_phases": [0, 1, 2], "post_initial_terminal_input":
            "DSR each phase; exact preparation, then one edit or separately acknowledged Q and R; no redraw on restore",
            "preparation_precondition": "DSR 8;61 and unchanged source lines except the single declared L prefix; preserve failure before acknowledgment",
            "scope": "Fresh direct Telnet process per case; document, DSR and public marker rectangles remain separate",
            "stop": "Wrong fixed geometry/grid, unarmed NAWS, stale snapshots, source/reply/phase mismatch, bounds or deadline failure; no retries"}


@lru_cache(maxsize=32)
def synthetic_lines(case, phase):
    """Finite source-derived text candidates, not predictions of the original policy."""
    validate_case(case)
    require(type(phase) is int and 0 <= phase < len(phases(case)), "Phase bound")
    originals = {"", "0123456789", "ANCHOR", " "*90+"v", " "*90+"w", "x", " "*90+"wx"}
    if phase < 2:
        return frozenset(originals)
    if case.startswith("erase-"):
        originals.add("L"+" "*89+"v")
    if phase == 2:
        return frozenset(originals)
    result = set(originals)
    if case.startswith("erase-"):
        for line in originals:
            # The protocol can address a soft fragment inside one retained
            # logical line; keep exact interval blanks, including trailing ones.
            for start in range(len(line)+1):
                for end in range(start, len(line)+1):
                    erased = line[:start]+" "*(end-start)+line[end:]
                    result.update((erased, erased.rstrip(" ")))
    elif case in ("insert-char", "delete-char"):
        for line in originals:
            for column in range(len(line)+1):
                whole = line[:column]+(" "+line[column:] if case == "insert-char" else line[column+1:])
                result.update((whole, whole.rstrip(" ")))
            for edge in (61, 91, 92):
                padded = line.ljust(edge)
                for column in range(min(edge, len(padded))):
                    edited = (padded[:column]+" "+padded[column:edge-1]+padded[edge:] if case == "insert-char"
                              else padded[:column]+padded[column+1:edge]+" "+padded[edge:])
                    result.update((edited, edited.rstrip(" "), edited[:edge], edited[:edge].rstrip(" ")))
    else:
        # Narrow, declared edge positions: new right cell 60; continuation 61;
        # old logical end 91; or a distinct next fragment at column 0.
        qlines = {" "*60+"Q", " "*60+"Q"+" "*29+"v", " "*29+"v"}
        result.update(qlines)
        if phase >= 4:
            result.add("R")
            for line in qlines | {" "*90+"v"}:
                for column in (0, 60, 61, 91):
                    result.add(line[:column].ljust(column)+"R"+line[column+1:])
            result.update((" "*60+"QR", "R"+" "*28+"v", "R"+" "*29+"v", " "*60+"R"))
    return frozenset(result)


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    require(all(type(line) is str and line in synthetic_lines(case, phase) for line in terminal["lines"]),
            "Only fixed action-derived synthetic document lines are allowed")
    limits = {**{str(n): 1 for n in range(10)}, "ANCHOR": 1,
              "L": int(case.startswith("erase-") and phase >= 2), "Q": int(case == "right-edge-wrap" and phase >= 3),
              "R": 1+int(case == "right-edge-wrap" and phase >= 4), "v": 1, "w": 1, "x": 1}
    require(all(sum(line.count(marker) for line in terminal["lines"]) <= maximum for marker, maximum in limits.items()),
            "Repeated or premature source/action marker")


def validate_preparation(case, before, prepared, reply):
    require(reply == [8, 61], "Preparation DSR differs")
    expected = list(before["lines"])
    require(expected.count(" "*90+"v") == 1, "Preparation tail sentinel is missing or repeated")
    if case.startswith("erase-"):
        index = expected.index(" "*90+"v")
        expected[index] = "L"+" "*89+"v"
    require(prepared["lines"] == expected, "Preparation changed source content beyond its declared prefix")


def audit_mechanics(directory, case):
    validate_case(case)
    count = len(phases(case))
    expected_grids, expected_windows = [list(v) for v in required_grids(case)], [list(v) for v in windows(case)]
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "Edit peer did not complete cleanly")
    require(result.get("status") == "observed-fixed-resize-edit-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "Edit UI observation incomplete")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(k)) is int and cleanup[k] >= 0 for k in ("total", "terminated")) and
            profile == {"only_synthetic_session": True, "session_count": 1}, "Owned cleanup/profile differs")
    grids, freezes, history = peer["frozen_grids"], peer["freeze_event_counts"], peer["naws_history"]
    require(grids == expected_grids and len(freezes) == count and type(history) is list and 1 <= len(history) <= 64 and
            all(type(grid) is list and len(grid) == 2 and all(type(n) is int and 0 <= n <= 65535 for n in grid) for grid in history) and
            all(type(n) is int and 1 <= n <= len(history) for n in freezes), "Fixed grids/freeze/NAWS bounds differ")
    require(len(peer["resize_boundaries"]) == 2 and freezes == sorted(freezes), "Resize boundary count differs")
    for index, boundary in zip(resize_phases(case), peer["resize_boundaries"]):
        before, after = boundary["before_event_count"], boundary["after_event_count"]
        require(type(before) is int and type(after) is int and boundary["phase"] == index and
                boundary["requested_window"] == expected_windows[index] and freezes[index-1] <= before < after == freezes[index] <= len(history) and
                all(grid == grids[index-1] for grid in history[freezes[index-1]-1:before]) and
                all(grid[1] == 20 for grid in history[before:after]) and history[after-1] == grids[index], "NAWS resize boundary differs")
    require(all(grid == grids[1] for grid in history[freezes[1]-1:freezes[-2]]) and
            all(grid == grids[-1] for grid in history[freezes[-1]-1:]), "Unarmed action/final NAWS change retained")
    records, observations, checks = peer["records"], result["observations"], result["geometry_checks"]
    require(len(records) == len(observations) == len(checks) == count and peer["phase"] == count-1 and peer["phase_count"] == count,
            "Fixed edit phases incomplete")
    replies = bytearray()
    for index, (expected, actual, observed, check) in enumerate(zip(phase_records(case), records, observations, checks)):
        grid, window = expected_grids[index], expected_windows[index]
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
        if index == 2:
            validate_preparation(case, observations[1]["terminal"], terminal, actual["cursor_reply"])
        require(type(terminal.get("markers")) is list and len(terminal["markers"]) <= 256, "Marker bound")
        require((directory / f"ack-{index}").read_text(encoding="utf-8") == "observed", "Phase acknowledgment missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-edit-mechanics-v1", "case_id": case, "stage_count": count, "dsr_count": count,
            "pre_action_stage_count": 3, "frozen_grids": grids, "naws_events": len(history), "job_active": 0,
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
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed editing contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Edit source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Edit run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_edit_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Edit source identity changed during run")
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
