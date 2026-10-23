"""Focused E2 identity tests; synthetic data never claims a real UI run."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
import uuid

from experiments.shell.workflow_sessions_checks import (validate_workflow_sessions, expected_raw, validate_checkpoint_raw,
                                                       validate_cycle_transition, validate_resource_phases, _validate_paste_cases, _cache_bounds)

SOURCE_FILES = (
    'CMakeLists.txt', 'experiments/build_paths.py', 'experiments/shell/shell.cpp',
    'experiments/shell/workspace.hpp', 'experiments/shell/workspace_interactions.hpp',
    'experiments/shell/workspace_workflow_scenario.hpp', 'experiments/shell/workflow_scenario_state.hpp',
    'experiments/shell/workflow_controller.hpp', 'experiments/shell/workflow_fixture.hpp',
    'experiments/shell/runtime_observation.hpp', 'experiments/shell/queues.hpp', 'experiments/shell/resize.hpp',
    'experiments/shell/raw_console_receiver.hpp', 'experiments/shell/text_client.cpp', 'experiments/shell/input_client.cpp',
    'experiments/shell/test_launcher.cpp', 'experiments/shell/paste.hpp', 'experiments/shell/paste_scenario_state.hpp',
    'experiments/shell/find_bar.hpp', 'experiments/shell/run.py', 'experiments/shell/workflow_checks.py',
    'experiments/shell/workflow_sessions_checks.py', 'experiments/shell/test_workflow_sessions.py',
    'experiments/shell/workflow_resources.py', 'experiments/conpty/session.hpp',
    'experiments/presentation/render.hpp', 'experiments/presentation/desktop.hpp',
    'experiments/core/history_view.hpp', 'experiments/core/text_projection.hpp', 'experiments/core/terminal.hpp',
    'experiments/core/screen.hpp', 'experiments/core/unicode_width.hpp',
    'benchmarks/tools/sample.py', 'benchmarks/tools/summarize_resources.py', 'assets/fonts/RobotoMono-Regular.ttf',
)


def public_sample(record, error_kind=None):
    """Only explicit scalar/group fields escape the private execute record."""
    allowed = ('mode','status','timed_out','exit_code','job_after','job_after_cleanup','desktop_gone',
               'result','frame_sha256','sessions_workflow','resources')
    nested = {
        'job_after': ('active','total','terminated'), 'job_after_cleanup': ('active','total','terminated'),
        'result': ('valid','mode','sessions_closed','active_children_after','presented_done'),
        'sessions_workflow': ('schema','live_sessions','checkpoints','rejected_ninth','load_foreground_verified',
                             'data_workflow_verified','complete_workflow_verified','system_clipboard_verified','ime_verified'),
        'resources': ('summary','scope','performance_gate'),
    }
    result = {}
    for key in allowed:
        if key not in record: continue
        value = record[key]
        if key in nested:
            if type(value) is not dict: raise ValueError('public sample group is not an object')
            value = {field:value[field] for field in nested[key] if field in value}
        result[key] = value
    permitted_strings = {'ui-workflow-sessions','passed','failed','termforge-workflow-sessions-identity-validation-v1',
                         'resource-summary.json','launcher-tree','not_evaluated'}
    for key,value in result.items():
        for field,item in (value.items() if type(value) is dict else ((key,value),)):
            if item is None or type(item) in (bool,int): continue
            if type(item) is str and (item in permitted_strings or field=='frame_sha256' and re.fullmatch('[0-9a-f]{64}',item)): continue
            raise ValueError('unsafe value in public sample')
    if error_kind is None and record.get('status') == 'failed': error_kind = 'ProbeFailure'
    if error_kind is not None and (type(error_kind) is not str or re.fullmatch('[A-Za-z][A-Za-z0-9_]{0,63}',error_kind) is None):
        raise ValueError('unsafe public error classification')
    result['error_kind'] = error_kind
    return result


def identity_sample(out):
    slots = []
    for index, name in enumerate(('A', 'B', 'C', 'F1', 'F2', 'F3', 'F4', 'F5'), 1):
        capture = f'fixtures/{name}-{index}/capture.bin'
        path = out / capture; path.parent.mkdir(parents=True); path.write_bytes(b'')
        slots.append(dict(name=name, fixture_role='B' if name == 'C' else name,
                          session_id=index, generation=index, window_token=str(100 + index),
                          window_creation_serial=index, exists=True, visible=name in ('A', 'C'),
                          selected=name in ('A', 'C'), group='main' if name in ('A', 'B', 'F1', 'F2') else 'bottom',
                          capture=capture, fixture_ready=True, fixture_bound=True,
                          process_identity=hashlib.sha256(f'synthetic-process-{index}'.encode()).hexdigest(),
                          raw_bytes=0, raw_sha256=hashlib.sha256(b'').hexdigest(), runtime={'session_id': index}))
    points = [dict(seq=i + 1, phase=phase, elapsed_ms=i * 10, cycle=0,
                   attempted_creations=8+i, window_creations=8,
                   job={'active': 18, 'total': 18, 'terminated': 0}, slots=copy.deepcopy(slots))
              for i, phase in enumerate(('eight-ready', 'reject-ninth'))]
    data = dict(schema='termforge-workflow-sessions-v1', fixture='r108-console-v1', data_only=True,
                system_clipboard=False, ime=False, checkpoints=points)
    record = dict(timed_out=False, exit_code=0, desktop_gone=True, job_after={'active': 0})
    return data, record


class WorkflowSessionsOracleTest(unittest.TestCase):
    def test_observed_cache_budgets_and_paste_errors(self):
        runtime = {'search': {'find_query_bytes': 14, 'find_query_capacity': 31,
                   'result_count': 1, 'result_capacity': 2, 'retained_metadata_rows': 117,
                   'pending_navigation_count': 0, 'actual_work_units': None},
                   'history_capacity_bytes': None, 'paste_busy': True,
                   'paste': {'target': 3, 'state': 'sending', 'total_bytes': 3000000,
                   'source_units': 1000000, 'accepted_bytes': 21023, 'sent_bytes': 20000,
                   'in_flight_bytes': 1023, 'retained_capacity_bytes': 3000015, 'error_present': False}}
        _cache_bounds(runtime)
        for group,field,value in (
            ('search','find_query_capacity',10**9), ('search','find_query_capacity',13),
            ('search','result_capacity',10**9), ('search','result_capacity',0),
            ('search','retained_metadata_rows',301), ('search','pending_navigation_count',2),
            ('paste','retained_capacity_bytes',10**9), ('paste','retained_capacity_bytes',2999999),
            ('paste','error_present',True), ('paste','in_flight_bytes',1022),
            ('paste','in_flight_bytes',1025), ('paste','source_units',None),
            ('paste','sent_bytes',21024)):
            with self.subTest(group=group,field=field,value=value):
                changed=copy.deepcopy(runtime); changed[group][field]=value
                with self.assertRaises(ValueError): _cache_bounds(changed)
        terminal=copy.deepcopy(runtime); terminal['paste_busy']=False
        terminal['last_terminal_paste']=terminal.pop('paste')
        terminal['last_terminal_paste'].update(state='failed',error_present=True,
                                              in_flight_bytes=0,retained_capacity_bytes=15)
        _cache_bounds(terminal)  # A close-induced failure may carry a real error.
        terminal['last_terminal_paste']['state']='cancelled'
        with self.assertRaises(ValueError): _cache_bounds(terminal)

    def test_public_sample_removes_private_paths_and_only_keeps_error_class(self):
        private=dict(mode='ui-workflow-sessions',status='failed',timed_out=False,exit_code=2,
                     desktop_name='private-desktop',stdout='PID=1234',stderr='C:/private/secret',error='C:/private/error',
                     job_after=dict(active=0,total=10,terminated=0,pid=1234),desktop_gone=True,
                     result=dict(valid=False,mode='ui-workflow-sessions',sessions_closed=0,active_children_after=0,
                                 presented_done=None,private_path='C:/private/profile'))
        exported=public_sample(private,'ValueError')
        self.assertEqual(exported['error_kind'],'ValueError')
        self.assertEqual(exported['job_after'],dict(active=0,total=10,terminated=0))
        text=json.dumps(exported)
        self.assertNotIn('private',text);self.assertNotIn('1234',text)
        private['mode']='C:/private/secret'
        with self.assertRaises(ValueError):public_sample(private)

    def test_six_resident_interval_excludes_creation_and_is_one_second(self):
        phases=[('eight-ready',0),('load-started',0),('background-complete',0)]
        for cycle in range(1,6): phases.extend([(p,cycle) for p in ('cycle-six','cycle-six-end','cycle-eight')])
        phases.append(('after-paste-close',5));marks=[];tick=1000
        for i,(phase,cycle) in enumerate(phases,1):
            tick+=1000 if phase=='cycle-six-end' else 100
            six=phase in ('cycle-six','cycle-six-end')
            marks.append(dict(schema='termforge-workflow-resource-phase-v1',seq=i,phase=phase,cycle=cycle,
                              qpc_counter=tick,qpc_frequency=1000,live_sessions=6 if six else 7 if phase=='after-paste-close' else 8,
                              window_creations=8+2*(cycle-1 if six else cycle)))
        validate_resource_phases(marks)
        for key,value in [('qpc_counter',marks[3]['qpc_counter']+999),('live_sessions',8),('window_creations',10),('qpc_frequency',2000)]:
            broken=copy.deepcopy(marks);broken[4][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): validate_resource_phases(broken)

    def test_paste_exact_prefix_rejects_late_eot_wrong_target_and_unreleased_capacity(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary)
            (out/'cancel.bin').write_bytes('汉'.encode()*6000)
            (out/'close.bin').write_bytes('汉'.encode()*256)
            cases=[dict(name=name,target=3,total=3000000,before_sent=16384,before_accepted=18000,
                        accepted=18000,sent=18000,in_flight=0,retained_capacity=0,checkpoint_bytes=768,
                        received=18000 if name=='cancel' else 768,flood_observed=True,state='cancelled') for name in ('cancel','close')]
            data={'paste_cases':cases};_validate_paste_cases(out,data,3)
            for key,value in [('target',4),('in_flight',1),('retained_capacity',3000000),('checkpoint_bytes',0),('sent',17999)]:
                broken=copy.deepcopy(data);broken['paste_cases'][0][key]=value
                with self.subTest(key=key),self.assertRaises(ValueError): _validate_paste_cases(out,broken,3)
            (out/'cancel.bin').write_bytes('汉'.encode()*6000+b'\x04')
            with self.assertRaises(ValueError): _validate_paste_cases(out,data,3)
            (out/'cancel.bin').write_bytes('汉'.encode()*6000)
            (out/'close.bin').write_bytes('汉'.encode()*257)
            with self.assertRaises(ValueError): _validate_paste_cases(out,data,3)

    def test_cycle_replaces_two_generations_and_keeps_six_targets(self):
        previous={name:dict(session_id=i,generation=i,window_creation_serial=i,window_token=str(100+i))
                  for i,name in enumerate(('A','B','C','F1','F2','F3','F4','F5'),1)}
        six={name:copy.deepcopy(row) for name,row in previous.items() if name not in ('F4','F5')}
        eight=copy.deepcopy(six)
        for i,name in enumerate(('F4','F5'),9):
            eight[name]=dict(session_id=i,generation=i,window_creation_serial=i,window_token=str(100+i))
        closed=[dict(previous[name],name=name,window_gone=True) for name in ('F4','F5')]
        validate_cycle_transition(previous,six,eight,closed)
        for defect in ('old_generation','resident_changed','window_still_live'):
            broken=copy.deepcopy(eight);old_closed=copy.deepcopy(closed)
            if defect=='old_generation': broken['F4']['generation']=7
            elif defect=='resident_changed': broken['A']['session_id']=99
            else: old_closed[0]['window_gone']=False
            with self.subTest(defect=defect),self.assertRaises(ValueError):
                validate_cycle_transition(previous,six,broken,old_closed)

    def test_partial_evidence_is_not_default_e2_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary);data,record=identity_sample(out)
            (out/'sessions.json').write_text(json.dumps(data),encoding='utf-8')
            with self.assertRaises(ValueError): validate_workflow_sessions(out,record)
    def test_foreground_input_has_one_literal_and_only_one_target(self):
        literal='TF_R108_FOREGROUND 中文\r'.encode('utf-8')
        self.assertEqual(expected_raw('B','find-complete'), b'')
        self.assertEqual(expected_raw('B','foreground-input'), literal)
        self.assertEqual(expected_raw('B','background-complete'), literal)
        for name in ('A','C','F1','F2','F3','F4','F5'):
            self.assertEqual(expected_raw(name,'foreground-input'), b'')

    def test_raw_checkpoint_rejects_early_duplicate_partial_and_wrong_target(self):
        literal='TF_R108_FOREGROUND 中文\r'.encode('utf-8')
        for name,phase,raw,valid in [('B','foreground-input',literal,True),
                                   ('B','find-complete',literal,False),
                                   ('A','foreground-input',literal,False),
                                   ('B','foreground-input',literal*2,False),
                                   ('B','foreground-input',literal[:-1],False),
                                   ('B','background-complete',b'',False)]:
            slot=dict(name=name,raw_hex=raw.hex(),raw_bytes=len(raw),raw_sha256=hashlib.sha256(raw).hexdigest())
            if valid: validate_checkpoint_raw(slot,phase)
            else:
                with self.subTest(name=name,phase=phase,raw=raw),self.assertRaises(ValueError):
                    validate_checkpoint_raw(slot,phase)

    def test_rejects_capacity_identity_reuse_and_fabricated_empty_input(self):
        changes = {
            'no_ninth_attempt': lambda p: p.update(attempted_creations=8),
            'ninth_window_created': lambda p: p.update(window_creations=9),
            'ninth_process_lived_briefly': lambda p: p['job'].update(total=19, terminated=1),
            'duplicate_hwnd': lambda p: p['slots'][1].update(window_token=p['slots'][0]['window_token']),
            'duplicate_process': lambda p: p['slots'][1].update(process_identity=p['slots'][0]['process_identity']),
            'changed_session': lambda p: p['slots'][0].update(session_id=99),
            'wrong_runtime': lambda p: p['slots'][0]['runtime'].update(session_id=2),
            'unbound_fixture': lambda p: p['slots'][0].update(fixture_bound=False),
            'wrong_target_path': lambda p: p['slots'][0].update(capture='../capture.bin'),
            'hidden_selected': lambda p: p['slots'][0].update(visible=False),
            'bool_sequence': lambda p: p.update(seq=True),
            'old_phase': lambda p: p.update(phase='eight-ready'),
            'backward_time': lambda p: p.update(elapsed_ms=-1),
        }
        for defect, change in changes.items():
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temporary:
                out=Path(temporary); data,record=identity_sample(out)
                change(data['checkpoints'][1])
                (out/'sessions.json').write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_workflow_sessions(out,record,allow_partial=True)
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary);data,record=identity_sample(out)
            (out/'fixtures/A-1/capture.bin').write_bytes(b'x')
            (out/'sessions.json').write_text(json.dumps(data), encoding='utf-8')
            with self.assertRaises(ValueError):
                validate_workflow_sessions(out,record,allow_partial=True)

    def test_rejects_missing_stage_and_unrecovered_outer_resources(self):
        for defect in ('missing', 'job', 'desktop', 'timeout', 'exit'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temporary:
                out=Path(temporary); data,record=identity_sample(out)
                if defect=='missing': data['checkpoints'].pop()
                elif defect=='job': record['job_after']['active']=1
                elif defect=='desktop': record['desktop_gone']=False
                elif defect=='timeout': record['timed_out']=True
                else: record['exit_code']=2
                (out/'sessions.json').write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_workflow_sessions(out,record,allow_partial=True)

    def test_eight_identities_and_actual_ninth_attempt(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary); data,record=identity_sample(out)
            (out/'sessions.json').write_text(json.dumps(data), encoding='utf-8')
            result=validate_workflow_sessions(out,record,allow_partial=True)
            self.assertEqual(result['live_sessions'],8)
            self.assertFalse(result['complete_workflow_verified'])


@unittest.skipUnless(os.name == 'nt', 'eight-session ConPTY workspace requires Windows')
class WorkflowSessionsTests(unittest.TestCase):
    def test_two_fresh_owned_eight_session_rounds(self):
        from experiments.build_paths import ROOT, binary
        from experiments.shell.run import execute
        from experiments.shell.workflow_resources import summarize_resource_observations
        executable = binary('termforge_shell')
        binaries = {name: hashlib.sha256(binary(name).read_bytes()).hexdigest() for name in
                    ('termforge_shell', 'shell_test_launcher', 'shell_text_client', 'shell_input_client')}
        sources = {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES}
        parent = Path(os.environ.get('TERMFORGE_TEST_ARTIFACTS', ROOT/'build/test-artifacts'))/'workflow-sessions'
        parent.mkdir(parents=True, exist_ok=True)
        for round_number in range(2):
            out = parent / ('run-'+uuid.uuid4().hex)
            validation = dict(schema='termforge-workflow-sessions-driver-v1', round=round_number,
                              binary_sha256=binaries, data_only=True, system_clipboard_verified=False,
                              sources=sources, source_to_binary_binding='not_independently_verified',
                              source_hash_policy='sha256_raw_worktree_bytes_git_checkout_may_differ',
                              ime_verified=False, status='failed', error_kind=None)
            try:
                record = execute(executable, 'ui-workflow-sessions', out, timeout=90, resources=True)
                checked = validate_workflow_sessions(out, record)
                self.assertTrue(checked['data_workflow_verified'])
                self.assertFalse(checked['complete_workflow_verified'])
                resources = json.loads((out/'resource-summary.json').read_bytes())
                derived = summarize_resource_observations(out/'resource-observations.jsonl')
                self.assertEqual(resources['observations'], {'file':'resource-observations.jsonl','summary':derived})
                self.assertEqual(resources['status'], 'recorded')
                self.assertEqual(resources['scope'], 'launcher-tree')
                self.assertEqual(resources['performance_gate'], 'not_evaluated')
                self.assertTrue(resources['root_identity_verified'])
                self.assertTrue(resources['sampler_joined'])
                self.assertIsNone(resources['phase_windows'])
                validation.update(status='passed', workflow=checked, resources=derived)
            except BaseException as error:
                validation['error_kind'] = type(error).__name__
                raise
            finally:
                # execute owns directory creation and persists its original failure/cleanup record.
                if out.is_dir():
                    try:
                        private_sample = json.loads((out/'sample.json').read_bytes())
                        exported = public_sample(private_sample, validation['error_kind'])
                        (out/'sample.public.json').write_text(json.dumps(exported,indent=2)+'\n',encoding='utf-8')
                        self.assertEqual(json.loads((out/'sample.public.json').read_bytes()),
                                         public_sample(private_sample, validation['error_kind']))
                    except BaseException as error:
                        validation.update(status='failed', export_error_kind=type(error).__name__)
                        if validation['error_kind'] is None:
                            (out/'validation.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8')
                            raise
                    (out/'validation.json').write_text(json.dumps(validation, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    unittest.main()
