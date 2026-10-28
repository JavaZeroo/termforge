"""One original origin/wrap/margin observation, guarded by an immutable run plan."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
CASE_ID = "origin-wrap-margin-v1"
SOURCE_FILES = ("research/ui/origin_wrap_reference.py", "research/ui/origin_wrap_reference.js",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")
EXE_SHA256 = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"
MANIFEST_SHA256 = "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def phases(rows, columns):
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled grid out of bounds")
    esc = b"\x1b["
    cup = lambda row, column: f"\x1b[{row};{column}H".encode("ascii")
    return [
        ("fresh-session-cup", esc+b"2J"+esc+b"HFT"),
        ("explicit-origin-wrap-reset", esc+b"?6l"+esc+b"?7h"+esc+b"r"+esc+b"2J"+esc+b"H"),
        ("main-margin-set", esc+b"2;5r"),
        ("origin-reset-cup", cup(1,3)+b"D"),
        ("origin-set", esc+b"?6h"),
        ("origin-set-cup", cup(1,3)+b"O"),
        ("origin-set-upper-relative-bound", esc+b"99A"+esc+b"99D"),
        ("origin-set-lower-relative-bound", esc+b"99B"),
        ("origin-reset-after-margin", esc+b"?6l"),
        ("region-sentinels", esc+b"r"+esc+b"2J"+cup(1,1)+b"TP"+cup(rows,1)+b"BT"+
         cup(2,1)+b"R0"+cup(3,1)+b"R1"+cup(4,1)+b"R2"+cup(5,1)+b"R3"+esc+b"2;5r"),
        ("region-bottom-newline", cup(5,1)+b"\r\nNW"),
        ("above-region-newline", cup(1,1)+b"\nUP"),
        ("below-region-newline", cup(rows,1)+b"\nDN"),
        ("main-margin-reset", esc+b"r"),
        # The two letters share a source packet with no DSR between them.
        ("wrap-enabled-edge", esc+b"?7h"+cup(8,columns)+b"AB"),
        ("wrap-disabled-edge", esc+b"?7l"+cup(10,columns)+b"CD"),
        ("wrap-enabled-again-edge", esc+b"?7h"+cup(12,columns)+b"EF"),
        ("pending-edge-disable-before-next", cup(14,columns)+b"G"+esc+b"?7lH"),
        ("disabled-edge-enable-before-next", esc+b"?7l"+cup(16,columns)+b"I"+esc+b"?7hJ"),
        ("main-margin-origin-before-alt", esc+b"?7h"+esc+b"2;5r"+esc+b"?6h"+cup(2,4)+b"M"),
        ("alternate-enter", esc+b"?1049h"),
        ("alternate-margin-origin", esc+b"3;6r"+esc+b"?6h"+cup(2,4)+b"A"),
        ("alternate-exit", esc+b"?1049l"),
        ("main-cup-after-alternate", cup(1,6)+b"R"),
        ("final-explicit-reset", esc+b"?6l"+esc+b"?7h"+esc+b"r"+cup(1,1)+b"DONE"),
    ]


def reply_observation(data):
    """Return a complete DSR observation, or wait for a split reply prefix."""
    match = re.fullmatch(rb"\x1b\[(\??)([0-9]{1,5});([0-9]{1,5})R", data)
    if match is None:
        # A receive can split inside the ESC/CSI header or either coordinate.
        # Other bytes, additional replies and tails must never be discarded.
        if data in (b"", b"\x1b") or re.fullmatch(rb"\x1b\[\??[0-9]{0,5}(?:;[0-9]{0,5})?", data):
            return None
        raise ValueError("Unexpected DSR response bytes")
    return {"cursor_reply": [int(match[2]), int(match[3])],
            "cursor_reply_private": match[1] == b"?", "response_hex": data.hex()}


def peer_class():
    """Reuse the protected Telnet parser in a private namespace, without changing older runs."""
    from research.ui import alternate_reference
    original = Path(alternate_reference.__file__)
    source = original.read_text(encoding="utf-8")
    replacements = {
        'if self.state["rows"] and selected is None:':
            'if self.state["rows"] and selected is None and (self.directory / "fixture-ready").exists():',
        'selected = phases(self.state["rows"])':
            'self.state["frozen_grid"] = [self.state["columns"], self.state["rows"]]\n                    self.state["freeze_event_count"] = len(self.state.get("naws_history", []))\n                    selected = phases(self.state["rows"], self.state["columns"])',
        'self.state["rows"] = int.from_bytes(sub[3:5], "big")':
            'self.state["rows"] = int.from_bytes(sub[3:5], "big")\n                                self._on_naws()',
        '                if index >= 0:\n                    matches = list(re.finditer(rb"\\x1b\\[(\\??)(\\d+);(\\d+)R", bytes(received[reply_start:])))\n                    self.state["records"][index]["cursor_reply"] = [int(matches[-1][2]),int(matches[-1][3])] if matches else None':
            '                self.state["received_hex"] = received.hex()\n                if index >= 0:\n                    raw = bytes(received[reply_start:])\n                    self.state["records"][index]["response_hex"] = raw.hex()\n                    observed = reply_observation(raw)\n                    if observed is not None:\n                        self.state["records"][index].update(observed)',
        'if selected and (index == -1 or (self.directory / f"ack-{index}").exists()):':
            'if selected and (index == -1 or ((self.directory / f"ack-{index}").exists() and self.state["records"][index]["cursor_reply"] is not None)):',
    }
    for old, new in replacements.items():
        require(source.count(old) == 1, "Protected peer source changed")
        source = source.replace(old, new)
    namespace = {"__file__": str(original), "__name__": "termforge_origin_wrap_peer"}
    exec(compile(source, str(original), "exec"), namespace)
    namespace.update(phases=phases, reply_observation=reply_observation)
    return namespace["Peer"]


class Peer(peer_class()):
    def _on_naws(self):
        grid = [self.state["columns"], self.state["rows"]]
        history = self.state.setdefault("naws_history", [])
        if len(history) >= 64:
            raise RuntimeError("NAWS event bound")
        history.append(grid)
        self._record()
        if self.state.get("frozen_grid") and self.state["frozen_grid"] != grid:
            raise RuntimeError("NAWS changed after fixture freeze")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rea_identity(rea):
    package = rea.parent.parent / "rea-agents/package.json"
    metadata = json.loads(package.read_text(encoding="utf-8"))
    require(rea.is_file() and metadata["name"] == "rea-agents" and metadata["version"] == "6.7.0" and
            metadata["bin"]["rea"] == "scripts/rea.mjs", "Locked REA version/entry differs")
    selected = shutil.which("node")
    require(selected is not None, "Prepared Node executable is unavailable")
    node = Path(selected).resolve()
    require(node.is_file(), "Prepared Node executable is not a file")
    return {"version": "6.7.0", "shim_sha256": sha(rea), "package_json_sha256": sha(package),
            "entry_sha256": sha(package.parent / "scripts/rea.mjs"),
            "node_executable": str(node), "node_executable_sha256": sha(node)}


def phase_records(rows=25, columns=91):
    return [{"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content),
             "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, content) in enumerate(phases(rows, columns))]


def fixed_contract():
    return {"schema": "termforge-origin-wrap-plan-v1", "issue": 151, "case_id": CASE_ID,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": 25,
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "example_grid": [91, 25], "example_phases": phase_records(),
            "reference_exe_sha256": EXE_SHA256, "reference_files_manifest_sha256": MANIFEST_SHA256}


def validate_fixed_contract(plan):
    for name, expected in fixed_contract().items():
        require(type(plan.get(name)) is type(expected) and plan[name] == expected, "Fixed source/bounds differ")


def launcher_source():
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    substitutions = {
        "from research.ui.r1_vt_fixture import Peer": "from research.ui.origin_wrap_reference import Peer",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")':
            'Path(__file__).with_name("origin_wrap_reference.js").read_text(encoding="utf-8")',
    }
    for old, new in substitutions.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def host_identity():
    return {"python_sha256": sha(Path(sys.executable)), "python_version": sys.version,
            "windows_version": platform.version()}


def audit_mechanics(directory):
    """Check observation completeness, without inventing any terminal-semantic answer."""
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in
        ("peer.json", "origin-wrap-result.json", "cleanup.json", "profile-check.json")]
    require(all(peer.get(name) is True for name in ("connected", "closed", "complete")) and not peer.get("error"),
            "Peer did not complete cleanly")
    require(result.get("status") == "observed-all-origin-wrap-phases" and result.get("error") is None and
            result.get("presented_done") is None and all(result.get(name) is False for name in
            ("full_active_grid_verified", "terminal_style_values_verified", "local_shell_transport_verified")),
            "UI observation is partial or claims an unknown endpoint")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(name)) is int and cleanup[name] >= 0 for name in ("total", "terminated")) and
            profile.get("only_synthetic_session") is True and type(profile.get("session_count")) is int and
            profile["session_count"] == 1, "Owned Job/profile cleanup differs")
    grid = peer["frozen_grid"]
    require(type(grid) is list and len(grid) == 2, "Frozen grid differs")
    expected = phase_records(grid[1], grid[0])
    history, freeze = peer["naws_history"], peer["freeze_event_count"]
    require(type(history) is list and 1 <= len(history) <= 64 and type(freeze) is int and
            1 <= freeze <= len(history) and all(type(event) is list and len(event) == 2 and
            all(type(n) is int and 0 <= n <= 65535 for n in event) for event in history) and
            history[freeze-1] == grid and all(event == grid for event in history[freeze:]), "NAWS history changed or is incomplete")
    records, observations = peer["records"], result["observations"]
    require(len(records) == len(observations) == 25 and peer["phase_count"] == 25 and peer["phase"] == 24 and
            peer["phase_label"] == expected[-1]["label"], "Stage sequence is incomplete")
    replies = bytearray()
    for source, row, observation in zip(expected, records, observations):
        require(all(row.get(key) == value for key, value in source.items()), "Actual frozen source differs")
        raw = bytes.fromhex(row["response_hex"])
        reply = reply_observation(raw)
        require(reply is not None and all(row.get(key) == value for key, value in reply.items()), "DSR observation differs")
        replies.extend(raw)
        require(all(observation.get(key) == row[key] for key in ("phase", "label", *reply)) and
                observation["frozen_grid"] == grid, "UI/peer phase observations differ")
        terminal = observation["terminal"]
        require(terminal["connected"] is True and terminal["protocol"] == "Telnet" and
                type(terminal["capacity"]) is int and terminal["capacity"] == grid[1] and
                type(terminal["line_count"]) is int and 0 <= terminal["line_count"] <= 200 and
                type(terminal["lines"]) is list and len(terminal["lines"]) == terminal["line_count"] and
                all(type(line) is str and len(line) <= 1024 for line in terminal["lines"]), "Document observation bounds differ")
        require((directory / f'ack-{row["phase"]}').read_text(encoding="utf-8") == "observed", "UI acknowledgment differs")
    require(peer["received_hex"] == replies.hex(), "Unassigned received bytes retained")
    return {"schema": "termforge-origin-wrap-mechanics-v1", "stage_count": 25, "dsr_count": 25,
            "frozen_grid": grid, "naws_events": len(history), "job_active": 0,
            "semantic_acceptance": False, "presented_done": None}


def prepare_plan(source, out, rea, destination, execution_authorized=False):
    source, out, rea, destination = [path.resolve() for path in (source, out, rea, destination)]
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            destination.is_relative_to(ROOT / "local"), "Plan/output must belong to this worktree local directory")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    plan = {**fixed_contract(), "execution_authorized": execution_authorized,
            "source_base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "source": str(source), "out": str(out), "rea": str(rea), "rea_prepared_identity": rea_identity(rea),
            "source_files_sha256": {name: sha(ROOT / name) for name in SOURCE_FILES},
            "launcher_source_sha256": hashlib.sha256(launcher_source().encode()).hexdigest(), **host_identity(),
            "source_freeze": "Stable actual NAWS/capacity before first source; retain every complete NAWS; any subsequent grid change fails",
            "scope": "Direct DSR and synthetic document text only; document is not the active screen grid",
            "stop": "Identity/protocol/NAWS/stage/timeout/error stops the single attempt; no retry or budget extension"}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(plan, indent=2) + "\n")
    return plan


def run_plan(path):
    path = path.resolve()
    plan = json.loads(path.read_text(encoding="utf-8"))
    require(plan.get("execution_authorized") is True, "Offline proposal only; original execution has not been authorized")
    validate_fixed_contract(plan)
    require(plan["source_base_commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "Source commit changed after plan")
    require(plan["source_files_sha256"] == {name: sha(ROOT / name) for name in SOURCE_FILES}, "Probe source changed after plan")
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source().encode()).hexdigest(), "Launcher source differs")
    require(all(plan[key] == value for key, value in host_identity().items()), "Execution host/runtime differs")
    source, out, rea = [Path(plan[key]).resolve() for key in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "This run plan/output is not new/local")
    require(plan["rea_prepared_identity"] == rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    # The inherited copier validates the complete 4,047-file distribution before launch.
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One original launch attempt; retain after failure and never retry this plan.\n")
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_origin_wrap_launcher"}
    exec(compile(launcher_source(), namespace["__file__"], "exec"), namespace)
    # The inherited run() resolves this name in its own namespace. After the
    # pre-attempt checks it must use the bound executable, never select PATH again.
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"],
                                          str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    require(plan["source_files_sha256"] == {name: sha(ROOT / name) for name in SOURCE_FILES}, "Probe source changed during execution")
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
        print(json.dumps({"prepared": True, "phase_count": 25, "maximum_original_runs": 1,
                          "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution, "Authorization belongs to preparation of a fresh plan")
        run_plan(args.plan)
