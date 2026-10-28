"""Separate selection-gate replays; never join endpoints with another run's ROI."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.raster_selection_analysis import require,require_collapsed


def decision(caret,anchor):
    try:require_collapsed(caret,anchor)
    except ValueError as error:return {"selection_gate_allowed":False,"reason":str(error)}
    return {"selection_gate_allowed":True,"reason":None}


def make_replay(r126_result,destination):
    prior=json.loads((ROOT/"research/ui/results/2026-10-28-raster-style-reference/selection-getter-counterexample.json").read_text(encoding="utf-8"))
    current=json.loads((ROOT/"research/ui/results/2026-10-28-raster-style-reference/validation.json").read_text(encoding="utf-8"))
    raw=r126_result.read_bytes()
    raw_sha=hashlib.sha256(raw).hexdigest()
    require(raw_sha==current["raw_files"]["raster-result.json"]["sha256"],"R126 private phase0 identity differs")
    original=json.loads(raw)
    require(original["status"]=="partial-raster-observation" and original["grab_phases"]==[] and len(original["captures"])==1,"R126 boundary differs")
    terminal=original["captures"][0]["terminal"]
    require("selection_caret" not in terminal and "selection_anchor" not in terminal,"R126 unexpectedly contains selection endpoints")
    rows=[]
    for index,row in enumerate(prior["counterexamples"]):
        caret,anchor=row["caret"],row["anchor"]
        rows.append({"id":f"historical-collapsed-{index}","source_evidence_id":prior["source_evidence_id"],"source_file_sha256":prior["source_file_sha256"],
                     "selection_caret":caret,"selection_anchor":anchor,"selection_text_type":"string","selection_text_length":row["selection_length"],**decision(caret,anchor)})
    text=terminal.get("selection")
    rows.append({"id":"r126-actual-phase0-missing-endpoints","source_evidence_id":current["provider"]["evidence_id"],"source_file_sha256":raw_sha,
                 "selection_caret":None,"selection_anchor":None,"selection_text_type":"string" if isinstance(text,str) else "null" if text is None else "other",
                 "selection_text_length":len(text) if isinstance(text,str) else None,**decision(None,None)})
    result={"schema":"termforge-selection-gate-replay-v1","issue":129,"scope":"Offline selection guard only; no original execution, native grab, pixels or combined-run acceptance",
            "records":rows,"endpoints_and_rois_from_different_runs_combined":False,
            "limitations":["Allowing a historical selection gate does not mean that its raster or ROI pipeline passed.","R126 did not capture selectionCaretP/selectionAnchorP. caretPosition is not substituted and no equal endpoint is fabricated.","Noncollapsed or unavailable endpoints still reject; selectionText type/length is not a selection verdict."]}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"replay_records":len(rows),"gate_allowed":[row["selection_gate_allowed"] for row in rows],"new_original_runs":0}))


if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--r126-result",type=Path,required=True);parser.add_argument("--destination",type=Path,required=True);args=parser.parse_args();make_replay(args.r126_result,args.destination)
