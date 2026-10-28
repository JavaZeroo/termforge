"""Trace-data/CLI contract: known Alacritty differences cannot mask later drift."""
import copy
import json
import unittest

from cases import Case
import compare_candidate as comparison

CASE=Case('alternate_screen','A\r\nB\x1b[?1049hALT\x1b[?1049lZ')
FINAL='624c2bd5d31b73bfba47dbbbb72260a54d9a9af663e65d5ad2a34d6f4559e067'
BYTE={
    'input_sha256':'6ae16cedd522763cbbefccf3424d3195350d2d149d06a1e815e52ab73f96fe76',
    'rows':3,'columns':12,'history':3,
    'first_difference':{'offset':12,'path':'.screen.row','expected':0,'actual':1},
    'final_core_sha256':FINAL,'final_candidate_sha256':FINAL,
    'canonical_core_trace_sha256':'8229315bee9585fa620254e0e294e51ac0bd81511abd114a225774a5ca19686b',
    'canonical_candidate_trace_sha256':'f01576eb7ca499c8d3d72789b0cdf2411ba9479134cd6d85666f35e50c8c38b0',
    'core_checkpoints':25,'candidate_checkpoints':25}


def literal_trace(candidate=False):
    """Independent literal cursor/text states for the unchanged 24-byte input."""
    def screen(row,column,lines):
        style={'fg':-1,'bg':-1,'bold':False}
        cells=[[{'text':char if char!=' ' else '', 'width':1 if char!=' ' else 0,'style':dict(style)}
                for char in line.ljust(12)] for line in lines]
        return {'row':row,'column':column,'wrap_pending':False,'cells':cells,'history':[],'current_style':dict(style)}
    states=[(0,0,['','','']),(0,1,['A','','']),(0,0,['A','','']),(1,0,['A','',''])]
    states += [(1,1,['A','B',''])]*8
    states += ([(1,1,['','','']),(1,2,['',' A','']),(1,3,['',' AL','']),(1,4,['',' ALT',''])] if candidate else
               [(0,0,['','','']),(0,1,['A','','']),(0,2,['AL','','']),(0,3,['ALT','',''])])
    states += [states[-1]]*7
    states += [(1,1,['A','B','']),(1,2,['A','BZ',''])]
    return [{'offset':offset,'screen':screen(*state)} for offset,state in enumerate(states)]


class CandidateReferenceContract(unittest.TestCase):
    def test_legacy_first_and_final_only_entry_must_not_authorize_a_trace(self):
        core,candidate=literal_trace(),literal_trace(True)
        difference=comparison.compare_traces(core,candidate)
        legacy={key:value for key,value in BYTE.items() if not key.startswith('canonical_') and not key.endswith('checkpoints')}
        known={'alternate_screen':{'plans':{'byte':legacy}}}
        self.assertEqual(comparison.reference_status(CASE,'byte',difference,core,candidate,known),'difference')

    def test_complete_literal_signature_matches_and_later_checkpoint_drift_rejects(self):
        core,candidate=literal_trace(),literal_trace(True)
        self.assertEqual(len(core),25)
        self.assertEqual(comparison.state_sha(core),BYTE['canonical_core_trace_sha256'])
        self.assertEqual(comparison.state_sha(candidate),BYTE['canonical_candidate_trace_sha256'])
        difference=comparison.compare_traces(core,candidate)
        self.assertEqual(difference,{'checkpoint':12,**BYTE['first_difference']})
        known={'alternate_screen':{'plans':{'byte':BYTE}}}
        self.assertEqual(comparison.reference_status(CASE,'byte',difference,core,candidate,known),'reference_difference')
        for target in ('core','candidate'):
            left,right=copy.deepcopy(core),copy.deepcopy(candidate)
            (left if target=='core' else right)[13]['screen']['current_style']['fg']=1
            self.assertEqual(comparison.compare_traces(left,right),difference)
            self.assertEqual(left[-1],core[-1]);self.assertEqual(right[-1],candidate[-1])
            self.assertEqual(comparison.reference_status(CASE,'byte',difference,left,right,known),'difference')

    def test_missing_signature_fields_truncation_and_unlisted_plan_stay_differences(self):
        core,candidate=literal_trace(),literal_trace(True)
        difference=comparison.compare_traces(core,candidate)
        for field in BYTE:
            expected=dict(BYTE);expected.pop(field)
            self.assertEqual(comparison.reference_status(CASE,'byte',difference,core,candidate,{'alternate_screen':{'plans':{'byte':expected}}}),'difference',field)
        known={'alternate_screen':{'plans':{'byte':BYTE}}}
        self.assertEqual(comparison.reference_status(CASE,'seed:99',difference,core,candidate,known),'difference')
        shortened=copy.deepcopy(candidate);shortened.pop(14)
        self.assertEqual(comparison.reference_status(CASE,'byte',difference,core,shortened,known),'difference')

    def test_missing_projection_fields_cannot_become_a_match(self):
        for field in ('offset','row','current_style','cell_style'):
            records=literal_trace()
            if field=='offset':records[14].pop('offset')
            elif field=='cell_style':records[14]['screen']['cells'][0][0].pop('style')
            else:records[14]['screen'].pop(field)
            with self.subTest(field=field),self.assertRaises(ValueError):comparison.common_projection(records)

    def test_complete_checkpoint_schedule_is_required_including_initial_and_resize(self):
        core=literal_trace()
        comparison.validate_checkpoints(core,[1]*24,())
        for malformed in (core[1:],core[:14]+core[15:],core[:-1],core+[copy.deepcopy(core[-1])]):
            with self.assertRaises(ValueError):comparison.validate_checkpoints(malformed,[1]*24,())
        comparison.validate_checkpoints(core+[copy.deepcopy(core[-1])],[1]*24,('--resize=2x6',))
        with self.assertRaises(ValueError):comparison.validate_checkpoints(core,[1]*24,(),input_length=25)

    def test_registry_is_closed_to_the_reviewed_four_plans(self):
        path=comparison.Path(comparison.__file__).with_name('candidate-known-differences.json')
        known=json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(set(known['alternate_screen']['plans']),{'byte','seed:42','seed:20261022','seed:8675309'})
        self.assertEqual(known['alternate_screen']['plans']['byte'],BYTE)
        self.assertFalse(known['alternate_screen']['semantic_acceptance'])


if __name__=='__main__':unittest.main()
