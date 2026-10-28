"""Public capture/export seams with synthetic pixels; no original launch."""
from __future__ import annotations
import hashlib
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
from research.ui.raster_raw_analysis import classify_pixels
from research.ui.test_raster_selection_reference import fixture as selection_fixture


def fixture(root):
    from research.ui.raster_raw_reference import SOURCE_FILES
    directory,planpath,data=selection_fixture(root)
    result=data.pop('raster-result.json')
    result.update(status='captured-five-raw-raster-phases',reason=None)
    for capture in result['captures']:
        capture['capture_valid']=capture.pop('calibration')
        for roi in capture['rois']:roi.pop('colors')
    # All phase2 H pixels have a genuine tie; collection is complete regardless.
    for roi in result['captures'][2]['rois']:
        roi['h_pixels']=[0xff112233,0xff445566,0xff112233,0xff445566]
    data['raster-raw-result.json']=result
    messages=[{'type':'send','payload':{'kind':'raw_raster_phase',**copy.deepcopy(capture)}} for capture in result['captures']]
    messages.append({'type':'send','payload':{'kind':'raw_raster_result',**copy.deepcopy(result)}})
    data['evidence.json']['data']['evidence']['normalized_result']['messages']=messages
    for name,value in data.items():(directory/name).write_text(json.dumps(value))
    for index in range(5):(directory/f'ack-{index}').write_text('captured')
    plan=json.loads(planpath.read_text());plan.update(case_id='raw-capture-v1',source_files_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES})
    planpath.write_text(json.dumps(plan))
    return directory,planpath,data


def write_records(directory,data):
    result=data['raster-raw-result.json']
    messages=[{'type':'send','payload':{'kind':'raw_raster_phase',**copy.deepcopy(capture)}} for capture in result['captures']]
    messages.append({'type':'send','payload':{'kind':'raw_raster_result',**copy.deepcopy(result)}})
    data['evidence.json']['data']['evidence']['normalized_result']['messages']=messages
    for name,value in data.items():(directory/name).write_text(json.dumps(value))


