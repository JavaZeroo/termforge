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

CASE_ID = "grid-style-switch"
PHASE_COUNT = 14
MARKERS = ("GRID",)
RIS_PHASES = (7, 13)
FLOW_CONTROL = b"\x13\x11"
SOURCE_FILES = ("research/ui/alternate_ris_reference.py", "research/ui/grid_style_capture.js", "research/ui/raster_raw_reference.js", *base.SOURCE_FILES)
EXE_SHA256, MANIFEST_SHA256 = base.EXE_SHA256, base.MANIFEST_SHA256
require = base.require


def phases(rows, columns, case):
    require(case == CASE_ID, "Unexpected RIS case")
    require(type(rows) is int and type(columns) is int and 20 <= rows <= 60 and 40 <= columns <= 160,
            "Controlled grid out of bounds")
    require((columns, rows) == (91, 25), "fixed grid")
    def cup(r, c): return f"\x1b[{r};{c}H".encode()
    def sgr(v): return f"\x1b[0;{v}m".encode()
    modes = ("0", "1", "31", "31;1", "91", "2", "4", "7", "9", "8", "53", "44", "33;44")
    def controls(row):
        return b"".join(cup(row, 5+5*i) + sgr(v) + b"M M " for i, v in enumerate(modes))
    park = b"\x1b[?25l" + cup(24,80)
    return [
        ("primary-controls", b"\x1b[0m\x1b[2J" + cup(2,5) + b"GRID" + controls(4) + sgr("31;1") + park),
        ("alternate-entry", b"\x1b[?1049h"),
        ("alternate-controls", cup(8,5) + b"\x1b[31mM M " + controls(10) + sgr("31;1") + park),
        ("repeat-entry", b"\x1b[?1049h"),
        ("repeat-first-write", cup(12,5) + b"\x1b[31mM M " + park),
        ("ordinary-return", b"\x1b[?1049l" + cup(20,5) + b"\x1b[31mM M " + park),
        ("alternate-before-ris", b"\x1b[?1049h" + cup(14,5) + sgr("4") + b"M M " + park),
        ("alternate-ris", b"\x1bc"),
        ("primary-first-write", cup(16,5) + b"M M " + park),
        ("explicit-return", b"\x1b[?1049l"),
        ("empty-reentry", b"\x1b[?1049h"),
        ("reentry-first-write", cup(18,5) + b"\x1b[31mM M " + park),
        ("final-return", b"\x1b[?1049l"),
        ("primary-ris", b"\x1bc"),
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
    raw = (ROOT / "research/ui/raster_raw_reference.js").read_text(encoding="utf-8")
    capture = raw[raw.index("function modulePathMatches"):raw.index("function must(")]
    capture += (ROOT / "research/ui/grid_style_capture.js").read_text(encoding="utf-8")
    return capture + substitute(source, {
        "const observation={phase:": "const observation={grid_capture:captureGrid(peer.phase, terminal),phase:",
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
