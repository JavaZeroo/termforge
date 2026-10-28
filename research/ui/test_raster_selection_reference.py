"""Offline #129 replay, same-frame controls and no-launch boundaries."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.raster_selection_reference import SOURCE_FILES,phases,run_plan
from research.ui.raster_selection_analysis import GROUPS,analyze_pixels,audit_capture,comparisons,require_collapsed
from research.ui.selection_guard_replay import decision
from research.ui.test_raster_style_reference import fixture as base_fixture,write_records

LABELS=[['D0 H H','M0 H H'],['A0 H','A0S1 H','A0D0 H'],['A1 H H'],['A2 H','A2D0 H','A2S1 H','A2S2 H'],['M1 H','M1S1 H','M1S2 H','M1D0 H']]
CAPTURE_LABELS=['D0/M0 H references','A0 H controls','A1 H H','A2 H controls','M1 H controls']
PALETTE={'default':(0xffc0c0c0,0xff101010),'main':(0xffaa0000,0xff0000aa),'alternate':(0xff00aa00,0xffaa00aa)}


def fixture(root):
    directory,plan_path,data=base_fixture(root)
    plan=json.loads(plan_path.read_text())
    plan.update(issue=129,execution_authorized=True,source_files_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES})
    plan_path.write_text(json.dumps(plan),encoding='utf-8')
    captures=[];records=[]
    for phase,(source_label,content) in enumerate(phases(25,91)):
        capture=copy.deepcopy(data['raster-result.json']['captures'][phase])
        capture.update(label=CAPTURE_LABELS[phase],rois=[])
        capture['image'].update(viewport_width=16,width=16,grab_rect=[0,0,15,1])
        terminal=capture['terminal'];terminal.pop('selection',None)
        lines=list(LABELS[phase]) if phase==0 else [' '.join(LABELS[phase])]
        terminal.update(lines=lines,line_count=len(lines),markers=[],selection_caret=9,selection_anchor=9,selection_text_type='string',selection_text_length=10,caret_rect={'x':16,'y':0,'width':0,'height':2})
        counts={}
        for ordinal,group in enumerate(GROUPS[phase]):
            sample=counts.get(group,0);counts[group]=sample+1
            if phase==0:label=LABELS[phase][0 if group=='default' else 1]
            elif phase==2:label=LABELS[phase][0]
            else:label=LABELS[phase][ordinal]
            offset=3+sample*2 if label.endswith('H H') else len(label)-1
            h=[ordinal*4+2,0,2,2];space=[ordinal*4,0,2,2]
            row=LABELS[phase].index(label) if phase==0 else 0
            column=lines[row].index(label)+offset
            terminal['markers'].append({'ordinal':ordinal,'label':label,'group':group,'sample':sample,'document_line':row,'column':column,'position':column,'character':'H','preceding_character':' ',
                                        'h_rect':dict(zip(('x','y','width','height'),h)),'space_rect':dict(zip(('x','y','width','height'),space))})
            family={'control-default':'default','control-main':'main','control-alternate':'alternate','entry':'default','repeat':'main','return':'main'}.get(group,group)
            fg,bg=PALETTE[family];hp=[fg,bg,fg,bg];sp=[bg]*4
            capture['rois'].append({'ordinal':ordinal,'group':group,'sample':sample,'h_rect':h,'space_rect':space,'h_pixels':hp,'space_pixels':sp,'colors':analyze_pixels(hp,sp)})
        captures.append(capture)
        records.append({'phase':phase,'label':source_label,'source_hex':content.hex(),'source_sha256':hashlib.sha256(content).hexdigest(),'cursor_reply':[1,1]})
    data['raster-result.json']['captures']=captures;data['peer.json']['records']=records
    write_records(directory,data)
    return directory,plan_path,data


class SelectionProposalTests(unittest.TestCase):
    def test_public_proposal_is_not_execution_authorization(self):
        plan=json.loads((ROOT/'research/ui/results/2026-10-28-raster-selection-proposal/plan.json').read_text())
        self.assertFalse(plan['execution_authorized']);self.assertEqual(plan['actual_original_runs'],0)
        self.assertEqual([row['source_hex'] for row in plan['phases']],[source.hex() for _,source in phases(25,91)])
        self.assertEqual([row['source_bytes'] for row in plan['phases']],[43,38,20,53,54])

    def test_actual_records_replay_selection_gate_separately(self):
        replay=json.loads((ROOT/'research/ui/results/2026-10-28-raster-selection-proposal/replay.json').read_text())
        self.assertFalse(replay['endpoints_and_rois_from_different_runs_combined'])
        self.assertEqual([row['selection_gate_allowed'] for row in replay['records']],[True,True,False])
        for row in replay['records']:
            self.assertEqual(decision(row['selection_caret'],row['selection_anchor']),{key:row[key] for key in ('selection_gate_allowed','reason')})
        self.assertEqual(replay['records'][2]['reason'],'selection-endpoints-unavailable')

    def test_unknown_and_noncollapsed_endpoints_reject_without_text_predicate(self):
        self.assertTrue(require_collapsed(97,97))
        for values in ((97,98),(None,None),(True,True),(1.5,1.5),(-1,-1),(2**53,2**53)):
            with self.subTest(values=values),self.assertRaises(ValueError):require_collapsed(*values)

    def test_proposal_cannot_launch_without_separate_authorization(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan=Path(temporary)/'proposal.json';plan.write_text(json.dumps({'issue':129,'maximum_original_runs':1,'execution_authorized':False}))
            with self.assertRaisesRegex(ValueError,'not been authorized'):run_plan(plan)
            self.assertFalse(plan.with_suffix('.attempted').exists())

    @unittest.skipUnless(shutil.which('node'),'Node required for production JS policy/native boundary tests')
    def test_production_js_endpoints_tokens_caret_and_native_lifecycle(self):
        completed=subprocess.run(['node',str(ROOT/'research/ui/test_raster_selection_native.js')],capture_output=True,text=True,timeout=10)
        self.assertEqual(completed.returncode,0,completed.stderr)

    def test_same_frame_matches_default_main_and_ambiguous_remains_null(self):
        with tempfile.TemporaryDirectory() as temporary:
            _,_,data=fixture(Path(temporary));captures=data['raster-result.json']['captures']
            result=comparisons(captures)
            self.assertEqual([row['unique_same_frame_control'] for row in result['frames']],['SGR0','SGR31;44','SGR31;44'])
            self.assertTrue(result['unmatched_does_not_imply_reset'])
            captures[1]['rois'][0]['colors']={'background_rgba':[1,2,3,255],'dominant_ink_rgba':[4,5,6,255]}
            unmatched=comparisons(captures)['frames'][0]
            self.assertEqual(unmatched['same_frame_result'],'unmatched');self.assertIsNone(unmatched['unique_same_frame_control'])
            for capture in captures:
                for roi in capture['rois']:roi['colors']=copy.deepcopy(captures[0]['rois'][0]['colors'])
            result=comparisons(captures)
            self.assertTrue(all(row['unique_same_frame_control'] is None and row['same_frame_result']=='ambiguous' for row in result['frames']))

    def test_native_bool_values_keep_raw_representation_and_reject_coercions(self):
        with tempfile.TemporaryDirectory() as temporary:
            _,_,data=fixture(Path(temporary))
            original=data['raster-result.json']['captures'][0]
            for value in (False,0,True,1):
                with self.subTest(value=value,value_type=type(value).__name__):
                    capture=copy.deepcopy(original);capture['image']['is_null']=value
                    if value:
                        capture.update(calibration=False,reason='empty-image',rois=[])
                    image=audit_capture(capture)['image']
                    self.assertIs(image['is_null'],bool(value))
                    self.assertEqual(image['is_null_raw_value'],value)
                    self.assertEqual(image['is_null_raw_type'],'boolean' if type(value) is bool else 'number')
            for value in (2,-1,'0',0.0,None):
                with self.subTest(invalid_value=value),self.assertRaisesRegex(ValueError,'Image null state'):
                    capture=copy.deepcopy(original);capture['image']['is_null']=value;audit_capture(capture)

    def check_cli(self,change=None,accepted=False):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);directory,plan,data=fixture(root)
                if change:change(data);write_records(directory,data)
                destination=root/'public/validation.json'
                completed=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(ROOT/'research/ui/export_raster_selection_reference.py'),'--directory',str(directory),'--plan',str(plan),'--destination',str(destination)],cwd=ROOT,capture_output=True,text=True,timeout=10)
                if change and not accepted:
                    self.assertNotEqual(completed.returncode,0,completed.stdout);self.assertFalse(destination.exists());self.assertFalse(destination.parent.exists())
                else:
                    self.assertEqual(completed.returncode,0,completed.stderr)
                    summary=json.loads(destination.read_text())
                    self.assertEqual(summary['issue'],129);self.assertTrue(summary['sample_colors_verified'])
                    self.assertEqual(summary['sample_color_comparisons']['frames'][0]['unique_same_frame_control'],'SGR0')
                    self.assertNotIn('h_pixels',destination.read_text());self.assertNotIn('space_pixels',destination.read_text())

    def test_synthetic_expanded_source_exports_scalars_only(self):self.check_cli()
    def test_native_numeric_false_exports_under_normal_and_optimized_python(self):
        self.check_cli(lambda data:[capture['image'].update(is_null=0) for capture in data['raster-result.json']['captures']],accepted=True)
    def test_native_non_boolean_number_rejects_without_public_output(self):
        self.check_cli(lambda data:data['raster-result.json']['captures'][0]['image'].update(is_null=2))
    def test_ambiguous_third_phase_retains_partial_without_later_sources(self):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);directory,plan,data=fixture(root)
                result=data['raster-result.json'];result.update(status='partial-raster-observation',reason='dominant-ink-ambiguous',grab_phases=[0,1,2])
                result['captures']=result['captures'][:3]
                stopped=result['captures'][2];stopped.update(calibration=False,reason='dominant-ink-ambiguous')
                stopped['rois']=stopped['rois'][:1]
                roi=stopped['rois'][0];bg=roi['space_pixels'][0]
                roi.update(h_pixels=[0xff102030,0xff405060,bg,bg],colors=None)
                peer=data['peer.json'];peer.update(complete=False,records=peer['records'][:3],received_hex=(b'\x1b[1;1R'*3).hex())
                for index in (2,3,4):(directory/f'ack-{index}').unlink()
                write_records(directory,data);destination=root/'public/validation.json'
                completed=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(ROOT/'research/ui/export_raster_selection_reference.py'),'--directory',str(directory),'--plan',str(plan),'--destination',str(destination)],cwd=ROOT,capture_output=True,text=True,timeout=10)
                self.assertEqual(completed.returncode,0,completed.stderr)
                summary=json.loads(destination.read_text())
                self.assertEqual(summary['observed_phase_count'],3);self.assertEqual(summary['grab_phases'],[0,1,2])
                self.assertEqual([row['calibration'] for row in summary['phases']],[True,True,False])
                self.assertFalse(summary['sample_colors_verified']);self.assertIsNone(summary['sample_color_comparisons'])
                self.assertIsNone(summary['phases'][2]['rois'][0]['colors'])
                self.assertEqual(summary['reason'],'dominant-ink-ambiguous')
    def test_uncollapsed_claim_cannot_be_promoted(self):self.check_cli(lambda data:data['raster-result.json']['captures'][0]['terminal'].update(selection_anchor=10))
    def test_raw_selection_text_is_rejected(self):self.check_cli(lambda data:data['raster-result.json']['captures'][0]['terminal'].update(selection='private path/text'))


if __name__=='__main__':unittest.main()