class RawRasterTests(unittest.TestCase):
    def test_fixed_proposal_is_non_executable_and_preserves_exact_five_sources(self):
        plan=json.loads((ROOT/'research/ui/results/2026-10-29-raster-raw-proposal/plan.json').read_text())
        self.assertFalse(plan['execution_authorized']);self.assertEqual(plan['actual_original_runs'],0)
        self.assertEqual(plan['case_id'],'raw-capture-v1')
        self.assertEqual([row['source_bytes'] for row in plan['phases']],[43,38,20,53,54])
        self.assertEqual(plan['maximum_grabs_per_phase'],1);self.assertEqual(plan['maximum_grabs'],5)
        from research.ui.raster_raw_reference import SOURCE_FILES,phases
        self.assertEqual([row['source_hex'] for row in plan['phases']],[source.hex() for _,source in phases(25,91)])
        self.assertEqual(plan['source_files_sha256'],{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES})

    @unittest.skipUnless(shutil.which('node'),'Node required for the external Qt boundary')
    def test_all_rois_survive_a_tied_color_histogram(self):
        result=subprocess.run(['node',str(ROOT/'research/ui/test_raster_raw_native.js')],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_tied_ink_is_unknown_with_candidates_and_complete_signatures(self):
        # Public synthetic arrangement reproduces the observed 16/16 count, not original pixels.
        h=[0xff112233]*16+[0xff445566]*16+[0xff101010]*112
        space=[0xff101010]*144
        result=classify_pixels(h,space)
        self.assertEqual(result['status'],'dominant-ink-ambiguous')
        self.assertIsNone(result['dominant_ink_rgba'])
        self.assertEqual(result['dominant_ink_candidates'],[{'rgba':[17,34,51,255],'count':16},{'rgba':[68,85,102,255],'count':16}])
        self.assertTrue(result['ink_observed'])
        self.assertEqual(result['h']['histogram'],[{'rgba':[16,16,16,255],'count':112},{'rgba':[17,34,51,255],'count':16},{'rgba':[68,85,102,255],'count':16}])
        self.assertEqual(result['h']['samples'],144)
        self.assertEqual(result['h']['sha256'],hashlib.sha256(json.dumps(h,separators=(',',':')).encode()).hexdigest())
        self.assertNotIn('pixels',json.dumps(result))

    def test_same_frame_matching_uses_ordered_raw_signature_and_preserves_aliases(self):
        from research.ui.raster_raw_analysis import compare_frame
        def roi(group,h):
            value=classify_pixels(h,[0xff101010]*4)
            return {'group':group,'h_rect':[0,0,2,2],'space_rect':[0,0,2,2],**value}
        target=roi('repeat',[0xff112233,0xff445566,0xff112233,0xff445566])
        controls=[roi('control-default',[0xff445566,0xff112233,0xff445566,0xff112233]),roi('control-main',[0xff112233,0xff445566,0xff112233,0xff445566])]
        result=compare_frame([target,*controls])
        self.assertEqual(result['matching_controls'],['SGR31;44'])
        self.assertEqual(result['unique_control'],'SGR31;44')
        self.assertIsNone(target['dominant_ink_rgba'])
        controls.append(roi('control-alternate',[0xff112233,0xff445566,0xff112233,0xff445566]))
        result=compare_frame([target,*controls])
        self.assertEqual(result['matching_controls'],['SGR31;44','SGR32;45'])
        self.assertIsNone(result['unique_control'])
        self.assertEqual(result['result'],'ambiguous')

    def test_complete_capture_exports_all_five_phases_despite_tied_ink(self):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);directory,plan,_=fixture(root);destination=root/'public/result.json'
                result=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(ROOT/'research/ui/export_raster_raw_reference.py'),'--directory',str(directory),'--plan',str(plan),'--destination',str(destination)],capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,0,result.stderr)
                public=json.loads(destination.read_text())
                self.assertTrue(public['raw_capture_complete']);self.assertEqual(public['observed_phase_count'],5)
                self.assertEqual(len(public['phases'][2]['rois']),2)
                self.assertIsNone(public['phases'][2]['rois'][0]['dominant_ink_rgba'])
                self.assertEqual(public['phases'][2]['rois'][0]['status'],'dominant-ink-ambiguous')
                self.assertFalse(public['internal_current_style_verified'])
                self.assertNotIn('h_pixels',destination.read_text());self.assertNotIn('space_pixels',destination.read_text())

    def reject_cli(self,change):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);directory,plan,data=fixture(root);change(data);write_records(directory,data);destination=root/'public/result.json'
                result=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(ROOT/'research/ui/export_raster_raw_reference.py'),'--directory',str(directory),'--plan',str(plan),'--destination',str(destination)],capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0,result.stdout)
                self.assertFalse(destination.exists());self.assertFalse(destination.parent.exists())

    def test_duplicate_roi_identity_is_rejected_without_public_output(self):
        self.reject_cli(lambda data:data['raster-raw-result.json']['captures'][0]['rois'].__setitem__(1,copy.deepcopy(data['raster-raw-result.json']['captures'][0]['rois'][0])))

    def test_wrong_marker_label_is_rejected_without_public_output(self):
        self.reject_cli(lambda data:data['raster-raw-result.json']['captures'][1]['terminal']['markers'][0].update(label='A0S1 H'))

    def test_missing_ink_or_nonuniform_space_stays_unknown_with_raw_signatures(self):
        from research.ui.raster_raw_analysis import compare_frame
        for h,space,status in (([1,1],[1,1],'no-marker-ink'),([2,2],[1,3],'space-background-ambiguous')):
            with self.subTest(status=status):
                value=classify_pixels(h,space)
                self.assertEqual(value['status'],status);self.assertIsNone(value['dominant_ink_rgba'])
                self.assertEqual(value['h']['samples'],2);self.assertEqual(value['space']['samples'],2)
                target={**value,'group':'entry','h_rect':[0,0,1,2],'space_rect':[0,0,1,2]}
                control={**target,'group':'control-default'}
                result=compare_frame([target,control])
                self.assertEqual(result['result'],'unknown');self.assertIsNone(result['unique_control'])

    def test_unauthorized_raw_proposal_cannot_launch_or_create_attempt(self):
        from research.ui.raster_raw_reference import run_plan
        with tempfile.TemporaryDirectory() as temporary:
            plan=Path(temporary)/'proposal.json'
            plan.write_text(json.dumps({'issue':129,'case_id':'raw-capture-v1','maximum_original_runs':1,'execution_authorized':False}))
            with self.assertRaisesRegex(ValueError,'not been authorized'):run_plan(plan)
            self.assertFalse(plan.with_suffix('.attempted').exists())

    def test_structural_identity_and_privacy_failures_create_no_public_output(self):
        changes=[
            lambda data:data['raster-raw-result.json']['captures'][0]['terminal'].update(selection_anchor=10),
            lambda data:data['raster-raw-result.json']['captures'][0]['terminal'].update(selection='PRIVATE/host/path'),
            lambda data:data['raster-raw-result.json']['captures'][0]['terminal']['lines'].__setitem__(0,'PRIVATE_REVIEW_MARKER'),
            lambda data:data['raster-raw-result.json']['captures'][0]['image'].update(image_dpr=2),
            lambda data:data['raster-raw-result.json']['captures'][0]['image'].update(is_null='0'),
            lambda data:data['peer.json']['naws_history'].append([92,25]),
            lambda data:data['raster-raw-result.json']['grab_phases'].append(0)]
        for index,change in enumerate(changes):
            with self.subTest(case=index):self.reject_cli(change)

    def test_real_partial_replay_does_not_fabricate_missing_roi_or_later_phases(self):
        replay=json.loads((ROOT/'research/ui/results/2026-10-29-raster-raw-proposal/replay.json').read_text())
        self.assertEqual(replay['new_original_runs'],0);self.assertFalse(replay['new_capture_inferred'])
        self.assertEqual(replay['original_phase2_rois_captured'],1);self.assertEqual(replay['original_phase2_markers'],2)
        self.assertFalse(replay['missing_rois_fabricated']);self.assertEqual(replay['original_grab_phases'],[0,1,2])
        self.assertEqual([row['count'] for row in replay['phase2_observed_first_roi']['dominant_ink_candidates']],[16,16])
        self.assertIsNone(replay['phase2_observed_first_roi']['dominant_ink_rgba'])


if __name__=='__main__':unittest.main()
