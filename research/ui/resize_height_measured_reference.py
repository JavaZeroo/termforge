"""Measured-geometry pure-height observations in a fresh immutable namespace."""
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
RESET = b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
BODY = (RESET + b"\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B"
        b"\x1b[12;90Huvw\x1b[19;1HLOWEND")
MARKERS = ("HTOP", "HARD_A", "HARD_B", "u", "v", "w", "LOWEND")
CASES = {
    "height-retention-measured": (("initial-body", 91, 20, BODY),
                                  ("height-shrunk-unmodified", 91, 14, b""),
                                  ("height-restored-unmodified", 91, 20, b"")),
}
RESULT_FILE = "resize-height-measured-result.json"
ATTEMPT = "One original resize-height-measured launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_height_measured_reference.py", "research/ui/resize_height_measured_reference.js",
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed resize-height-measured case")
    return case


def windows(case):
    validate_case(case)
    return [(1360 if columns == 91 else 1000, 749 if rows == 20 else 569)
            for _, columns, rows, _ in CASES[case]]


def required_grids(case):
    validate_case(case)
    return [(columns, rows) for _, columns, rows, _ in CASES[case]]


def resize_phases(case):
    values = required_grids(case)
    return tuple(index for index in range(1, len(values)) if values[index] != values[index-1])


def controller_contract(case):
    validate_case(case)
    return {"case_id": case, "labels": [label for label, _, _, _ in CASES[case]],
            "windows": [list(v) for v in windows(case)], "grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case)), "marker_labels": list(MARKERS),
            "initial_dsr": [19, 7]}


def phases(case, rows=20, columns=91):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == [91, 20], "Fixed initial grid differs")
    return [(label, value) for label, _, _, value in CASES[case]]


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


def allowed_grid(before, after, value):
    if not all(type(v) in (list, tuple) and len(v) == 2 and
               all(type(n) is int and 1 <= n <= 65535 for n in v) for v in (before, after, value)):
        return False
    changed = [axis for axis in (0, 1) if before[axis] != after[axis]]
    return len(changed) == 1 and all(
        min(before[axis], after[axis]) <= value[axis] <= max(before[axis], after[axis])
        if axis == changed[0] else value[axis] == before[axis] for axis in (0, 1))


def peer_class(case):
    validate_case(case)
    base = baseline.peer_class()
    # baseline creates a fresh module namespace for each class, not a shared peer.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(case, rows, columns)
    base._serve.__globals__["reply_observation"] = reply_observation

    class HeightPeer(base):
        def _freeze_initial(self):
            super()._freeze_initial()
            require(self.state["frozen_grid"] == [91, 20], "Fixed initial grid differs")
            self.state["case_id"] = case

        def _on_naws(self):
            grid = [self.state["columns"], self.state["rows"]]
            history = self.state.setdefault("naws_history", [])
            require(len(history) < 64 and all(type(n) is int and 0 <= n <= 65535 for n in grid), "NAWS event bound")
            history.append(grid)
            self._record()
            if self.state.get("frozen_grid"):
                target = self.state.get("resize_armed")
                if target is None:
                    require(grid == self.state["frozen_grid"], "NAWS changed outside an armed resize")
                else:
                    require(target in resize_phases(case) and
                            allowed_grid(self.state["frozen_grid"], required_grids(case)[target], grid),
                            "NAWS changed an unarmed axis or exceeded the fixed interval")

        def _phase_ready(self, index):
            if index == -1:
                return True
            ack = self.directory / f"ack-{index}"
            if not ack.exists() or self.state["records"][index]["cursor_reply"] is None:
                return False
            require(ack.read_text(encoding="utf-8") == "observed", "UI acknowledgement differs")
            if index == 0:
                require(self.state["records"][0]["cursor_reply"] == [19, 7], "Initial fixture DSR differs")
            if index == len(phases(case))-1:
                return True
            target = index + 1
            if target not in resize_phases(case):
                require(self.state["resize_armed"] is None and self.state["frozen_grid"] == list(required_grids(case)[target]),
                        "Action was not released from its fixed per-stage grid")
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
    return HeightPeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def probe_fragment(case):
    validate_case(case)
    source = (ROOT / "research/ui/resize_height_measured_reference.js").read_text(encoding="utf-8")
    require(source.count("__RESIZE_HEIGHT_MEASURED_CASE_JSON__") == 1, "Protected case template changed")
    return source.replace("__RESIZE_HEIGHT_MEASURED_CASE_JSON__", json.dumps(controller_contract(case)))


def launcher_source(case):
    validate_case(case)
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_height_measured_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-height-measured-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": len(phases(case)),
            "windows": [list(v) for v in windows(case)], "required_grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case)), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "controller_contract": controller_contract(case), "execution_group": "one-measured-height-case",
            "independent_dsr_bounds": {"row": [1, 200], "column": [1, 500]},
            "initial_fixture_dsr": [19, 7], "initial_fixture_markers": list(MARKERS),
            "geometry_basis": {"reference": "research/ui/results/2026-11-02-resize-geometry-reference/strict-export.json",
                "strict_export_sha256": "f18a2320c073ed477f773b42076c9968c0c8af8530af19548d5695e66e8cf9b5",
                "observed_windows": [[1360, 749], [1360, 569]], "observed_grids": [[91, 20], [91, 14]],
                "restore_geometry_previously_observed": False},
            "height_geometry_status": "The two shrink geometries were independently measured; restore91x20 is checked in this new run, without calibration or retries",
            "post_initial_terminal_input": "DSR only; no CUP, redraw, CR/LF or replacement text",
            "scope": "Fresh direct Telnet process per case; document/API positions, DSR and pixel rectangles remain separate observations",
            "stop": "Wrong fixed geometry/grid, unarmed or wrong-axis NAWS, stale snapshots, initial fixture/source/reply/phase mismatch, bounds or deadline failure; no retries"}


