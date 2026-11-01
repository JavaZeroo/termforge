"""One fresh-session origin/margin observation for one immutable asymmetric case.

Source defaults to the current Git checkout. An exported tree may instead carry
ROOT/reference-source.json; its exact file set, hashes and stated base commit are
verified and recorded as an archive identity, never as a guest Git checkout.
"""
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
CASES = ("main-on-alt-off", "main-off-alt-on")
PHASE_COUNT = 14
ARCHIVE_MANIFEST = "reference-source.json"
ARCHIVE_SCHEMA = "termforge-mode-reference-source-v1"
SOURCE_FILES = ("research/ui/mode_state_reference.py", "research/ui/mode_state_reference.js",
                "research/ui/alternate_reference.py", "research/ui/r1_launch.py", "research/ui/launch.py",
                "research/ui/qt_r1_text.js", "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")
EXE_SHA256 = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"
MANIFEST_SHA256 = "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def phases(rows, columns, case):
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled grid out of bounds")
    require(case in CASES, "Unknown immutable case")
    esc = b"\x1b["
    cup = lambda row, column: f"\x1b[{row};{column}H".encode("ascii")
    main_mode, alternate_mode = (b"h", b"l") if case == CASES[0] else (b"l", b"h")
    sentinels = b"".join(cup(row, 1) + f"R{row:02}".encode("ascii") for row in range(1, 9))
    sentinels += cup(rows, 1) + b"SB"
    return [
        ("fresh-primary-sentinels", esc+b"?6l"+esc+b"?7h"+esc+b"r"+esc+b"2J"+sentinels),
        ("primary-known-origin", esc+b"2;5r"+esc+b"?6"+main_mode+cup(2,10)+b"MP"),
        ("first-alternate-enter", esc+b"?1049h"),
        # CUP(1,1) alone has the same DSR in both modes. Saturation plus marker
        # position distinguishes them only when the explicit controls differ.
        ("alternate-origin-sample", esc+b"3;7r"+cup(1,1)+b"AS"+cup(99,1)),
        ("alternate-origin-on-control", esc+b"?6h"+esc+b"3;7r"+cup(1,1)+b"AI"+cup(99,1)),
        ("alternate-origin-off-control", esc+b"?6l"+esc+b"3;7r"+cup(1,1)+b"AO"+cup(99,1)),
        ("alternate-opposite-origin", esc+b"?6"+alternate_mode+cup(2,10)+b"AP"),
        ("primary-return-untouched", esc+b"?1049l"),
        ("primary-origin-sample", cup(1,1)+b"PS"+cup(99,1)),
        ("primary-origin-on-control", esc+b"?6h"+cup(1,1)+b"PI"+cup(99,1)),
        ("primary-origin-off-control", esc+b"?6l"+cup(1,1)+b"PO"+cup(99,1)),
        ("primary-bottom-sentinels", sentinels+esc+b"?6h"+cup(99,1)),
        ("primary-bottom-newline", b"\r\nNW"),
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


def peer_class(case):
    """Reuse the protected Telnet parser in a private namespace, without changing older runs."""
    require(case in CASES, "Unknown immutable case")
    from research.ui import alternate_reference
    original = Path(alternate_reference.__file__)
    source = original.read_text(encoding="utf-8")
    replacements = {
        '"phase": None, "records": []}': '"phase": None, "records": [], "case": MODE_STATE_CASE}',
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
    namespace = {"__file__": str(original), "__name__": "termforge_mode_state_peer", "MODE_STATE_CASE": case}
    exec(compile(source, str(original), "exec"), namespace)
    namespace.update(phases=lambda rows, columns: phases(rows, columns, case), reply_observation=reply_observation)

    class BoundPeer(namespace["Peer"]):
        case_id = case

        def _on_naws(self):
            grid = [self.state["columns"], self.state["rows"]]
            history = self.state.setdefault("naws_history", [])
            if len(history) >= 64:
                raise RuntimeError("NAWS event bound")
            history.append(grid)
            self._record()
            if self.state.get("frozen_grid") and self.state["frozen_grid"] != grid:
                raise RuntimeError("NAWS changed after fixture freeze")

    return BoundPeer


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


def phase_records(case, rows=25, columns=91):
    return [{"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content),
             "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, content) in enumerate(phases(rows, columns, case))]


def fixed_contract(case):
    require(case in CASES, "Unknown immutable case")
    return {"schema": "termforge-mode-state-plan-v1", "issue": 27, "case_id": case,
            "maximum_original_runs": 1, "actual_original_runs": 0, "phase_count": PHASE_COUNT,
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "example_grid": [91, 25], "example_phases": phase_records(case),
            "semantic_acceptance": False,
            "interpretation_notes": [
                "Each case starts one fresh process/session; the opposite case needs its own new plan and attempt.",
                "Alternate sample follows DECSTBM; compare its DSR and marker location to explicit ON/OFF controls using the same DECSTBM.",
                "If controls coincide, origin remains unknown. No semantic answer is a success condition.",
                "Primary sample precedes explicit origin changes. Compare with same-frame ON/OFF controls and synthetic marker locations.",
                "Height 4/5/full-height alone cannot establish bounds; inspect marker locations and the R01..R08/SB/NW newline transition.",
                "Document lines include retained content, not a full active grid. Markers/rectangles are observations, not a private origin-bit read.",
            ],
            "reference_exe_sha256": EXE_SHA256, "reference_files_manifest_sha256": MANIFEST_SHA256}


def validate_fixed_contract(plan):
    for name, expected in fixed_contract(plan.get("case_id")).items():
        require(type(plan.get(name)) is type(expected) and plan[name] == expected, "Fixed source/bounds differ")


def launcher_source(case):
    require(case in CASES, "Unknown immutable case")
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    substitutions = {
        "from research.ui.r1_vt_fixture import Peer":
            f"from research.ui.mode_state_reference import peer_class\n        Peer = peer_class({case!r})",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")':
            'Path(__file__).with_name("mode_state_reference.js").read_text(encoding="utf-8")'
            f'.replace("__MODE_STATE_CASE_JSON__", {json.dumps(case)!r})',
    }
    for old, new in substitutions.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


def host_identity():
    return {"python_sha256": sha(Path(sys.executable)), "python_version": sys.version,
            "windows_version": platform.version()}


def source_identity(*, root=None, source_files=None):
    """Verify source bytes and distinguish a checkout from a supplied archive."""
    root = ROOT if root is None else root
    source_files = SOURCE_FILES if source_files is None else source_files
    actual = {name: sha(root / name) for name in source_files}
    manifest = root / ARCHIVE_MANIFEST
    if manifest.exists():
        raw = manifest.read_bytes()
        require(len(raw) <= 16384, "Source archive manifest exceeds bound")

        def unique_fields(pairs):
            fields = {}
            for key, value in pairs:
                require(key not in fields, "Duplicate source archive manifest field")
                fields[key] = value
            return fields

        value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_fields)
        require(type(value) is dict and set(value) == {"schema", "source_base_commit", "source_files_sha256"},
                "Source archive manifest fields differ")
        require(value["schema"] == ARCHIVE_SCHEMA, "Source archive manifest schema differs")
        base = value["source_base_commit"]
        hashes = value["source_files_sha256"]
        require(type(hashes) is dict and set(hashes) == set(source_files) and
                all(type(digest) is str and re.fullmatch(r"[0-9a-f]{64}", digest) for digest in hashes.values()),
                "Source archive file/hash set differs")
        require(hashes == actual, "Source archive bytes differ from manifest")
        identity = {"kind": "source-archive", "manifest": ARCHIVE_MANIFEST,
                    "manifest_sha256": hashlib.sha256(raw).hexdigest()}
    else:
        base = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        identity = {"kind": "git-checkout"}
    require(type(base) is str and re.fullmatch(r"[0-9a-f]{40}", base), "Source base commit differs")
    return {"source_identity": identity, "source_base_commit": base, "source_files_sha256": actual}


def verify_source_identity(plan):
    current = source_identity()
    require(all(plan.get(key) == value for key, value in current.items()), "Source identity changed after plan")


def audit_mechanics(directory, case):
    """Check observation completeness, without inventing any terminal-semantic answer."""
    require(case in CASES, "Unknown immutable case")
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    require((directory / "stderr.txt").read_bytes() == b"" and not (directory / "launcher-failure.json").exists(),
            "REA stderr/launcher failure retained")
    peer, result, cleanup, profile = [read(name) for name in
        ("peer.json", "mode-state-result.json", "cleanup.json", "profile-check.json")]
    require(peer.get("case") == result.get("case") == case, "Plan/peer/UI case differs")
    require(all(peer.get(name) is True for name in ("connected", "closed", "complete")) and not peer.get("error"),
            "Peer did not complete cleanly")
    require(result.get("status") == "observed-all-mode-state-phases" and result.get("error") is None and
            result.get("presented_done") is None and all(result.get(name) is False for name in
            ("full_active_grid_verified", "terminal_style_values_verified", "local_shell_transport_verified")),
            "UI observation is partial or claims an unknown endpoint")
    require(type(cleanup.get("active")) is int and cleanup["active"] == 0 and
            all(type(cleanup.get(name)) is int and cleanup[name] >= 0 for name in ("total", "terminated")) and
            profile.get("only_synthetic_session") is True and type(profile.get("session_count")) is int and
            profile["session_count"] == 1, "Owned Job/profile cleanup differs")
    grid = peer["frozen_grid"]
    require(type(grid) is list and len(grid) == 2, "Frozen grid differs")
    expected = phase_records(case, grid[1], grid[0])
    history, freeze = peer["naws_history"], peer["freeze_event_count"]
    require(type(history) is list and 1 <= len(history) <= 64 and type(freeze) is int and
            1 <= freeze <= len(history) and all(type(event) is list and len(event) == 2 and
            all(type(n) is int and 0 <= n <= 65535 for n in event) for event in history) and
            history[freeze-1] == grid and all(event == grid for event in history[freeze:]), "NAWS history changed or is incomplete")
    records, observations = peer["records"], result["observations"]
    require(len(records) == len(observations) == PHASE_COUNT and peer["phase_count"] == PHASE_COUNT and peer["phase"] == PHASE_COUNT-1 and
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
    return {"schema": "termforge-mode-state-mechanics-v1", "case_id": case, "stage_count": PHASE_COUNT, "dsr_count": PHASE_COUNT,
            "frozen_grid": grid, "naws_events": len(history), "job_active": 0,
            "semantic_acceptance": False, "presented_done": None}


def prepare_plan(source, out, rea, destination, case, execution_authorized=False):
    source, out, rea, destination = [path.resolve() for path in (source, out, rea, destination)]
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            destination.is_relative_to(ROOT / "local"), "Plan/output must belong to this worktree local directory")
    require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    plan = {**fixed_contract(case), **source_identity(), "execution_authorized": execution_authorized,
            "source": str(source), "out": str(out), "rea": str(rea), "rea_prepared_identity": rea_identity(rea),
            "launcher_source_sha256": hashlib.sha256(launcher_source(case).encode()).hexdigest(), **host_identity(),
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
    case = plan["case_id"]
    verify_source_identity(plan)
    require(plan["launcher_source_sha256"] == hashlib.sha256(launcher_source(case).encode()).hexdigest(), "Launcher source differs")
    require(all(plan[key] == value for key, value in host_identity().items()), "Execution host/runtime differs")
    source, out, rea = [Path(plan[key]).resolve() for key in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out != ROOT / "local" and out.is_relative_to(ROOT / "local") and
            not out.exists(), "This run plan/output is not new/local")
    require(plan["rea_prepared_identity"] == rea_identity(rea), "Prepared REA entry changed")
    require(sha(source / "WindTerm.exe") == EXE_SHA256, "Pinned original executable differs")
    # The inherited copier validates the complete 4,047-file distribution before launch.
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One original launch attempt; retain after failure and never retry this plan.\n")
    namespace = {"__file__": str(ROOT / "research/ui/r1_launch.py"), "__name__": "termforge_mode_state_launcher"}
    exec(compile(launcher_source(case), namespace["__file__"], "exec"), namespace)
    # The inherited run() resolves this name in its own namespace. After the
    # pre-attempt checks it must use the bound executable, never select PATH again.
    namespace["cli_command"] = lambda _: [plan["rea_prepared_identity"]["node_executable"],
                                          str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
    namespace["run"](source, out, rea, 45, False, "menu-wrap")
    verify_source_identity(plan)
    mechanics = audit_mechanics(out, case)
    (out / "mechanics-check.json").write_text(json.dumps(mechanics, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--authorize-execution", action="store_true")
    parser.add_argument("--case", choices=CASES)
    for name in ("source", "out", "rea"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea and args.case, "Source/output/REA/case required")
        prepare_plan(args.source, args.out, args.rea, args.plan, args.case, args.authorize_execution)
        print(json.dumps({"prepared": True, "case_id": args.case, "phase_count": PHASE_COUNT, "maximum_original_runs": 1,
                          "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution and args.case is None and args.source is None and args.out is None and args.rea is None,
                "Execution uses only the immutable plan; preparation options cannot override it")
        run_plan(args.plan)
