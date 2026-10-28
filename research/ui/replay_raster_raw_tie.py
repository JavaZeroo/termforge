"""Re-analyze fixed #129 private ROI samples without inventing uncaptured cells."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.raster_raw_analysis import classify_pixels,compare_frame,raw_signature,require

RAW_SHA256='3887faaa8d055f42388eafc4b0a9ee2e2b8111d3fa1384e07ec5f0a50585f34d'
EVIDENCE_ID='ev_432abe8baf4fc86efd4994cd12cc3ab5fe0b5120476d22540c5b26e2e3c693a4'


def replay(raw,evidence_path,destination):
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    require(sha(raw)==RAW_SHA256,'Original partial record differs')
    original=json.loads(raw.read_text(encoding='utf-8'))
    evidence=json.loads(evidence_path.read_text(encoding='utf-8'))['data']['evidence']
    require(evidence['evidence_id']==EVIDENCE_ID,'Evidence identity differs')
    finals=[row['payload'] for row in evidence['normalized_result']['messages'] if row['type']=='send' and isinstance(row.get('payload'),dict) and row['payload'].get('kind')=='raster_result']
    require([{key:value for key,value in row.items() if key!='kind'} for row in finals]==[original],'Evidence and fixed raw differ')
    require(original['status']=='partial-raster-observation' and original['reason']=='dominant-ink-ambiguous' and original['grab_phases']==[0,1,2],'Original stop differs')
    stopped=original['captures'][2]
    require(stopped['calibration'] is False and len(stopped['rois'])==1 and len(stopped['terminal']['markers'])==2,'Uncaptured second ROI must stay missing')
    def summarize(roi):
        value={key:roi[key] for key in ('ordinal','group','sample','h_rect','space_rect')}
        value.update(classify_pixels(roi['h_pixels'],roi['space_pixels']))
        value['raw_signature']=raw_signature(value)
        return value
    tied=summarize(stopped['rois'][0])
    require(tied['status']=='dominant-ink-ambiguous' and [row['count'] for row in tied['dominant_ink_candidates']]==[16,16],'Fixed ambiguity changed')
    entry=[summarize(roi) for roi in original['captures'][1]['rois']]
    summary={'schema':'termforge-raster-raw-offline-replay-v1','case_id':'raw-capture-v1','new_original_runs':0,
             'analysis_sha256':sha(ROOT/'research/ui/raster_raw_analysis.py'),'analysis_helpers_sha256':sha(ROOT/'research/ui/raster_selection_analysis.py'),'replay_source_sha256':sha(Path(__file__)),
             'source_evidence_id':EVIDENCE_ID,'source_raw_sha256':RAW_SHA256,'source_evidence_sha256':sha(evidence_path),
             'source_production_commit':'e302fa3acb6a2ed2982aad5550e027d37c772da6','original_status':original['status'],'original_reason':original['reason'],
             'original_grab_phases':[0,1,2],'original_phase2_rois_captured':1,'original_phase2_markers':2,'missing_rois_fabricated':False,'new_capture_inferred':False,
             'phase2_observed_first_roi':tied,'phase1_same_frame_raw_signature_comparison':compare_frame(entry),
             'limitation':'Offline replay only. It cannot recover the uncaptured second H or unsent phases3/4, and does not reclassify the original batch as complete.'}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'replayed':True,'new_original_runs':0,'phase2_candidates':tied['dominant_ink_candidates'],'phase1_unique_control':summary['phase1_same_frame_raw_signature_comparison']['unique_control']}))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--raw',type=Path,required=True);parser.add_argument('--evidence',type=Path,required=True);parser.add_argument('--destination',type=Path,required=True);args=parser.parse_args();replay(args.raw,args.evidence,args.destination)
