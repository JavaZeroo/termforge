"""Independent interior-column EL0/EL2 observations using the fixed 85-byte wide body."""
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
CASES = {"erase-right": (b"\x1b[0K",), "erase-whole-line": (b"\x1b[2K",)}
ORIGINAL_BODY = baseline.phases(20, 91)[0][1]
INSERT_ANCHOR = b"\x1b[?7l\x1b[8;91H"
INSERTED_SOURCE = b"\x1b[8;1HL"
require(len(ORIGINAL_BODY) == 78 and hashlib.sha256(ORIGINAL_BODY).hexdigest() ==
        "c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c" and ORIGINAL_BODY.count(INSERT_ANCHOR) == 1,
        "Fixed baseline body or insertion anchor differs")
BODY = ORIGINAL_BODY.replace(INSERT_ANCHOR, INSERTED_SOURCE+INSERT_ANCHOR, 1)
RESULT_FILE = "resize-erase-interior-result.json"
ATTEMPT = "One original resize-erase-interior launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_erase_interior_reference.py", "research/ui/resize_erase_interior_reference.js",
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed resize-erase-interior case")
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
    return b"\x1b[8;31H"


def body_identity():
    return {"original_source_bytes": len(ORIGINAL_BODY), "original_source_sha256": hashlib.sha256(ORIGINAL_BODY).hexdigest(),
            "insertion_before_hex": INSERT_ANCHOR.hex(), "inserted_source_hex": INSERTED_SOURCE.hex(),
            "inserted_source_bytes": len(INSERTED_SOURCE), "inserted_source_sha256": hashlib.sha256(INSERTED_SOURCE).hexdigest(),
            "derived_source_hex": BODY.hex(), "derived_source_bytes": len(BODY), "derived_source_sha256": hashlib.sha256(BODY).hexdigest()}


def phases(case, rows=20, columns=91):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == [91, 20], "Fixed initial grid differs")
    return [("initial-derived-body-L", BODY), ("after-shrink-before-preparation", b""),
            ("after-CUP-before-erase", preparation(case)), ("after-"+case, CASES[case][0]), ("after-restore-after-erase", b"")]


def reply_observation(data):
    result = common.reply_observation(data)
    if result is not None:
        row, column = result["cursor_reply"]
        require(1 <= row <= 200 and 1 <= column <= 500, "DSR exceeds independent safety bounds")
    return result


def phase_records(case, rows=20, columns=91):
    return [{"phase": index, "label": label, "source_hex": value.hex(), "source_bytes": len(value),
             "source_sha256": hashlib.sha256(value).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, value) in enumerate(phases(case, rows, columns))]


def peer_class(case):
    validate_case(case)
    base = baseline.peer_class()
    # baseline creates a fresh module namespace for each class, not a shared peer.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(case, rows, columns)
    base._serve.__globals__["reply_observation"] = reply_observation

    class ErasePeer(base):
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
    return ErasePeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def probe_fragment(case):
    validate_case(case)
    source = (ROOT / "research/ui/resize_erase_interior_reference.js").read_text(encoding="utf-8")
    require(source.count("__RESIZE_ERASE_INTERIOR_CASE_JSON__") == 1, "Protected case template changed")
    return source.replace("__RESIZE_ERASE_INTERIOR_CASE_JSON__", json.dumps(case))


def launcher_source(case):
    validate_case(case)
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_erase_interior_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-erase-interior-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": len(phases(case)),
            "windows": [list(v) for v in windows(case)], "required_grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case)), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "action_phases": list(range(3, len(phases(case))-1)), "pre_action_phases": [0, 1, 2], "post_initial_terminal_input":
            "DSR each phase; initial wide-body L, then CUP only after shrink, one EL0/EL2, no redraw on restore",
            "initial_body_derivation": body_identity(), "independent_dsr_bounds": {"row": [1, 200], "column": [1, 500]},
            "preparation_column": 31, "document_candidate_prefix_lengths": [0, 30, 60, 61, 91],
            "preparation_precondition": "DSR 8;31 and source lines exactly equal to the shrunk baseline; no assumption that L/v survived resize",
            "scope": "Fresh direct Telnet process per case; document, DSR and public marker rectangles remain separate",
            "stop": "Wrong fixed geometry/grid, unarmed NAWS, stale snapshots, source/reply/phase mismatch, bounds or deadline failure; no retries"}


def finite_layout(lines, narrow):
    result = set(lines)
    for line in lines:
        result.add(line.rstrip(" "))
        for width in ((61, 91) if narrow else (91,)):
            if len(line) <= width:
                result.add(line.ljust(width))
        if narrow:
            for edge in (61, 91):
                if len(line) > edge:
                    result.update((line[:edge], line[edge:], line[:edge].rstrip(" "), line[edge:].rstrip(" ")))
    return result


@lru_cache(maxsize=16)
def synthetic_lines(case, phase):
    """Only finite literal source, fixed-width fragments and one erased interval."""
    validate_case(case)
    require(type(phase) is int and 0 <= phase < 5, "Phase bound")
    originals = {"", "0123456789", "ANCHOR", "L"+" "*89+"v", " "*90+"w", "x", " "*90+"wx"}
    candidates = finite_layout(originals, phase >= 1)
    if phase < 3:
        return frozenset(candidates)
    result = set(candidates)
    for line in candidates:
        for start in range(len(line)+1):
            for end in range(start, len(line)+1):
                erased = line[:start]+" "*(end-start)+line[end:]
                result.update((erased, erased.rstrip(" ")))
    # Competing observed-document endpoints are admitted without predicting
    # which policy wins: cursor prefix 30, width-minus-one 60, current/old
    # widths 61/91, and an empty line. All strings derive from the same finite
    # source-erasure candidates, only after the actual EL stage.
    for line in tuple(result):
        result.update(line[:end] for end in (0, 30, 60, 61, 91))
    return frozenset(finite_layout(result, True))


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    require(all(type(line) is str and line in synthetic_lines(case, phase) for line in terminal["lines"]),
            "Only fixed interior-erase-source synthetic document lines are allowed")
    limits = {**{str(n): 1 for n in range(10)}, "ANCHOR": 1, "L": 1, "v": 1, "w": 1, "x": 1}
    require(all(sum(line.count(marker) for line in terminal["lines"]) <= maximum for marker, maximum in limits.items()),
            "Repeated source marker")


def validate_preparation(case, before, prepared, reply):
    validate_case(case)
    require(reply == [8, 31], "Preparation DSR differs")
    require(prepared["lines"] == before["lines"], "CUP preparation changed shrunk source content")


def audit_mechanics(directory, case):
    validate_case(case)
    count = len(phases(case))
    expected_grids, expected_windows = [list(v) for v in required_grids(case)], [list(v) for v in windows(case)]
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "Erase peer did not complete cleanly")
    require(result.get("status") == "observed-fixed-resize-erase-interior-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "Erase UI observation incomplete")
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
            "Fixed erase phases incomplete")
    replies = bytearray()
    for index, (expected, actual, observed, check) in enumerate(zip(phase_records(case), records, observations, checks)):
        grid, window = expected_grids[index], expected_windows[index]
        require(all(actual.get(k) == v for k, v in expected.items()) and actual["frozen_grid"] == grid, "Exact action source differs")
        raw = bytes.fromhex(actual["response_hex"]); reply = reply_observation(raw)
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
    return {"schema": "termforge-resize-erase-interior-mechanics-v1", "case_id": case, "stage_count": count, "dsr_count": count,
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
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed erasing contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Erase source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Erase run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_erase_interior_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Erase source identity changed during run")
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
