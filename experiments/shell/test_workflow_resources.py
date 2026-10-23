"""Actual data-only owned chain plus resource evidence/schema checks; no clipboard/IME."""
import json
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))


def wait_for(predicate, detail, timeout=5):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError(detail)
        time.sleep(.01)


def records(path):
    if not path.exists():
        return []
    data = path.read_bytes()
    if len(data) > 2 * 1024 * 1024:
        raise AssertionError('controlled short trace exceeded test budget')
    return [json.loads(line) for line in data.splitlines(keepends=True) if line.endswith(b'\n')]


def resource_fixture():
    def observed(pid, group, lifetime, delta, memory, handles):
        return dict(pid=pid, create_time=1000.0, group=group, status='observed',
                    cpu_seconds_lifetime=lifetime, cpu_seconds_since_first_observation=delta,
                    private_bytes=memory, working_set_bytes=memory*2, handles=handles)
    metadata = dict(type='metadata', schema_version='0.1', root_pid=41, root_create_time=1000.0,
                    psutil_version='7.2.2', clock='perf_counter_ns', start_counter_ns=100,
                    requested_interval_ns=1000000000, requested_discovery_interval_ns=1000000000)
    samples = []
    for index, processes in enumerate([
        [observed(41,'root',10,0,5,5), observed(42,'descendant',4,0,100,100),
         observed(43,'descendant',7,0,1,1), observed(90,'sampler',20,0,777,777)],
        [observed(41,'root',12,2,5,5), observed(42,'descendant',7,3,1,1),
         observed(43,'descendant',7,0,100,100), observed(90,'sampler',21,1,999,999)],
        [observed(41,'root',13,3,5,5), dict(pid=42,create_time=1000.0,group='descendant',status='NoSuchProcess'),
         dict(pid=43,create_time=1000.0,group='descendant',status='AccessDenied'), observed(90,'sampler',22,2,888,888)]
    ]):
        samples.append(dict(type='sample',elapsed_ns=index*1000000000,
                            actual_interval_ns=None if index==0 else 1000000000,
                            collection_ns=10+index,schedule_lateness_ns=index,processes=processes,errors=[]))
    return [metadata,*samples,dict(type='end',elapsed_ns=3000000000,status='complete')]


def write_fixture(path, rows):
    path.write_text(''.join(json.dumps(row,allow_nan=False)+'\n' for row in rows),encoding='utf-8')


