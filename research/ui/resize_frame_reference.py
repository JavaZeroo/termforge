"""Independent frame/edge state observations; source-bound stages and offline preparation."""
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
PREFIX = RESET + b"\x1b[2;1H0123456789\x1b[3;1HANCHOR"
BODY = baseline.phases(20, 91)[0][1]
require(len(BODY) == 78 and hashlib.sha256(BODY).hexdigest() ==
        "c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c", "Fixed 78-byte body differs")
CASES = {
    "alternate-active": (("alternate-body", 91, b"\x1b[?1049h"+BODY),
                         ("alternate-shrunk", 61, b""), ("alternate-restored", 91, b"")),
    "hidden-primary": (("primary-origin-on-body", 91, BODY+b"\x1b[2;5r\x1b[?6h\x1b[2;10HMP"),
                       ("alternate-origin-off", 91, b"\x1b[?1049h\x1b[3;7r\x1b[?6l\x1b[2;10HAP"),
                       ("alternate-shrunk-primary-hidden", 61, b""),
                       ("primary-return-unmodified", 61, b"\x1b[?1049l"),
                       ("primary-saved-origin-sample", 61, b"\x1b[1;1HPS\x1b[99;1HBS"),
                       ("primary-explicit-origin-on", 61, b"\x1b[?6h\x1b[1;1HPI\x1b[99;1HBI"),
                       ("primary-explicit-origin-off", 61, b"\x1b[?6l\x1b[1;1HPO\x1b[99;1HBO"),
                       ("primary-width-restored-after-controls", 91, b"")),
    "pending-on-shrink": (("wrap-on-old-right-edge", 91, PREFIX+b"\x1b[8;91HW"),
                          ("shrunk-before-next-character", 61, b""),
                          ("shrunk-after-X", 61, b"X"), ("restored-after-X", 91, b"")),
    "wrap-off-shrink": (("wrap-off-old-right-edge", 91, PREFIX+b"\x1b[?7l\x1b[8;91HW"),
                        ("shrunk-before-next-character", 61, b""),
                        ("shrunk-after-X", 61, b"X"), ("restored-after-X", 91, b"")),
    "pending-on-grow": (("wrap-on-base", 91, PREFIX), ("shrunk-before-edge-setup", 61, b""),
                        ("wrap-on-narrow-right-edge", 61, b"\x1b[8;61HW"),
                        ("grown-before-next-character", 91, b""), ("grown-after-X", 91, b"X")),
    "wrap-off-grow": (("wrap-off-base", 91, PREFIX+b"\x1b[?7l"), ("shrunk-before-edge-setup", 61, b""),
                      ("wrap-off-narrow-right-edge", 61, b"\x1b[8;61HW"),
                      ("grown-before-next-character", 91, b""), ("grown-after-X", 91, b"X")),
}
INITIAL_CASES = ("alternate-active", "hidden-primary", "pending-on-shrink", "wrap-off-shrink")
RESULT_FILE = "resize-frame-result.json"
ATTEMPT = "One original resize-frame launch attempt; preserve failures and never retry this plan.\n"
SOURCE_FILES = ("research/ui/resize_frame_reference.py", "research/ui/resize_frame_reference.js",
                "research/ui/resize_state_reference.py", "research/ui/mode_state_reference.py",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")


def validate_case(case):
    require(type(case) is str and case in CASES, "Unknown fixed resize-frame case")
    return case


def windows(case):
    validate_case(case)
    return [(1360 if width == 91 else 1000, 749) for _, width, _ in CASES[case]]


def required_grids(case):
    validate_case(case)
    return [(width, 20) for _, width, _ in CASES[case]]


def resize_phases(case):
    values = required_grids(case)
    return tuple(index for index in range(1, len(values)) if values[index] != values[index-1])


def controller_contract(case):
    validate_case(case)
    return {"case_id": case, "labels": [label for label, _, _ in CASES[case]],
            "windows": [list(v) for v in windows(case)], "grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case))}


def phases(case, rows=20, columns=91):
    validate_case(case)
    require(type(rows) is int and type(columns) is int and [columns, rows] == [91, 20], "Fixed initial grid differs")
    return [(label, value) for label, _, value in CASES[case]]


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

    class FramePeer(base):
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
    return FramePeer


def source_identity():
    return common.source_identity(root=ROOT, source_files=SOURCE_FILES)


def probe_fragment(case):
    validate_case(case)
    source = (ROOT / "research/ui/resize_frame_reference.js").read_text(encoding="utf-8")
    require(source.count("__RESIZE_FRAME_CASE_JSON__") == 1, "Protected case template changed")
    return source.replace("__RESIZE_FRAME_CASE_JSON__", json.dumps(controller_contract(case)))


def launcher_source(case):
    validate_case(case)
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.resize_frame_reference import peer_class, probe_fragment\n        Peer = peer_class("+repr(case)+")",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "probe_fragment("+repr(case)+")",
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def fixed_contract(case):
    validate_case(case)
    return {"schema": "termforge-resize-frame-plan-v1", "issue": 29, "related_issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": len(phases(case)),
            "windows": [list(v) for v in windows(case)], "required_grids": [list(v) for v in required_grids(case)],
            "resize_phases": list(resize_phases(case)), "window_rect_format": "qt-qrect-inclusive-ltrb",
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "phases": phase_records(case), "reference_exe_sha256": EXE_SHA256,
            "reference_files_manifest_sha256": MANIFEST_SHA256, "semantic_acceptance": False,
            "controller_contract": controller_contract(case),
            "execution_group": "initial-four" if case in INITIAL_CASES else "deferred-grow",
            "independent_dsr_bounds": {"row": [1, 200], "column": [1, 500]}, "post_initial_terminal_input":
            "Each exact stage is observed and acknowledged before the next; zero-byte stages add only DSR, no corrective CUP or redraw",
            "scope": "Fresh direct Telnet process per case; document, DSR and public marker rectangles remain separate",
            "stop": "Wrong fixed geometry/grid, unarmed NAWS, stale snapshots, source/reply/phase mismatch, bounds or deadline failure; no retries"}


def bounded_fragments(lines, split):
    result = set(lines)
    for line in lines:
        result.add(line.rstrip(" "))
        # A write may materialize the complete known row width, including its
        # trailing blanks. Keep only the two declared finite padding horizons.
        for width in ((61, 91) if split else (91,)):
            if len(line) <= width:
                result.add(line.ljust(width))
        if split:
            for edge in (61, 91):
                if len(line) > edge:
                    result.update((line[:edge], line[edge:], line[:edge].rstrip(" "), line[edge:].rstrip(" ")))
    return result


def overwrite(line, column, text):
    return line[:column].ljust(column)+text+line[column+len(text):]


@lru_cache(maxsize=64)
def synthetic_lines(case, phase):
    """Finite literal-source transformations, never an assertion of frame policy."""
    validate_case(case)
    require(type(phase) is int and 0 <= phase < len(phases(case)), "Phase bound")
    base = {"", "0123456789", "ANCHOR"}
    split = any(index <= phase for index in resize_phases(case))
    if case in ("alternate-active", "hidden-primary"):
        base.update((" "*90+"v", " "*90+"w", "x", " "*90+"wx"))
        if case == "hidden-primary":
            middles = [None, "MP"] + (["AP"] if phase >= 1 else [])
            heads = [None]+(["PS","BS"] if phase >= 4 else [])+(["PI","BI"] if phase >= 5 else [])+(["PO","BO"] if phase >= 6 else [])
            base = {overwrite(variant, 0, head) if head else variant
                    for line in base for middle in middles
                    for variant in [overwrite(line, 9, middle) if middle else line] for head in heads}
        return frozenset(bounded_fragments(base, split))
    grow = case.endswith("-grow")
    w_phase, x_phase = (2, 4) if grow else (0, 2)
    if phase >= w_phase:
        base.add(" "*(60 if grow else 90)+"W")
    base = bounded_fragments(base, split)
    if phase >= x_phase:
        written = set(base)
        for line in base:
            columns = {0, 60, 61, 90, 91}
            if "W" in line:
                columns.update((line.index("W"), line.index("W")+1))
            for column in columns:
                written.add(overwrite(line, column, "X"))
        base = bounded_fragments(written, split)
    return frozenset(base)


def validate_document(case, phase, terminal):
    require(type(terminal.get("line_count")) is int and 0 <= terminal["line_count"] <= 200 and
            type(terminal.get("lines")) is list and len(terminal["lines"]) == terminal["line_count"], "Document bound")
    require(all(type(line) is str and line in synthetic_lines(case, phase) for line in terminal["lines"]),
            "Only fixed frame-source synthetic document lines are allowed")
    body = case in ("alternate-active", "hidden-primary")
    hidden = case == "hidden-primary"
    grow = case.endswith("-grow")
    limits = {**{str(n): 1 for n in range(10)}, "ANCHOR": 1,
              "v": int(body), "w": int(body), "x": int(body),
              "MP": int(hidden), "AP": int(hidden and phase >= 1),
              "PS": int(hidden and phase >= 4), "BS": int(hidden and phase >= 4),
              "PI": int(hidden and phase >= 5), "BI": int(hidden and phase >= 5),
              "PO": int(hidden and phase >= 6), "BO": int(hidden and phase >= 6),
              "W": int(not body and phase >= (2 if grow else 0)),
              "X": int(not body and phase >= (4 if grow else 2))}
    require(all(sum(line.count(marker) for line in terminal["lines"]) <= maximum for marker, maximum in limits.items()),
            "Repeated or premature frame-source marker")


def audit_mechanics(directory, case):
    validate_case(case)
    count = len(phases(case))
    expected_grids, expected_windows = [list(v) for v in required_grids(case)], [list(v) for v in windows(case)]
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in ("peer.json", RESULT_FILE, "cleanup.json", "profile-check.json")]
    require(all(peer.get(k) is True for k in ("connected", "closed", "complete")) and not peer.get("error") and peer.get("case_id") == case,
            "Frame peer did not complete cleanly")
    require(result.get("status") == "observed-fixed-resize-frame-states" and result.get("case_id") == case and result.get("error") is None and
            result.get("window_rect_format") == "qt-qrect-inclusive-ltrb" and result.get("presented_done") is None and
            result.get("full_active_grid_verified") is False and result.get("local_shell_transport_verified") is False,
            "Frame UI observation incomplete")
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
    for index in range(1, count):
        if index not in resize_phases(case):
            require(grids[index] == grids[index-1] and
                    all(grid == grids[index] for grid in history[freezes[index-1]-1:freezes[index]]),
                    "Unarmed per-stage NAWS change retained")
    require(all(grid == grids[-1] for grid in history[freezes[-1]-1:]), "Unarmed final NAWS change retained")
    records, observations, checks = peer["records"], result["observations"], result["geometry_checks"]
    require(len(records) == len(observations) == len(checks) == count and peer["phase"] == count-1 and peer["phase_count"] == count,
            "Fixed frame phases incomplete")
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
        require(type(terminal.get("markers")) is list and len(terminal["markers"]) <= 256, "Marker bound")
        require((directory / f"ack-{index}").read_text(encoding="utf-8") == "observed", "Phase acknowledgment missing")
        replies.extend(raw)
    require(peer["received_hex"] == replies.hex(), "Unassigned reply bytes retained")
    return {"schema": "termforge-resize-frame-mechanics-v1", "case_id": case, "stage_count": count, "dsr_count": count,
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
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in fixed_contract(case).items()), "Fixed frame contract differs")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Frame source identity changed")
    require(all(plan.get(k) == v for k, v in common.host_identity().items()), "Execution host/runtime differs")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    source, out, rea = [Path(plan[k]).resolve() for k in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "Frame run plan/output is not new/local")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(plan["rea_prepared_identity"] == common.rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write(ATTEMPT)
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_resize_frame_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"], str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(all(plan.get(k) == v for k, v in source_identity().items()), "Frame source identity changed during run")
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
