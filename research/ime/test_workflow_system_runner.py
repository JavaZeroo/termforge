"""Public runner policy and real benign GO seam; never forge hosted input."""
import importlib.util
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from research.ime import workflow_system_runner as runner
from research.ime import product_runner
from research.ime.inventory_hosted import require_hosted

NATIVE_LITERAL = (b'{ "valid":true,"mode":"ui-workflow-system","sessions_closed":18,"active_children_after":0,"presented_done":null }\r\n'
    b'{"schema":"termforge-hosted-ime-launcher-v1","mode":"ui-workflow-system","child_exit_code":0,"child_signaled":true,"timed_out":false,"inner_job_after":{"active":0,"total":80,"terminated":0}}\r\n')


class WorkflowSystemRunnerPolicyTests(unittest.TestCase):
    def test_effective_entry_rejects_actual_local_environment_before_resolving_or_creating(self):
        try:
            require_hosted(os.environ)
        except ValueError:
            pass
        else:
            self.skipTest('hosted VM is reserved for the explicitly invoked effective runner')
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / 'must-not-exist'
            with self.assertRaisesRegex(ValueError, 'designated disposable'):
                runner.execute_hosted_workflow(out / 'missing.exe', out)
            self.assertFalse(out.exists())

    def test_import_and_legacy_tuple_do_not_enable_resources_or_new_input(self):
        subprocess.run([sys.executable, '-c', 'import sys; import research.ime.workflow_system_runner; '
                        'assert "psutil" not in sys.modules'], cwd=runner.ROOT, check=True, timeout=10)
        self.assertEqual(product_runner.MODES, ('ui-ime-basic', 'ui-ime-geometry', 'ui-ime-focus',
                                               'ui-ime-close', 'ui-ime-reading'))
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / 'must-not-exist'
            with self.assertRaises(ValueError):
                product_runner._run_gated_command('ui-ime-basic', out, ['unused'], 1,
                                                   resource_observation=True)
            self.assertFalse(out.exists())

    def test_public_failure_keeps_unknowns_and_drops_private_values(self):
        record = dict(status='failed', mode=runner.MODE, pid=123456, error={'detail':'C:/private/SECRET'},
                      stdout='private text', cleanup_errors=[{'detail':'private error'}],
                      lifecycle=[{'stage':'starting','elapsed_seconds':0.01,'pid':123456}], environment={'TOKEN':'secret'},
                      outer_job_after=None, content_verified=False, resources_verified=False)
        result = runner._public_case(record, 0)
        self.assertEqual(result['outer_job_after'], None)
        self.assertEqual(result['cleanup_error_count'], 1)
        self.assertFalse(result['real_system_workflow_verified'])
        self.assertFalse(result['complete_r1_verified'])
        self.assertIsNone(result['presented_done'])
        text = json.dumps(result)
        self.assertEqual(result['lifecycle'], [{'stage':'starting','elapsed_seconds':0.01}])
        for private in ('123456', 'C:/private', 'SECRET', 'TOKEN', 'secret', 'private text', 'environment'):
            self.assertNotIn(private, text)

    def test_safe_export_preserves_original_native_bytes_and_withholds_error_text(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary); (out/'stdout.jsonl').write_bytes(NATIVE_LITERAL)
            (out/'stderr.txt').write_bytes(b'')
            record={'streams':{name:product_runner.file_identity(out/name) for name in ('stdout.jsonl','stderr.txt')}}
            value=runner._export_streams(out,record)
            self.assertEqual((out/'public/stdout.jsonl').read_bytes(),NATIVE_LITERAL)
            self.assertEqual(value['files'],{'stdout.jsonl':'public/stdout.jsonl','stderr.txt':'public/stderr.txt'})
        for raw in (NATIVE_LITERAL+b'private/path\n',NATIVE_LITERAL.replace(b'"total":80',b'"pid":123,"total":80'),
                    NATIVE_LITERAL.replace(b'{ "valid":true',b'{ "valid":"C:/private/SECRET","valid":true'),
                    NATIVE_LITERAL.replace(b'"total":80',b'"total":"C:/private/SECRET","total":80')):
            with self.subTest(raw=raw[:20]),tempfile.TemporaryDirectory() as temporary:
                out=Path(temporary);(out/'stdout.jsonl').write_bytes(raw);(out/'stderr.txt').write_bytes(b'SECRET/path')
                record={'streams':{name:product_runner.file_identity(out/name) for name in ('stdout.jsonl','stderr.txt')}}
                value=runner._export_streams(out,record)
                self.assertEqual(value['files'],{'stdout.jsonl':None,'stderr.txt':None})
                self.assertFalse((out/'public').exists())
                from research.ime.workflow_system_checks import _validate_native_output
                with self.assertRaises(ValueError):_validate_native_output(raw)

    def test_public_go_and_original_streams_have_an_independent_audit_gate(self):
        from research.ime.workflow_system_checks import validate_system_public_record,validate_workflow_system_public
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary);(out/'stdout.jsonl').write_bytes(NATIVE_LITERAL);(out/'stderr.txt').write_bytes(b'')
            record=dict(status='passed',assigned=True,gate_released=True,process_reaped=True,timed_out=False,
                        forced_job_termination=False,returncode=0,cleanup_errors=[],evidence_errors=[],
                        outer_job_before_cleanup={'active':0,'total':80,'terminated':0},
                        outer_job_after={'active':0,'total':80,'terminated':0},content_verified=True,resources_verified=True,
                        verification=dict(snapshots=13,ordered_events=28,guarded_key_batches=9,native_shortcuts=4,
                                          companion_observations=169,combined_observations=406),
                        resources_verification=dict(scope='launcher-tree',phase_status='mapped',performance_gate='not_evaluated',sample_count=20),
                        lifecycle=[dict(stage=s,elapsed_seconds=i*.1) for i,s in enumerate(
                            ('starting','awaiting-go','job-assigned','releasing-go','finished'))],
                        streams={name:product_runner.file_identity(out/name) for name in ('stdout.jsonl','stderr.txt')})
            record['public_stream_export']=runner._export_streams(out,record)
            public=runner._public_case(record,0);validate_system_public_record(public)
            for change in (lambda r:r['lifecycle'].reverse(),lambda r:r['lifecycle'][0].update(elapsed_seconds=-.01),
                           lambda r:r['outer_job_after'].update(active=1),lambda r:r.update(pid=123),
                           lambda r:r.update(returncode=True),lambda r:r.update(public_streams={'stdout.jsonl':'../private','stderr.txt':None})):
                bad=copy.deepcopy(public);change(bad)
                with self.assertRaises(ValueError):validate_system_public_record(bad)
            (out/'result.public.json').write_text(json.dumps(public),encoding='utf-8')
            # Host metadata and valid native bytes alone cannot stand in for
            # the missing actual 26-checkpoint content/fixture evidence.
            with self.assertRaises((ValueError,OSError)):validate_workflow_system_public(out)
            (out/'public/stdout.jsonl').write_bytes(NATIVE_LITERAL.replace(b'18',b'17',1))
            with self.assertRaisesRegex(ValueError,'digest mismatch'):validate_workflow_system_public(out)

    def test_failure_classes_are_safe_and_explain_native_or_resource_boundaries(self):
        for private,expected in (({'timed_out':True},'NativeDeadlineExceeded'),
                                 ({'returncode':2},'NativeProcessFailed'),
                                 ({'failure_stage':'resources'},'ResourceEvidenceRejected'),
                                 ({'failure_stage':'content'},'ContentOracleRejected')):
            self.assertEqual(runner._public_case(dict(status='failed',**private),0)['error_kind'],expected)

    def test_content_pass_does_not_turn_unknown_resources_into_overall_pass(self):
        record = dict(status='failed', content_verified=True, resources_verified=False,
                      verification=dict(snapshots=13, ordered_events=28, guarded_key_batches=9,
                                        native_shortcuts=4, companion_observations=169, combined_observations=406))
        value = runner._public_case(record, 0)
        self.assertTrue(value['content_verified'])
        self.assertFalse(value['resources_verified'])
        self.assertEqual(value['status'], 'failed')
        record['status'] = 'passed'
        with self.assertRaises(ValueError): runner._public_case(record, 0)

    def test_boolean_counts_and_injected_resource_strings_are_rejected(self):
        with self.assertRaises(ValueError):
            runner._public_case({'outer_job_after':{'active':False,'total':1,'terminated':0}}, 0)
        record = dict(content_verified=True, verification=dict(snapshots=True, ordered_events=28,
                      guarded_key_batches=9, native_shortcuts=4, companion_observations=169, combined_observations=406))
        with self.assertRaises(ValueError): runner._public_case(record, 0)
        record = dict(resources_verified=True, resources_verification=dict(scope='C:/private',
                      phase_status='mapped',performance_gate='not_evaluated',sample_count=10))
        with self.assertRaises(ValueError): runner._public_case(record, 0)


