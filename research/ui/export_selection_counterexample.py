"""Project only endpoint/length counterexamples from one pinned prior record."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

RAW_SHA256="be349abb34cf28f42b634ccf65bd0749e1e9940856059156b794e7968b7ef83c"
EVIDENCE_ID="ev_8fbd9aead9aa5d60f49e05112c31eb542febf65084643c025eb8a52ffd590048"


def require(condition,message):
    if not condition:raise ValueError(message)


def counterexample_rows(evidence):
    selected={}
    for message in evidence["normalized_result"]["messages"]:
        payload=message.get("payload")
        if message["type"]=="send" and isinstance(payload,dict) and payload.get("kind")=="snapshot" and payload.get("phase") in ("shell-ready","styled-final"):
            require(payload["phase"] not in selected,"Duplicate prior snapshot")
            state=json.loads(payload["text_state"])
            caret,anchor,text=state.get("caret"),state.get("anchor"),state.get("selection")
            require(type(caret) is int and type(anchor) is int and 0<=caret==anchor<=1000000,"Counterexample endpoints are not known collapsed integers")
            require(isinstance(text,str) and 0<len(text)<=32768,"Counterexample text is empty or unavailable")
            selected[payload["phase"]]={"caret":caret,"anchor":anchor,"selection_length":len(text)}
    require(set(selected)=={"shell-ready","styled-final"},"Prior counterexample snapshots missing")
    return [selected[name] for name in ("shell-ready","styled-final")]


def export(evidence_path,destination):
    raw=evidence_path.read_bytes()
    require(hashlib.sha256(raw).hexdigest()==RAW_SHA256,"Prior evidence file identity differs")
    outer=json.loads(raw);evidence=outer["data"]["evidence"]
    require(outer["ok"] and evidence["evidence_id"]==EVIDENCE_ID,"Prior evidence identity differs")
    require(not evidence["normalized_result"]["messages_truncated"],"Prior evidence is truncated")
    rows=counterexample_rows(evidence)
    result={"schema":"termforge-selection-getter-counterexample-v1","source_evidence_id":EVIDENCE_ID,"source_file_sha256":RAW_SHA256,
            "source_scope":"Two historical owned local-shell UI snapshots of the same pinned original; no new original execution",
            "reference_exe_sha256":"7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0",
            "counterexamples":rows,"interpretation":"Nonempty selectionText does not establish a noncollapsed selection: both recorded finite endpoints are equal.",
            "does_not_prove_r126_selection_collapsed":True,"text_or_paths_published":False}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8",newline="\n")
    print(json.dumps({"exported":destination.as_posix(),"counterexamples":len(rows)}))


if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--evidence",type=Path,required=True);parser.add_argument("--destination",type=Path,required=True);args=parser.parse_args();export(args.evidence,args.destination)
