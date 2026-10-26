"""E1 data-only literal evidence tests; no windows, clipboard or IME input."""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.shell.workflow_checks import (validate_fixture_case, validate_rejected_order, validate_fixture_host,
                                               validate_console_window, controller_observations, validate_controller_exit)

PREFIX = 'TF_R108_PASTE 中文\rNEXT\r'


def synthetic_case(directory, role='A', generation=1):
    """Independent schema sample, never calls the production fixture generator."""
    source = bytearray(); stages = []; events = []
    def event(name, phase=None):
        events.append(dict(schema='termforge-workflow-progress-v1', seq=len(events)+1,
                           elapsed_ms=len(events)*10, event=name, phase=phase,
                           source_bytes=len(source), records=sum(s['records'] for s in stages),
                           source_sha256=hashlib.sha256(source).hexdigest()))
    event('ready')
    if role == 'A':
        for name, first, count in [('output', 0, 160), ('append', 160, 20), ('evict', 180, 500)]:
            event('event-start', name); begin=len(source); began=events[-1]['elapsed_ms']
            payload=''.join(f'TF_R108_A_{n:04d} 中文 e\u0301\r\n' for n in range(first,first+count)).encode()
            source.extend(payload)
            stages.append(dict(name=name,first=first,requested_records=count,records=count,bytes=len(payload),
                               source_begin=begin,source_end=len(source),source_sha256=hashlib.sha256(payload).hexdigest(),
                               begin_ms=began,end_ms=len(events)*10,complete=True))
            event('output-complete', name)
    event('stop'); event('stopped')
    received=b'' if role=='A' else (PREFIX+'你好').encode()
    units=len(received.decode().encode('utf-16-le'))//2
    manifest=dict(schema='termforge-workflow-fixture-v1',fixture='r108-console-v1',role=role,generation=generation,
                  status='stopped',source_bytes=len(source),source_sha256=hashlib.sha256(source).hexdigest(),
                  records=sum(s['records'] for s in stages),stages=stages,
                  receiver=dict(units=units,bytes=len(received),sha256=hashlib.sha256(received).hexdigest(),joined=True,error=0),worker_joined=True)
    sidecar=dict(schema='termforge-raw-console-receiver-v1',state='stopped',stop_requested=True,worker_joined=True,
                 last_read_success=True,last_read_count=0,last_read_error=0,worker_error=0,failure_step='',
                 zero_read_accepted=True,reads_entered=2 if received else 1,reads_completed=2 if received else 1,
                 cancel_calls=1,cancel_successes=1,last_cancel_error=0,received_units=units,saved_bytes=len(received),
                 unexpected_cancel_observed=False,unexpected_cancel_error=0,last_wait_result=0,last_wait_error=0)
    (directory/'capture.bin').write_bytes(received)
    (directory/'capture.bin.source.bin').write_bytes(source)
    (directory/'capture.bin.workflow.json').write_text(json.dumps(manifest),encoding='utf-8')
    (directory/'capture.bin.receiver.json').write_text(json.dumps(sidecar),encoding='utf-8')
    (directory/'capture.bin.progress.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in events),encoding='utf-8')
    return manifest,events


def synthetic_console(directory, role='F1', generation=1, columns=80, lines=None):
    if lines is None:
        lines = [f'TF_R108_{role}_31999 中文 e\u0301' + '.' * 35,
                 f'TF_R108_{role}_OUTPUT_DONE 0500', '']
    payload = ''.join(lines).encode('utf-16-le')
    cell_payload = bytearray()
    for line in lines:
        cells = []
        for char in line:
            cells.append((ord(char), 0x107 if char in '中文' else 0x7))
            if char in '中文':
                cells.append((ord(char), 0x207))
        cells.extend([(0x20, 0x7)] * (columns - len(cells)))
        for codepoint, attributes in cells:
            cell_payload.extend(struct.pack('<HH', codepoint, attributes))
    counts = [len(line.encode('utf-16-le')) // 2 for line in lines]
    metadata = dict(schema='termforge-console-window-v1', role=role, generation=generation,
                    columns=columns, rows=len(lines), top=0, cursor_column=0, cursor_row=len(lines)-1,
                    utf16_units=sum(counts), row_units=counts, text_sha256=hashlib.sha256(payload).hexdigest(),
                    cell_count=columns*len(lines), cell_stride=4, cell_sha256=hashlib.sha256(cell_payload).hexdigest())
    (directory/'capture.bin.console.bin').write_bytes(payload)
    (directory/'capture.bin.cells.bin').write_bytes(cell_payload)
    (directory/'capture.bin.console.json').write_text(json.dumps(metadata),encoding='utf-8')
    return metadata


def captured_rejected_order(directory):
    """The 0e1fb585 failure record, with no process identities or private paths."""
    fixture = Path(__file__).with_name('fixtures') / 'rejected-order-before-read.json'
    record = json.loads(fixture.read_text(encoding='utf-8'))
    for suffix in ('', '.source.bin'):
        (directory / ('capture.bin' + suffix)).write_bytes(b'')
    for key, suffix in [('manifest', '.workflow.json'), ('receiver', '.receiver.json')]:
        (directory / ('capture.bin' + suffix)).write_text(json.dumps(record[key]), encoding='utf-8')
    (directory / 'capture.bin.progress.jsonl').write_text(
        ''.join(json.dumps(event) + '\n' for event in record['events']), encoding='utf-8')
    return record


class WorkflowOracleTest(unittest.TestCase):
    def test_failed_controller_observation_retains_unknown_without_private_fields(self):
        # Synthetic failed-row counterexample, not recovered historical data:
        # a five-second wait times out and close produces STATUS_CONTROL_C_EXIT.
        # This tests the sanitizer without claiming to reproduce the cause.
        row = dict(generation=21, observed_generation=21, child_exit_code=0,
                   cmd_exit_code=0xC000013A, exit_input_bytes=5, cmd_exit_wait_ms=5000,
                   transport_bytes_before_exit_input=100, transport_bytes_before_close=140,
                   prompt_count_before_exit_input=1, prompt_count_before_close=1,
                   inner_job_active_after=0, launched_through_existing_cmd=True,
                   identity_verified=True, child_joined=True, phases_complete=False,
                   parent_order_rejected=False, target_job_cancelled=False,
                   abnormal_completion_rejected=False, stopped_event=True, child_finished=True,
                   cmd_exited_before_exit_input=False, cmd_wait_exit_result=False,
                   cmd_native_wait_result_before_exit_input=258, cmd_native_wait_error_before_exit_input=0,
                   cmd_native_wait_result=258, cmd_native_wait_error=0,
                   forced_process_cancel=False, forced_reader_cancel=False,
                   cleanup_complete=True, passed=False, cmd_exit_code_before_exit_input=None,
                   preclose_cmd_exit_code=None, identity_pid=1234, process_creation_time=999,
                   raw_transport='PRIVATE')
        host = dict(schema='termforge-workflow-controller-test-v1', passed=False,
                    cases=[dict(row, name=name, role=role) for name, role in
                           [('B graceful', 'B'), ('A phases', 'A'), ('B closed', 'B')]])
        observed = controller_observations(host)
        self.assertFalse(observed[0]['cmd_wait_exit_result'])
        self.assertIsNone(observed[0]['preclose_cmd_exit_code'])
        self.assertFalse(observed[0]['passed'])
        for value in observed:
            self.assertTrue({'identity_pid', 'process_creation_time', 'raw_transport'}.isdisjoint(value))
        with self.assertRaises(ValueError):
            validate_controller_exit(observed[0], closed=False)

    def test_controller_preclose_exit_cannot_be_replaced_by_cleanup(self):
        row = dict(cmd_wait_exit_result=True, cmd_native_wait_result=0, cmd_native_wait_error=0,
                   preclose_cmd_exit_code=0, exit_input_bytes=5,
                   forced_process_cancel=False, forced_reader_cancel=False, cmd_exit_code=0)
        validate_controller_exit(row, closed=False)
        closed = dict(row, preclose_cmd_exit_code=91, exit_input_bytes=0, cmd_exit_code=91)
        validate_controller_exit(closed, closed=True)
        # A plausible post-close code and successful cleanup cannot fill an
        # unobserved pre-close result; preserve the original timed-out verdict.
        for patch in ({'cmd_wait_exit_result': False, 'preclose_cmd_exit_code': None},
                      {'preclose_cmd_exit_code': 0xC000013A}, {'preclose_cmd_exit_code': False},
                      {'cmd_native_wait_result': 258}, {'cmd_native_wait_result': 0xFFFFFFFF, 'cmd_native_wait_error': 6},
                      {'cmd_native_wait_result': False}, {'cmd_native_wait_error': False},
                      {'exit_input_bytes': 4}, {'exit_input_bytes': True},
                      {'forced_process_cancel': True}, {'forced_reader_cancel': True}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                validate_controller_exit(dict(row, **patch), closed=False)

    def test_captured_rejected_order_can_stop_before_first_read(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            captured_rejected_order(directory)
            result = validate_rejected_order(directory, expected_generation=5)
            self.assertTrue(result['expected_rejection'])
            self.assertFalse(result['normal_completion'])
            self.assertEqual(result['reason'], 'workflow command order invalid')

    def test_before_first_read_rejection_requires_complete_clean_stop(self):
        defects = [('reads_entered', 1), ('reads_entered', False), ('reads_completed', 1),
                   ('worker_joined', False), ('stop_requested', False), ('worker_error', 5),
                   ('failure_step', 'ReadConsoleW'), ('state', 'failed'),
                   ('received_units', 1), ('saved_bytes', 1),
                   ('cancel_calls', 0), ('cancel_successes', 1), ('last_cancel_error', 0),
                   ('last_cancel_error', 5), ('unexpected_cancel_observed', True),
                   ('unexpected_cancel_error', 5), ('last_wait_result', 258), ('last_wait_error', 5),
                   ('last_read_success', True), ('last_read_count', 1), ('last_read_error', 995),
                   ('zero_read_accepted', True)]
        for field, value in defects:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                record = captured_rejected_order(directory)
                record['receiver'][field] = value
                (directory / 'capture.bin.receiver.json').write_text(json.dumps(record['receiver']), encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_rejected_order(directory, expected_generation=5)
        for suffix in ('', '.source.bin'):
            with self.subTest(nonempty=suffix), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                captured_rejected_order(directory)
                (directory / ('capture.bin' + suffix)).write_bytes(b'x')
                with self.assertRaises(ValueError):
                    validate_rejected_order(directory, expected_generation=5)

    def test_before_first_read_is_not_accepted_for_normal_fixture_cases(self):
        for role in ('A', 'B'):
            with self.subTest(role=role), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                captured = captured_rejected_order(directory)
                synthetic_case(directory, role)
                (directory / 'capture.bin.receiver.json').write_text(json.dumps(captured['receiver']), encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory, expected_role=role, expected_generation=1,
                                          expected_input=b'' if role == 'A' else (PREFIX + '你好').encode())

    def test_console_window_requires_bounded_identity_and_complete_unicode(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            metadata = synthetic_console(directory, generation=7)
            actual = validate_console_window(directory, expected_role='F1', expected_generation=7)
            self.assertEqual(actual['lines'][0], 'TF_R108_F1_31999 中文 e\u0301' + '.' * 35)
            self.assertEqual(actual['lines'][-1], '')
            self.assertEqual(actual['cell_lines'], actual['lines'])
            for field, value in [('role', 'F2'), ('generation', 8), ('columns', 0),
                                 ('utf16_units', metadata['utf16_units'] + 1),
                                 ('text_sha256', '0' * 64),
                                 ('cell_count', metadata['cell_count'] + 1)]:
                with self.subTest(corruption=field):
                    changed = dict(metadata); changed[field] = value
                    (directory/'capture.bin.console.json').write_text(json.dumps(changed),encoding='utf-8')
                    with self.assertRaises(ValueError):
                        validate_console_window(directory, expected_role='F1', expected_generation=7)
            changed = dict(metadata); changed['row_units'] = changed['row_units'][:-1]
            (directory/'capture.bin.console.json').write_text(json.dumps(changed),encoding='utf-8')
            with self.assertRaises(ValueError):
                validate_console_window(directory, expected_role='F1', expected_generation=7)
            (directory/'capture.bin.console.bin').unlink()
            with self.assertRaises(ValueError):
                validate_console_window(directory, expected_role='F1', expected_generation=7)

    def test_rejection_keeps_real_reason_and_cannot_be_normal_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);manifest,events=synthetic_case(directory,'B',5)
            manifest.update(role='F2',status='failed',failure='workflow command order invalid',
                            receiver=dict(units=0,bytes=0,sha256=hashlib.sha256(b'').hexdigest(),joined=True,error=0))
            events[1].update(event='event-rejected',phase='append')
            (directory/'capture.bin').write_bytes(b'')
            path=directory/'capture.bin.receiver.json';receiver=json.loads(path.read_text(encoding='utf-8'))
            receiver.update(saved_bytes=0,received_units=0);path.write_text(json.dumps(receiver),encoding='utf-8')
            (directory/'capture.bin.workflow.json').write_text(json.dumps(manifest),encoding='utf-8')
            progress=directory/'capture.bin.progress.jsonl'
            progress.write_text(''.join(json.dumps(x)+'\n' for x in events),encoding='utf-8')
            self.assertFalse(validate_rejected_order(directory,expected_generation=5)['normal_completion'])
            with self.assertRaises(ValueError):
                validate_fixture_case(directory,expected_role='F2',expected_generation=5,expected_input=b'')
            events[1].update(event='stop',phase=None)
            progress.write_text(''.join(json.dumps(x)+'\n' for x in events),encoding='utf-8')
            with self.assertRaises(ValueError):
                validate_rejected_order(directory,expected_generation=5)

    def test_flood_complete_and_interrupted_prefix(self):
        for complete in (True, False):
            with self.subTest(complete=complete),tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);manifest,_=synthetic_case(directory,'B')
                role='F1' if complete else 'F2'
                count=32000 if complete else 128
                rows=[]
                for number in range(count):
                    text=('TF_R108_'+role+'_'+str(number).zfill(5)+' 中文 e\u0301').encode()
                    rows.append(text+b'.'*(62-len(text))+b'\r\n')
                source=b''.join(rows);sha=hashlib.sha256(source).hexdigest()
                manifest.update(role=role,source_bytes=len(source),source_sha256=sha,records=count,
                                receiver=dict(units=0,bytes=0,sha256=hashlib.sha256(b'').hexdigest(),joined=True,error=0))
                manifest['stages']=[dict(name='output',first=0,requested_records=32000,records=count,
                                        bytes=len(source),source_begin=0,source_end=len(source),source_sha256=sha,
                                        begin_ms=10,end_ms=20,complete=complete)]
                definitions=[('ready',None,0),('event-start','output',0)]
                if complete: definitions.append(('output-complete','output',len(source)))
                definitions.extend([('stop',None,len(source)),('stopped',None,len(source))])
                events=[dict(schema='termforge-workflow-progress-v1',seq=i+1,elapsed_ms=i*10,
                             event=name,phase=phase,source_bytes=size,records=size//64,
                             source_sha256=hashlib.sha256(source[:size]).hexdigest())
                        for i,(name,phase,size) in enumerate(definitions)]
                receiver=json.loads((directory/'capture.bin.receiver.json').read_text(encoding='utf-8'))
                receiver.update(saved_bytes=0,received_units=0)
                (directory/'capture.bin.receiver.json').write_text(json.dumps(receiver),encoding='utf-8')
                (directory/'capture.bin').write_bytes(b'')
                (directory/'capture.bin.source.bin').write_bytes(source)
                manifest['console_window']=complete
                if complete:
                    synthetic_console(directory,role=role,lines=[rows[-1][:-2].decode('utf-8'),
                        f'TF_R108_{role}_OUTPUT_DONE 0500',''])
                (directory/'capture.bin.workflow.json').write_text(json.dumps(manifest),encoding='utf-8')
                (directory/'capture.bin.progress.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in events),encoding='utf-8')
                result=validate_fixture_case(directory,expected_role=role,expected_generation=1,expected_input=b'',expected_complete=complete)
                self.assertIs(result['complete'],complete)
                self.assertEqual(result['records'],count)
                self.assertIs(result['console_window'] is not None,complete)
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role=role,expected_generation=1,expected_input=b'',expected_complete=not complete)
                (directory/'capture.bin.source.bin').write_bytes(source.replace(b'00001',b'00000',1))
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role=role,expected_generation=1,expected_input=b'',expected_complete=complete)

    def test_explicit_stop_cancelled_read_is_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);synthetic_case(directory)
            path=directory/'capture.bin.receiver.json'
            value=json.loads(path.read_text(encoding='utf-8'))
            value.update(last_read_success=False,last_read_error=995,zero_read_accepted=False)
            path.write_text(json.dumps(value),encoding='utf-8')
            validate_fixture_case(directory,expected_role='A',expected_generation=1,expected_input=b'')

    def test_receiver_failures_cannot_hide_behind_manifest_success(self):
        for key,value in [('worker_joined',False),('stop_requested',False),('worker_error',5),
                          ('last_read_error',5),('zero_read_accepted',False),('last_cancel_error',5),
                          ('reads_completed',0),('saved_bytes',1),('last_wait_result',258)]:
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);synthetic_case(directory)
                path=directory/'capture.bin.receiver.json'
                sidecar=json.loads(path.read_text(encoding='utf-8'));sidecar[key]=value
                path.write_text(json.dumps(sidecar),encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role='A',expected_generation=1,expected_input=b'')

    def test_stage_hash_offset_completion_and_times_are_independent(self):
        for key,value in [('source_sha256','0'*64),('source_begin',1),('complete',False),
                          ('records',True),('begin_ms',0),('end_ms',9999)]:
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);manifest,_=synthetic_case(directory)
                manifest['stages'][0][key]=value
                (directory/'capture.bin.workflow.json').write_text(json.dumps(manifest),encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role='A',expected_generation=1,expected_input=b'')

    def test_a_numbered_batches_and_b_prefix_unicode_are_data_only(self):
        for role in ('A','B'):
            with self.subTest(role=role), tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);synthetic_case(directory,role,9)
                expected=b'' if role=='A' else (PREFIX+'你好').encode()
                result=validate_fixture_case(directory,expected_role=role,expected_generation=9,expected_input=expected)
                self.assertEqual(result['records'],680 if role=='A' else 0)
                self.assertFalse(result['ime_verified'])

    def test_reject_wrong_role_generation_raw_duplicate_and_changed_source(self):
        for defect in ('role','generation','duplicate','early_input','source','source_bom'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);manifest,_=synthetic_case(directory)
                if defect in ('role','generation'):
                    manifest[defect]='B' if defect=='role' else 2
                    (directory/'capture.bin.workflow.json').write_text(json.dumps(manifest),encoding='utf-8')
                elif defect in ('duplicate','early_input'):
                    (directory/'capture.bin').write_bytes(('你好你好' if defect=='duplicate' else 'x').encode())
                else:
                    source=directory/'capture.bin.source.bin';raw=source.read_bytes()
                    source.write_bytes(raw.replace(b'0000',b'0001',1) if defect=='source' else b'\xef\xbb\xbf'+raw)
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role='A',expected_generation=1,expected_input=b'')

    def test_reject_old_missing_reordered_or_fabricated_progress(self):
        for defect in ('old','missing','reordered','hash','offset','time','seq'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);_,events=synthetic_case(directory)
                if defect=='old': events[0]['schema']='termforge-product-ime-v1'
                if defect=='missing': events.pop(3)
                if defect=='reordered': events[1],events[2]=events[2],events[1]
                if defect=='hash': events[2]['source_sha256']='0'*64
                if defect=='offset': events[2]['source_bytes']+=1
                if defect=='time': events[3]['elapsed_ms']=0
                if defect=='seq': events[2]['seq']=True
                (directory/'capture.bin.progress.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in events),encoding='utf-8')
                with self.assertRaises(ValueError):
                    validate_fixture_case(directory,expected_role='A',expected_generation=1,expected_input=b'')


@unittest.skipUnless(os.name == 'nt', 'workflow fixture requires Windows ConPTY')
class WorkflowFixtureTests(unittest.TestCase):
    def test_two_fresh_controller_rounds(self):
        from experiments.conpty.run import OwnedJob
        executable, client = binary('workflow_fixture_test'), binary('shell_text_client')
        parent = Path(os.environ.get('TERMFORGE_TEST_ARTIFACTS', ROOT / 'build/test-artifacts')) / 'workflow-controller'
        parent.mkdir(parents=True, exist_ok=True)
        for round_number in range(2):
            directory = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
            cases = directory / 'cases'; cases.mkdir()
            record = dict(schema='termforge-workflow-controller-validation-v1', round=round_number,
                          data_only=True, ime_verified=False, clipboard_verified=False,
                          executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                          client_sha256=hashlib.sha256(client.read_bytes()).hexdigest(),
                          source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
                              'experiments/shell/workflow_controller.hpp', 'experiments/shell/workflow_fixture_test.cpp',
                              'experiments/shell/test_workflow.py', 'experiments/shell/workflow_checks.py',
                              'experiments/conpty/session.hpp', 'experiments/shell/workflow_fixture.hpp',
                              'experiments/shell/raw_console_receiver.hpp', 'experiments/shell/text_client.cpp')},
                          returncode=None, outer_job_after=None, forced_cleanup=False, timed_out=False,
                          process_reaped=False, error_kind=None, status='failed', cases=[], native_case_observations=[])
            job = OwnedJob(); process = None; stdout = stderr = b''
            try:
                process = subprocess.Popen([str(executable), '--controller-host', str(cases)], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                job.assign(process.pid)
                stdout, stderr = process.communicate(b'GO\n', timeout=25)
                record['returncode'] = process.returncode
                record['outer_job_after'] = job.drain(timeout=3)
                # Parse before the return-code assertion so a native failed
                # case retains its pre-close observation, independently of the
                # verified-success list below. Raw identity-bearing stdout is
                # still private and is never copied into this allowlist.
                host = json.loads(stdout)
                record['native_case_observations'] = controller_observations(host)
                self.assertEqual(process.returncode, 0, stderr.decode('utf-8', 'replace'))
                self.assertEqual(stderr, b'')
                self.assertEqual(record['outer_job_after']['active'], 0)
                self.assertEqual(host['schema'], 'termforge-workflow-controller-test-v1')
                self.assertIs(host['data_only'], True)
                self.assertIs(host['ime_or_global_input_used'], False)
                self.assertIs(host['passed'], True)
                specifications = [('B graceful', 'B', 21), ('A phases', 'A', 22), ('B closed', 'B', 23)]
                self.assertEqual([row['name'] for row in host['cases']], [row[0] for row in specifications])
                identities = set()
                for row, (name, role, generation) in zip(host['cases'], specifications):
                    with self.subTest(round=round_number, case=name):
                        self.assertEqual(row['role'], role)
                        self.assertIs(type(row['generation']), int)
                        self.assertEqual(row['generation'], generation)
                        self.assertIs(type(row['observed_generation']), int)
                        self.assertEqual(row['observed_generation'], generation)
                        for flag in ('launched_through_existing_cmd', 'identity_verified', 'child_joined', 'cleanup_complete', 'passed'):
                            self.assertIs(row[flag], True)
                        self.assertIs(type(row['inner_job_active_after']), int)
                        self.assertEqual(row['inner_job_active_after'], 0)
                        self.assertIs(type(row['identity_pid']), int)
                        self.assertGreater(row['identity_pid'], 0)
                        self.assertIs(type(row['process_creation_time']), int)
                        self.assertGreater(row['process_creation_time'], 0)
                        case = cases/name
                        identity = (case/'capture.bin.identity').read_bytes()
                        self.assertEqual(len(identity), 8)
                        self.assertEqual(struct.unpack('<II', identity), (row['identity_pid'], generation))
                        token = hashlib.sha256(f"{row['identity_pid']}:{row['process_creation_time']}".encode()).hexdigest()
                        self.assertNotIn(token, identities); identities.add(token)
                        closed = name == 'B closed'
                        for flag, expected in (('target_job_cancelled', closed), ('abnormal_completion_rejected', closed),
                                               ('child_finished', not closed), ('stopped_event', not closed),
                                               ('phases_complete', role == 'A'), ('parent_order_rejected', role == 'A')):
                            self.assertIs(row[flag], expected)
                        self.assertIs(type(row['child_exit_code']), int)
                        self.assertIs(type(row['cmd_exit_code']), int)
                        self.assertEqual(row['child_exit_code'], 91 if closed else 0)
                        self.assertEqual(row['cmd_exit_code'], 91 if closed else 0)
                        validate_controller_exit(row, closed=closed)
                        expected = (PREFIX+'你好').encode('utf-8') if name == 'B graceful' else b''
                        if closed:
                            # Job cancellation does not produce a normal receiver finish.
                            self.assertEqual((case/'capture.bin').read_bytes(), b'')
                            self.assertEqual((case/'capture.bin.source.bin').read_bytes(), b'')
                            self.assertEqual((case/'capture.bin.workflow.json').read_bytes(), b'')
                            self.assertFalse((case/'capture.bin.receiver.json').exists())
                            events = [json.loads(line) for line in (case/'capture.bin.progress.jsonl').read_bytes().splitlines()]
                            self.assertEqual(len(events), 1)
                            event = events[0]
                            self.assertEqual(event['schema'], 'termforge-workflow-progress-v1')
                            self.assertIs(type(event['seq']), int); self.assertEqual(event['seq'], 1)
                            self.assertEqual(event['event'], 'ready'); self.assertIsNone(event['phase'])
                            for key in ('elapsed_ms', 'source_bytes', 'records'):
                                self.assertIs(type(event[key]), int); self.assertGreaterEqual(event[key], 0)
                            self.assertEqual(event['source_bytes'], 0); self.assertEqual(event['records'], 0)
                            self.assertEqual(event['source_sha256'], hashlib.sha256(b'').hexdigest())
                            verified = dict(graceful_fixture_completion=False, closed_target_exit=91,
                                            empty_unfinished_manifest_verified=True, receiver_sidecar_absent=True)
                        else:
                            verified = validate_fixture_case(case, expected_role=role, expected_generation=generation,
                                                             expected_input=expected)
                        transport_path = case/'transport.bin'
                        self.assertLessEqual(transport_path.stat().st_size, 4*1024*1024)
                        transport = transport_path.read_bytes()
                        markers = [f'TF_R108_{role}_READY'.encode()]
                        if role == 'A':
                            markers += [b'TF_R108_A_OUTPUT_DONE 0160', b'TF_R108_A_APPEND_DONE 0020', b'TF_R108_A_EVICT_DONE 0500']
                        if not closed:
                            markers += [f'TF_R108_{role}_STOPPED'.encode()]
                        self.assertTrue(all(transport.count(marker) == 1 for marker in markers))
                        self.assertEqual([transport.index(marker) for marker in markers], sorted(transport.index(marker) for marker in markers))
                        if role == 'B':
                            self.assertNotIn(b'_DONE ', transport)
                        if closed:
                            self.assertNotIn(b'TF_R108_B_STOPPED', transport)
                        source = (case/'capture.bin.source.bin').read_bytes()
                        observed = re.findall(rb'TF_R108_(?:A|B|F[1-5])_[0-9]{4,5} [^\r\n\x1b]*', transport)
                        self.assertEqual(observed, source.splitlines())
                        observation = {key:value for key,value in row.items() if key not in ('identity_pid', 'process_creation_time')}
                        record['cases'].append(dict(observation=observation, process_identity_token=token,
                                                    private_identity_verified=True, fixture=verified, transport_verified=True,
                                                    transport_bytes=len(transport), transport_sha256=hashlib.sha256(transport).hexdigest()))
                # unittest subTest retains the original failure and continues;
                # partial case evidence must also leave the persisted run failed.
                self.assertEqual(len(record['cases']), 3, 'one or more controller cases were rejected')
                record['status'] = 'passed'
            except subprocess.TimeoutExpired:
                record.update(timed_out=True, error_kind='deadline-exceeded')
                raise
            except BaseException as error:
                record['error_kind'] = type(error).__name__
                raise
            finally:
                if process and process.poll() is None:
                    record['forced_cleanup'] = True
                    job.terminate(); process.kill()
                    stdout, stderr = process.communicate(timeout=5)
                if process:
                    record['returncode'] = process.poll()
                    record['process_reaped'] = process.poll() is not None
                try:
                    record['outer_job_after'] = job.drain(timeout=3)
                finally:
                    job.close()
                    (directory/'stdout.private.json').write_bytes(stdout)
                    (directory/'stderr.txt').write_bytes(stderr)
                    (directory/'validation.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')

    def test_two_fresh_owned_data_only_rounds(self):
        from experiments.conpty.run import OwnedJob
        executable = binary('workflow_fixture_test')
        client = binary('shell_text_client')
        parent = Path(os.environ.get('TERMFORGE_TEST_ARTIFACTS', ROOT / 'build/test-artifacts')) / 'workflow-fixture'
        parent.mkdir(parents=True, exist_ok=True)
        for round_number in range(2):
            directory = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
            cases = directory / 'cases'; cases.mkdir()
            record = dict(schema='termforge-workflow-fixture-validation-v1', round=round_number,
                          data_only=True, ime_verified=False, clipboard_verified=False,
                          executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                          client_sha256=hashlib.sha256(client.read_bytes()).hexdigest(),
                          returncode=None, outer_job_after=None, forced_cleanup=False,
                          timed_out=False, process_reaped=False, error_kind=None, status='failed')
            job = OwnedJob(); process = None; stdout = stderr = b''
            try:
                process = subprocess.Popen([str(executable), '--host', str(cases)], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                job.assign(process.pid)
                stdout, stderr = process.communicate(b'GO\n', timeout=35)
                record['returncode'] = process.returncode
                record['outer_job_after'] = job.drain(timeout=3)
                self.assertEqual(process.returncode, 0, stderr.decode('utf-8', 'replace'))
                self.assertEqual(stderr, b'')
                self.assertEqual(record['outer_job_after']['active'], 0)
                record['cases'] = validate_fixture_host(cases, json.loads(stdout))
                record['status'] = 'passed'
            except subprocess.TimeoutExpired:
                record.update(timed_out=True, error_kind='deadline-exceeded')
                raise
            except BaseException as error:
                record['error_kind'] = type(error).__name__
                raise
            finally:
                if process and process.poll() is None:
                    record['forced_cleanup'] = True
                    job.terminate(); process.kill()
                    stdout, stderr = process.communicate(timeout=5)
                if process:
                    record['returncode'] = process.poll()
                    record['process_reaped'] = process.poll() is not None
                try:
                    record['outer_job_after'] = job.drain(timeout=3)
                finally:
                    job.close()
                    (directory / 'stdout.json').write_bytes(stdout)
                    (directory / 'stderr.txt').write_bytes(stderr)
                    (directory / 'validation.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')


if __name__=='__main__':
    unittest.main()
