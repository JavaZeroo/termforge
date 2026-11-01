"""One bounded observation of origin across duplicate and later 1049 entry.

The frozen mode-state implementation is evaluated in a private namespace. Its
parser, source-archive identity, launch/cleanup and one-attempt guards remain the
same; only this case's input, contract and generated UI driver are substituted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as base

CASE_ID = "alternate-origin-reentry"
CASES = (CASE_ID,)
PHASE_COUNT = 14
SOURCE_FILES = ("research/ui/mode_reentry_reference.py", *base.SOURCE_FILES)
MARKERS = ("MP", "A0", "DS", "DI", "DO", "A1", "ES", "EI", "EO",
           "R01", "R02", "R03", "R04", "R05", "R06", "R07", "R08")
require = base.require
sha = base.sha
EXE_SHA256 = base.EXE_SHA256
MANIFEST_SHA256 = base.MANIFEST_SHA256
ARCHIVE_MANIFEST = base.ARCHIVE_MANIFEST
ARCHIVE_SCHEMA = base.ARCHIVE_SCHEMA


def phases(rows, columns, case):
    require(case == CASE_ID, "Unknown immutable reentry case")
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled grid out of bounds")
    esc = b"\x1b["
    cup = lambda row, column: f"\x1b[{row};{column}H".encode("ascii")
    sentinels = b"".join(cup(row, 1)+f"R{row:02}".encode("ascii") for row in range(1, 9))
    return [
        ("primary-known-origin-on", esc+b"?6l"+esc+b"?7h"+esc+b"r"+esc+b"2J"+sentinels+
         esc+b"2;5r"+esc+b"?6h"+cup(2, 10)+b"MP"),
        ("first-alternate-known-origin-on", esc+b"?1049h"+esc+b"3;7r"+esc+b"?6h"+cup(2, 10)+b"A0"),
        ("duplicate-alternate-enter-untouched", esc+b"?1049h"),
        ("duplicate-origin-sample", esc+b"3;7r"+cup(1, 1)+b"DS"+cup(99, 1)),
        ("duplicate-origin-on-control", esc+b"?6h"+esc+b"3;7r"+cup(1, 1)+b"DI"+cup(99, 1)),
        ("duplicate-origin-off-control", esc+b"?6l"+esc+b"3;7r"+cup(1, 1)+b"DO"+cup(99, 1)),
        ("alternate-known-origin-on-before-exit", esc+b"?6h"+cup(2, 10)+b"A1"),
        ("first-primary-return-untouched", esc+b"?1049l"),
        ("later-alternate-enter-untouched", esc+b"?1049h"),
        ("later-origin-sample", esc+b"3;7r"+cup(1, 1)+b"ES"+cup(99, 1)),
        ("later-origin-on-control", esc+b"?6h"+esc+b"3;7r"+cup(1, 1)+b"EI"+cup(99, 1)),
        ("later-origin-off-control", esc+b"?6l"+esc+b"3;7r"+cup(1, 1)+b"EO"+cup(99, 1)),
        ("final-primary-return-untouched", esc+b"?1049l"),
        ("final-explicit-reset", esc+b"?6l"+esc+b"?7h"+esc+b"r"+cup(1, 1)+b"DONE"),
    ]


def javascript_source():
    source = (ROOT / "research/ui/mode_state_reference.js").read_text(encoding="utf-8")
    old_markers = "['MP','AS','AI','AO','AP','PS','PI','PO','NW','SB','R01','R02','R03','R04','R05','R06','R07','R08']"
    substitutions = {
        "['main-on-alt-off','main-off-alt-on']": json.dumps(list(CASES)),
        "__MODE_STATE_CASE_JSON__": json.dumps(CASE_ID),
        old_markers: json.dumps(list(MARKERS)),
        "'mode-state-v1'": "'mode-reentry-v1'",
        "'/mode-state-result.json'": "'/mode-reentry-result.json'",
        "'mode_state_result'": "'mode_reentry_result'",
        "'mode_state_phase'": "'mode_reentry_phase'",
        "'observed-all-mode-state-phases'": "'observed-all-mode-reentry-phases'",
    }
    for old, new in substitutions.items():
        require(source.count(old) == 1, "Protected mode UI driver changed")
        source = source.replace(old, new)
    return source


def launcher_source(case):
    require(case == CASE_ID, "Unknown immutable reentry case")
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    substitutions = {
        "from research.ui.r1_vt_fixture import Peer":
            f"from research.ui.mode_reentry_reference import peer_class, javascript_source\n        Peer = peer_class({case!r})",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "javascript_source()",
    }
    for old, new in substitutions.items():
        require(source.count(old) == 1, "Protected launcher source changed")
        source = source.replace(old, new)
    return source


_RUNTIME = {"__file__": __file__, "__name__": "termforge_mode_reentry_runtime"}
_implementation = Path(base.__file__).read_text(encoding="utf-8").replace("mode-state-", "mode-reentry-")
exec(compile(_implementation, str(Path(base.__file__)), "exec"), _RUNTIME)
_RUNTIME.update(CASES=CASES, SOURCE_FILES=SOURCE_FILES, phases=phases, launcher_source=launcher_source)
_original_contract = _RUNTIME["fixed_contract"]


def fixed_contract(case):
    value = _original_contract(case)
    value["generated_driver_sha256"] = hashlib.sha256(javascript_source().encode()).hexdigest()
    value["interpretation_notes"] = [
        "One fresh process observes duplicate 1049h while alternate origin was explicitly ON, then exit and later entry after restoring alternate ON.",
        "Duplicate/later samples reapply DECSTBM 3..7 without changing origin; compare with explicit ON/OFF controls using the same DECSTBM.",
        "CUP(1,1) DSR alone is not discriminating; retain saturated CUP(99,1) DSR and independent DS/DI/DO or ES/EI/EO marker locations.",
        "Equal or inconsistent controls leave origin unknown. No semantic answer is a mechanical success condition.",
        "Main is explicitly ON with margin 2..5; raw return replies remain observations, not a direct read of private mode storage.",
        "Document lines include retained content rather than a complete active grid; no full-grid, style, resize, local-shell or per-frame implementation acceptance follows.",
    ]
    return value


_RUNTIME["fixed_contract"] = fixed_contract
for _name in ("peer_class", "phase_records", "validate_fixed_contract", "prepare_plan", "run_plan", "audit_mechanics",
              "reply_observation", "rea_identity", "source_identity", "host_identity"):
    globals()[_name] = _RUNTIME[_name]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--"+name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea, "Source/output/REA required")
        prepare_plan(args.source, args.out, args.rea, args.plan, CASE_ID, args.authorize_execution)
        print(json.dumps({"prepared": True, "case_id": CASE_ID, "phase_count": PHASE_COUNT,
                          "maximum_original_runs": 1, "execution_authorized": args.authorize_execution}))
    else:
        require(not args.authorize_execution and args.source is None and args.out is None and args.rea is None,
                "Execution uses only the immutable plan")
        run_plan(args.plan)