@unittest.skipUnless(os.name == 'nt' and importlib.util.find_spec('psutil') is not None,
                     'benign owned resources seam requires Windows and declared psutil')
class WorkflowSystemRunnerBenignJobTests(unittest.TestCase):
    def test_real_benign_gate_starts_sampler_before_go_and_joins_after_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            child = directory / 'benign.py'
            child.write_text("import sys,time\nassert sys.stdin.readline() == 'GO\\n'\n"
                             "print('BENIGN_GO_RECEIVED',flush=True)\ntime.sleep(.25)\n", encoding='utf-8')
            out = directory / 'case'
            row = product_runner._run_gated_command(runner.MODE, out,
                           [sys.executable, str(child)], 2, resource_observation=True)
            self.assertEqual(row['status'], 'collected')
            self.assertTrue(row['assigned'] and row['gate_released'] and row['process_reaped'])
            self.assertFalse(row['forced_job_termination'] or row['timed_out'] or row['real_ime_product_verified'])
            self.assertEqual(row['outer_job_after']['active'], 0)
            self.assertEqual((out/'stdout.jsonl').read_bytes(), b'BENIGN_GO_RECEIVED\r\n')
            summary = json.loads((out/'resource-summary.json').read_bytes())
            self.assertTrue(summary['sampler_joined'] and summary['root_identity_verified'])
            self.assertEqual([r['label'] for r in summary['marks']],
                             ['launcher-go', 'launcher-exit', 'cleanup-finished'])
            rows = [json.loads(line) for line in (out/'_resources-private/resources.jsonl').read_bytes().splitlines()]
            first = next(r for r in rows if r.get('type') == 'sample')
            self.assertLessEqual(first['elapsed_ns'], summary['marks'][0]['elapsed_ns'])
            self.assertEqual(summary['phase_windows']['status'], 'unknown')
            with self.assertRaises(ValueError): runner._verify_resources(out)


if __name__ == '__main__':
    unittest.main()