def bounded_fragments(lines, split):
    result = set(lines)
    for line in lines:
        result.add(line.rstrip(" "))
        for width in ((61, 91) if split else (91,)):
            if len(line) <= width:
                result.add(line.ljust(width))
        for edge in ((61, 91) if split else (91,)):
            if len(line) > edge:
                result.update((line[:edge], line[edge:], line[:edge].rstrip(" "), line[edge:].rstrip(" ")))
    return result


@lru_cache(maxsize=16)
def synthetic_lines(case, phase):
    """Finite ASCII source-derived words; loss is observation, not acceptance."""
    validate_case(case)
    require(type(phase) is int and 0 <= phase < len(phases(case)), "Phase bound")
    base = {"", "HTOP", "HARD_A", "HARD_B", "LOWEND", " "*89+"uvw"}
    return frozenset(bounded_fragments(base, False))


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    lines = terminal["lines"]
    require(all(type(line) is str and line.isascii() and line in synthetic_lines(case, phase) for line in lines),
            "Only fixed measured-height-source synthetic document lines are allowed")
    counts = {marker: sum(line.count(marker) for line in lines) for marker in MARKERS}
    require(all(count <= 1 for count in counts.values()), "Repeated height-source marker")
    if phase == 0:
        require(all(count == 1 for count in counts.values()), "Initial fixture sentinel missing")


def audit_mechanics(directory, case):
    validate_case(case)
    count = len(phases(case))
    expected_grids, expected_windows = [list(v) for v in required_grids(case)], [list(v) for v in windows(case)]
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "Height peer did not complete cleanly")
    require(result.get("status") == "observed-fixed-resize-height-measured-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "Height UI observation incomplete")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(k)) is int and cleanup[k] >= 0 for k in ("total", "terminated")) and
            profile == {"only_synthetic_session": True, "session_count": 1}, "Owned cleanup/profile differs")
    grids, freezes, history = peer["frozen_grids"], peer["freeze_event_counts"], peer["naws_history"]
    require(grids == expected_grids and len(freezes) == count and type(history) is list and 1 <= len(history) <= 64 and
            all(type(grid) is list and len(grid) == 2 and all(type(n) is int and 0 <= n <= 65535 for n in grid) for grid in history) and
            all(type(n) is int and 1 <= n <= len(history) for n in freezes), "Fixed grids/freeze/NAWS bounds differ")
    require(len(peer["resize_boundaries"]) == len(resize_phases(case)) and freezes == sorted(freezes), "Resize boundary count differs")
    for index, boundary in zip(resize_phases(case), peer["resize_boundaries"]):
        before, after = boundary["before_event_count"], boundary["after_event_count"]
        require(type(before) is int and type(after) is int and boundary["phase"] == index and
                boundary["requested_window"] == expected_windows[index] and freezes[index-1] <= before < after == freezes[index] <= len(history) and
                all(grid == grids[index-1] for grid in history[freezes[index-1]-1:before]) and
                all(allowed_grid(grids[index-1], grids[index], grid) for grid in history[before:after]) and history[after-1] == grids[index], "NAWS resize boundary differs")
    for index in range(1, count):
        if index not in resize_phases(case):
            require(grids[index] == grids[index-1] and
                    all(grid == grids[index] for grid in history[freezes[index-1]-1:freezes[index]]),
                    "Unarmed per-stage NAWS change retained")
    require(all(grid == grids[-1] for grid in history[freezes[-1]-1:]), "Unarmed final NAWS change retained")
    records, observations, checks = peer["records"], result["observations"], result["geometry_checks"]
    require(len(records) == len(observations) == len(checks) == count and peer["phase"] == count-1 and peer["phase_count"] == count,
            "Fixed height phases incomplete")
    replies = bytearray()
    for index, (expected, actual, observed, check) in enumerate(zip(phase_records(case), records, observations, checks)):
        grid, window = expected_grids[index], expected_windows[index]
        require(all(actual.get(k) == v for k, v in expected.items()) and actual["frozen_grid"] == grid, "Exact action source differs")
        raw = bytes.fromhex(actual["response_hex"]); reply = reply_observation(raw)
        if index == 0:
            require(reply is not None and reply["cursor_reply"] == [19, 7], "Initial fixture DSR differs")
        require(reply is not None and all(actual.get(k) == v for k, v in reply.items()), "DSR differs")
        require(all(observed.get(k) == actual[k] for k in ("phase", "label", "frozen_grid", *reply)), "Peer/UI phase differs")
        require(check.get("phase") == index and check.get("expected_size") == window and check.get("window_size") == window and
                baseline.qrect_size(check.get("window_rect")) == window and observed.get("window_size") == window and
                baseline.qrect_size(observed.get("window_rect")) == window, "Exact geometry differs")
        terminal = observed["terminal"]
        require(terminal.get("connected") is True and terminal.get("protocol") == "Telnet" and
                type(terminal.get("capacity")) is int and terminal["capacity"] == grid[1], "UI binding differs")
        validate_document(case, index, terminal)
        require(type(terminal.get("markers")) is list and len(terminal["markers"]) <= 256, "Marker bound")
        require((directory / f"ack-{index}").read_text(encoding="utf-8") == "observed", "Phase acknowledgment missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-height-measured-mechanics-v1", "case_id": case, "stage_count": count, "dsr_count": count,
            "stateful_observations_preserved": True, "frozen_grids": grids, "naws_events": len(history), "job_active": 0,
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
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed height contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Height source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Height run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_height_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Height source identity changed during run")
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