class PublicResourceEvidence(unittest.TestCase):
    def test_derived_schema_recomputes_simultaneous_tree_peaks_and_retains_exited_cpu(self):
        from experiments.shell import workflow_resources as resources
        with tempfile.TemporaryDirectory() as directory:
            raw, public = Path(directory)/'raw.jsonl', Path(directory)/'resource-observations.jsonl'
            write_fixture(raw,resource_fixture())
            report = resources.write_resource_observations(raw,public,expected_identity=(41,1000.0))
            self.assertEqual(report,resources.summarize_resource_observations(public))
            self.assertEqual(report['scope'],'launcher-tree')
            self.assertEqual(report['launcher_tree']['sampled_peak_observed_private_bytes'],106)
            self.assertEqual(report['launcher_tree']['sampled_peak_observed_handles'],106)
            self.assertEqual(report['launcher_tree']['observed_cpu_seconds'],6)
            self.assertEqual(report['launcher_tree']['observed_cpu_core_equivalents'],3)
            self.assertEqual(report['groups']['sampler']['sampled_peak_observed_private_bytes'],999)
            derived=records(public)
            self.assertEqual(derived[0]['schema'],'termforge-workflow-resource-observations-v1')
            self.assertNotIn('pid',derived[1]['processes'][0])
            self.assertNotIn('create_time',derived[1]['processes'][0])
            unavailable=derived[3]['processes'][1]
            self.assertIsNone(unavailable['handles']); self.assertIsNone(unavailable['private_bytes'])
            from benchmarks.tools.summarize_resources import summarize
            with self.assertRaises(ValueError):
                summarize(public) # New schema must never masquerade as fake PID v0.1.

    def test_identity_hash_is_canonical_and_pid_reuse_has_a_new_baseline(self):
        from experiments.shell import workflow_resources as resources
        self.assertEqual(resources.resource_process_identity(41,1000),'c6cd26a7ea94e8474b6720cf32c2f7a85c46d02a1523aa5472bf1b51fc7d5ce1')
        self.assertEqual(resources.resource_process_identity(41,1000.0),'c6cd26a7ea94e8474b6720cf32c2f7a85c46d02a1523aa5472bf1b51fc7d5ce1')
        self.assertEqual(resources.resource_process_identity(41,1001.0),'6ef2231750eb13215ef85f654265e1aed9297bdbac4d1990e265f1925aab9df7')
        for pid,created in ((True,1000),(41,True),(41,float('nan')),(41,float('inf')),(41,0)):
            with self.subTest(pid=pid,created=created),self.assertRaises(ValueError):
                resources.resource_process_identity(pid,created)
        rows=resource_fixture()
        rows[3]['processes'][1]=dict(pid=42,create_time=1001.0,group='descendant',status='observed',
                                    cpu_seconds_lifetime=100,cpu_seconds_since_first_observation=0,
                                    private_bytes=7,working_set_bytes=14,handles=2)
        rows[3]['errors']=[{'pid':42,'operation':'children','error':'AccessDenied'}]
        with tempfile.TemporaryDirectory() as directory:
            raw,public=Path(directory)/'raw.jsonl',Path(directory)/'resource-observations.jsonl'
            write_fixture(raw,rows)
            report=resources.write_resource_observations(raw,public,expected_identity=(41,1000.0))
            self.assertEqual(report['groups']['descendant']['observed_processes'],3)
            self.assertEqual(report['launcher_tree']['observed_cpu_seconds'],6)
            derived=records(public)
            self.assertEqual(derived[1]['processes'][1]['process_identity'],'05b706b66e9181f6eae3da502e05c3af758c8ae454940019a83e0e170800dbb9')
            self.assertEqual(derived[3]['processes'][1]['process_identity'],'2489c96dfa7120a342637c18366615b1e9d3170825e8cddbead40ac96cbe3af7')
            self.assertIsNone(derived[3]['errors'][0]['process_identity'])

    def test_unobserved_is_null_and_real_observed_zero_stays_zero(self):
        from experiments.shell import workflow_resources as resources
        rows=resource_fixture(); rows=[rows[0],rows[1],rows[-1]]
        rows[1]['processes']=[dict(pid=41,create_time=1000.0,group='root',status='AccessDenied'),
                              dict(pid=42,create_time=1000.0,group='descendant',status='NoSuchProcess'),
                              dict(pid=90,create_time=1000.0,group='sampler',status='observed',
                                   cpu_seconds_lifetime=20,cpu_seconds_since_first_observation=0,
                                   private_bytes=0,working_set_bytes=0,handles=0)]
        with tempfile.TemporaryDirectory() as directory:
            raw,public=Path(directory)/'raw.jsonl',Path(directory)/'resource-observations.jsonl'
            write_fixture(raw,rows)
            report=resources.write_resource_observations(raw,public,expected_identity=(41,1000.0))
            for group in ('root','descendant'):
                for key in ('observed_cpu_seconds','sampled_peak_observed_private_bytes','sampled_peak_observed_handles'):
                    self.assertIsNone(report['groups'][group][key])
            self.assertIsNone(report['launcher_tree']['sampled_peak_observed_handles'])
            self.assertEqual(report['groups']['sampler']['sampled_peak_observed_handles'],0)
            self.assertIsNone(report['groups']['sampler']['observed_cpu_core_equivalents'])

    def test_public_metrics_and_privacy_are_independently_checked_against_raw(self):
        from experiments.shell import workflow_resources as resources
        rows=resource_fixture()
        rows[0]['private_path']='PRIVATE_PATH_SENTINEL'
        rows[1]['processes'][0]['cmdline']='PRIVATE_COMMAND_SENTINEL'
        rows[2]['errors']=[{'pid':41,'operation':'children','error':'AccessDenied','path':'PRIVATE_ERROR_SENTINEL'}]
        with tempfile.TemporaryDirectory() as directory:
            raw,public=Path(directory)/'raw.jsonl',Path(directory)/'resource-observations.jsonl'
            write_fixture(raw,rows)
            resources.write_resource_observations(raw,public,expected_identity=(41,1000.0))
            self.assertNotIn('PRIVATE_',public.read_text(encoding='utf-8'))
            good=records(public)
            mutations={
                'pid_leak':lambda r:r[1]['processes'][0].update(pid=41),
                'metadata_path':lambda r:r[0].update(path='PRIVATE_PATH_SENTINEL'),
                'raw_error':lambda r:r[2]['errors'][0].update(error='AccessDenied PRIVATE_ERROR_SENTINEL'),
                'extra_error_field':lambda r:r[2]['errors'][0].update(pid=41),
                'wrong_root':lambda r:r[0].update(root_identity='0'*64),
                'role_change':lambda r:r[2]['processes'][1].update(group='sampler'),
                'same_sample_duplicate':lambda r:r[1]['processes'].append(copy.deepcopy(r[1]['processes'][0])),
                'different_actual_measurement':lambda r:r[1]['processes'][0].update(private_bytes=6),
                'wrong_source':lambda r:r[0].update(source_sha256='0'*64),
                'bad_cpu':lambda r:r[2]['processes'][0].update(cpu_seconds_since_first_observation=999),
                'bad_interval':lambda r:r[2].update(actual_interval_ns=7),
                'unavailable_faked_zero':lambda r:r[3]['processes'][1].update(handles=0),
                'truncated':lambda r:r.pop(),
                'after_end':lambda r:r.append(copy.deepcopy(r[1])),
                'future_end':lambda r:r[-1].update(status='future'),
            }
            for name,mutate in mutations.items():
                broken=copy.deepcopy(good); mutate(broken); write_fixture(public,broken)
                with self.subTest(name=name),self.assertRaises(ValueError):
                    resources.validate_resource_observations(raw,public,expected_identity=(41,1000.0))

    def test_wrong_root_or_truncated_raw_never_becomes_public_evidence(self):
        from experiments.shell import workflow_resources as resources
        with tempfile.TemporaryDirectory() as directory:
            raw,public=Path(directory)/'raw.jsonl',Path(directory)/'resource-observations.jsonl'
            write_fixture(raw,resource_fixture())
            with self.assertRaises(ValueError):
                resources.write_resource_observations(raw,public,expected_identity=(41,1001.0))
            self.assertFalse(public.exists())
            write_fixture(raw,resource_fixture()[:-1])
            with self.assertRaises(ValueError):
                resources.write_resource_observations(raw,public,expected_identity=(41,1000.0))
            self.assertFalse(public.exists())

    def test_never_started_finish_does_not_claim_a_thread_join_or_start_later(self):
        from experiments.shell import workflow_resources as resources
        with tempfile.TemporaryDirectory() as directory:
            trace=resources.WorkflowResources(object(),Path(directory)/'private')
            with self.assertRaises(ValueError):trace.mark('launcher-go')
            result=trace.finish()
            self.assertFalse(result['sampler_started']);self.assertFalse(result['sampler_joined'])
            self.assertIsNone(result['launcher_tree'])
            with self.assertRaises((ValueError,RuntimeError)):trace.start()

    def test_configuration_rejects_sleep_intervals_beyond_stop_budget_before_any_process_access(self):
        from experiments.shell import workflow_resources as resources
        with tempfile.TemporaryDirectory() as directory:
            private=Path(directory)/'private'
            for interval in (2,0,True,float('nan')):
                with self.subTest(interval=interval),self.assertRaises(ValueError):
                    resources.WorkflowResources(object(),private,interval=interval)
            self.assertFalse(private.exists())


