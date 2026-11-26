"""Observe RIS across primary and alternate buffers in one owned Telnet session."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import mode_state_reference as base

CASE_ID = "alternate-ris"
PHASE_COUNT = 10
MARKERS = ("HIST", "MAIN", "ALT", "AFTER")
RIS_PHASES = (2, 9)
FLOW_CONTROL = b"\x13\x11"
SOURCE_FILES = ("research/ui/alternate_ris_reference.py", *base.SOURCE_FILES)
EXE_SHA256, MANIFEST_SHA256 = base.EXE_SHA256, base.MANIFEST_SHA256
require = base.require


def phases(rows, columns, case):
    require(case == CASE_ID, "Unexpected RIS case")
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled grid out of bounds")
    return [
        ("primary-with-history-and-margin", b"\x1b[?6l\x1b[r\x1b[2J\x1b[1;1HHIST" + f"\x1b[{rows};1H".encode() +
         b"\r\n\r\n\x1b[4;2HMAIN\x1b[2;6r\x1b[?6h\x1b[2;7H"),
        ("alternate-with-margin", b"\x1b[?1049h\x1b[3;7r\x1b[?6h\x1b[2;4HALT"),
        ("ris-in-alternate", b"\x1bc"),
        ("implicit-origin-after-ris", b"\x1b[1;1HAFTER\x1b[99;1H"),
        ("explicit-origin-after-ris", b"\x1b[?6h\x1b[99;1H"),
        ("explicit-primary-return", b"\x1b[?1049l"),
        ("alternate-reentry", b"\x1b[?1049h"),
        ("explicit-origin-after-reentry", b"\x1b[?6h\x1b[99;1H"),
        ("final-primary-return", b"\x1b[?1049l"),
        ("ris-in-primary", b"\x1bc"),
    ]


def substitute(source, substitutions):
    for old, new in substitutions.items():
        require(source.count(old) == 1, "Protected RIS adapter changed")
        source = source.replace(old, new)
    return source


def observed_reply(data, phase):
    # The reset prefix is part of the observed wire response. It is only
    # allowed before the DSR in the two phases that actually send RIS.
    if phase in RIS_PHASES and data.startswith(FLOW_CONTROL[:1]):
        if data == FLOW_CONTROL[:1]:
            return None
        require(data.startswith(FLOW_CONTROL), "Unexpected RIS flow-control prefix")
        reply = base.reply_observation(data[len(FLOW_CONTROL):])
        if reply is not None:
            reply.update(response_hex=data.hex(), flow_control_hex=FLOW_CONTROL.hex())
        return reply
    return base.reply_observation(data)


def javascript_source():
    source = (ROOT / "research/ui/mode_state_reference.js").read_text(encoding="utf-8")
    return substitute(source, {
        "__MODE_STATE_CASE_JSON__": json.dumps(CASE_ID),
        "phaseCount=14": f"phaseCount={PHASE_COUNT}",
        "['main-on-alt-off','main-off-alt-on']": json.dumps([CASE_ID]),
        "['MP','AS','AI','AO','AP','PS','PI','PO','NW','SB','R01','R02','R03','R04','R05','R06','R07','R08']":
            json.dumps(list(MARKERS)),
        "response_hex:record.response_hex,frozen_grid":
            "response_hex:record.response_hex,flow_control_hex:record.flow_control_hex,frozen_grid",
    })


def launcher_source(case):
    require(case == CASE_ID, "Unexpected RIS case")
    source = (ROOT / "research/ui/r1_launch.py").read_text(encoding="utf-8")
    return substitute(source, {
        "from research.ui.r1_vt_fixture import Peer":
            "from research.ui.alternate_ris_reference import peer_class, javascript_source\n"
            f"        Peer = peer_class({case!r})",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "javascript_source()",
    })


_runtime = {"__file__": __file__, "__name__": "termforge_alternate_ris_runtime"}
source = substitute(Path(base.__file__).read_text(encoding="utf-8"), {
    "observed = reply_observation(raw)": "observed = observed_reply(raw, index)",
    "        reply = reply_observation(raw)": '        reply = observed_reply(raw, row["phase"])',
    "reply_observation=reply_observation)": "observed_reply=observed_reply)",
})
exec(compile(source, str(Path(base.__file__)), "exec"), _runtime)
_runtime.update(CASES=(CASE_ID,), PHASE_COUNT=PHASE_COUNT, SOURCE_FILES=SOURCE_FILES,
                phases=phases, launcher_source=launcher_source, observed_reply=observed_reply)
_base_contract = _runtime["fixed_contract"]


def fixed_contract(case):
    contract = _base_contract(case)
    contract.update(schema="termforge-alternate-ris-plan-v2",
                    generated_driver_sha256=hashlib.sha256(javascript_source().encode()).hexdigest(),
                    interpretation_notes=[
                        "Seed primary history and distinct non-default margins in both buffers, then send RIS in the alternate buffer.",
                        "Observe unmodified DSR and buffer flags before any addressing. Compare saturated CUP before and after explicit origin enable.",
                        "Observe explicit return, reentry, and a primary RIS control in the same session.",
                        "Only RIS phases may carry the observed XOFF/XON pair before DSR. Retain the complete response bytes.",
                        "The document may include both buffers. Document markers and flags remain separate from a full active grid or style audit.",
                    ])
    return contract


_runtime["fixed_contract"] = fixed_contract
for _name in ("peer_class", "phase_records", "validate_fixed_contract", "prepare_plan", "run_plan", "audit_mechanics", "source_identity"):
    globals()[_name] = _runtime[_name]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea, "Source/output/REA required")
        prepare_plan(args.source, args.out, args.rea, args.plan, CASE_ID, args.authorize_execution)
    else:
        require(not args.authorize_execution and not any((args.source, args.out, args.rea)),
                "Execution uses only the immutable plan")
        run_plan(args.plan)