@unittest.skipUnless(os.name == 'nt', 'Windows private bytes and owned Job')
class ActualLauncherTree(unittest.TestCase):
    def test_owned_go_chain_reuses_real_sampler_and_retains_unknown_phase_mapping(self):
        from experiments.conpty.run import OwnedJob
        from experiments.shell.workflow_resources import WorkflowResources
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            ready, stop = out / 'child-ready', out / 'stop'
            job, process, trace = OwnedJob(), None, None
            try:
                process = subprocess.Popen([getattr(sys, '_base_executable', sys.executable), str(Path(__file__).resolve()),
                                            '--fixture-parent', str(ready), str(stop)],
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                job.assign(process.pid)
                trace = WorkflowResources(process, out / '_resources-private', seconds=8, interval=.05,
                                          discovery_interval=.1)
                trace.start()
                first = records(trace.trace_path)
                self.assertEqual(first[0]['root_pid'], process.pid)
                self.assertTrue(any(p['group'] == 'root' and p['status'] == 'observed'
                                    for row in first if row['type'] == 'sample' for p in row['processes']))
                self.assertFalse(ready.exists(), 'GO must follow Job assignment and the first real launcher sample')
                trace.mark('launcher-go'); process.stdin.write(b'GO\n'); process.stdin.flush()
                wait_for(ready.exists, 'actual owned child did not signal ready')
                trace.mark('controlled-child-ready')

                def descendant_observed():
                    return any(p['group'] == 'descendant' and p['status'] == 'observed' and
                               p['private_bytes'] >= 16 * 1024 * 1024
                               for row in records(trace.trace_path) if row['type'] == 'sample' for p in row['processes'])

                wait_for(descendant_observed, 'actual child allocation was not discovered')
                stop.write_text('stop', encoding='ascii')
                stdout, stderr = process.communicate(timeout=5)
                self.assertEqual((process.returncode, stderr), (0, b''))
                self.assertEqual(json.loads(stdout), {'child_exit_code': 0})
                self.assertEqual(job.drain()['active'], 0)
                trace.mark('cleanup-finished')
                report = trace.finish(observations_path=out/'resource-observations.jsonl')
                self.assertEqual(report['scope'], 'launcher-tree')
                self.assertTrue(report['root_identity_verified'])
                self.assertTrue(report['sampler_joined'])
                self.assertEqual(report['performance_gate'], 'not_evaluated')
                self.assertGreaterEqual(report['trace']['sample_count'], 2)
                self.assertGreaterEqual(report['trace']['groups']['descendant']['observed_processes'], 1)
                self.assertGreaterEqual(report['launcher_tree']['sampled_peak_observed_private_bytes'], 16 * 1024 * 1024)
                self.assertGreater(report['launcher_tree']['sampled_peak_observed_handles'], 0)
                self.assertIsNone(report['phase_windows'])
                self.assertEqual(report['observations']['summary']['launcher_tree'],report['launcher_tree'])
                self.assertEqual(report['dependencies']['psutil_version'],'7.2.2')
            finally:
                if process is not None and process.poll() is None:
                    job.terminate(); process.communicate(timeout=5)
                if trace is not None:
                    trace.finish()
                self.assertEqual(job.drain()['active'], 0)
                job.close()


def fixture_child(ready, stop):
    allocation = bytearray(16 * 1024 * 1024)
    ready.write_text('ready', encoding='ascii')
    deadline = time.monotonic() + 6
    while not stop.exists():
        if time.monotonic() >= deadline:
            return 9
        allocation[0] = sum(range(10000)) % 256
    return 0


def fixture_parent(ready, stop):
    if sys.stdin.buffer.readline(4) != b'GO\n':
        return 8
    child = subprocess.Popen([getattr(sys, '_base_executable', sys.executable), str(Path(__file__).resolve()),
                              '--fixture-child', str(ready), str(stop)], creationflags=subprocess.CREATE_NO_WINDOW)
    code = child.wait(timeout=8)
    print(json.dumps({'child_exit_code': code}), flush=True)
    return code


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] in ('--fixture-parent', '--fixture-child'):
        function = fixture_parent if sys.argv[1] == '--fixture-parent' else fixture_child
        raise SystemExit(function(Path(sys.argv[2]), Path(sys.argv[3])))
    unittest.main()
