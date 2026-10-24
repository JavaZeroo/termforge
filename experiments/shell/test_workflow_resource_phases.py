"""Clock/phase evidence tests. The owned fixture only emits data, never UI input."""
import json
import copy
from contextlib import nullcontext
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))


def phase_fixture(out):
    from experiments.shell.test_workflow_resources import resource_fixture,write_fixture
    from experiments.shell.workflow_resources import write_resource_observations
    raw,observations=out/'raw.jsonl',out/'resource-observations.jsonl'
    write_fixture(raw,resource_fixture());write_resource_observations(raw,observations,expected_identity=(41,1000.0))
    origin=10_000_000_000
    points=[]
    for batch,elapsed in [('before-go',-100),('after-cleanup',20_000_000_000)]:
        for index in range(4):
            value=elapsed+index*10
            points.append(dict(batch=batch,perf_before_ns=value-2,perf_after_ns=value+2,
                               qpc_counter=origin+value,qpc_frequency=1_000_000_000))
    calibration=dict(implementation='QueryPerformanceCounter()',monotonic=True,adjustable=False,resolution_ns=1,points=points)
    rows=[]
    stages=[('eight-ready',0,8,8,100_000_000),('load-started',0,8,8,200_000_000),('background-complete',0,8,8,300_000_000)]
    for cycle in range(1,6):
        start=500_000_000+(cycle-1)*3_000_000_000
        stages.extend([('cycle-six',cycle,6,6+cycle*2,start),('cycle-six-end',cycle,6,6+cycle*2,start+2_000_000_000),
                       ('cycle-eight',cycle,8,8+cycle*2,start+2_100_000_000)])
    stages.append(('after-paste-close',5,7,18,15_000_000_000))
    for seq,(phase,cycle,live,creations,elapsed) in enumerate(stages,1):
        rows.append(dict(schema='termforge-workflow-resource-phase-v1',seq=seq,phase=phase,cycle=cycle,
                         qpc_counter=origin+elapsed,qpc_frequency=1_000_000_000,live_sessions=live,window_creations=creations))
    native=out/'resource-phases.jsonl';native.write_text(''.join(json.dumps(row)+'\n' for row in rows))
    marks=[dict(label='launcher-go',elapsed_ns=1,source='python-runner-observation')]
    return observations,native,calibration,marks


class PublicClockEvidence(unittest.TestCase):
    def test_published_calibration_replays_without_private_clock_epoch(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            first=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            self.assertEqual(first['status'],'mapped')
            second=summarize_resource_phases(observations,native,calibration=first['calibration'],marks=marks)
            self.assertEqual(second,first)

    def test_cpu_starts_inside_window_and_unavailable_descendants_are_unknown(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            first=next(w for w in report['windows'] if w['kind']=='six-resident')
            self.assertEqual(first['sample_sequences'],[2,3])
            self.assertEqual(first['launcher_tree']['sampled_peak_observed_private_bytes'],106)
            self.assertEqual(first['launcher_tree']['sampled_peak_observed_handles'],106)
            self.assertEqual(first['launcher_tree']['observed_accounted_cpu_seconds'],1)
            self.assertEqual(first['groups']['sampler']['observed_accounted_cpu_seconds'],1)
            self.assertIsNone(first['groups']['descendant']['observed_accounted_cpu_seconds'])
            self.assertEqual(first['groups']['descendant']['unavailable_rows'],2)
            self.assertTrue(all(w['launcher_tree']['observed_accounted_cpu_seconds'] is None for w in report['windows'] if w['kind']=='creation'))
            self.assertTrue(all(t['launcher_tree']['sampled_peak_observed_handles'] is None for t in report['six_resident_observations'][1:]))

    def test_collection_crossing_end_is_excluded_including_its_cpu(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        from experiments.shell.test_workflow_resources import resource_fixture,write_fixture
        from experiments.shell.workflow_resources import write_resource_observations
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);observations,native,calibration,marks=phase_fixture(out)
            rows=resource_fixture();rows[3]['collection_ns']=600_000_000
            write_fixture(out/'raw.jsonl',rows);observations.unlink()
            write_resource_observations(out/'raw.jsonl',observations,expected_identity=(41,1000.0))
            report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            first=next(w for w in report['windows'] if w['kind']=='six-resident')
            self.assertEqual(first['sample_sequences'],[2])
            self.assertIsNone(first['launcher_tree']['observed_accounted_cpu_seconds'])

    def test_collection_touching_uncertain_start_is_excluded(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        from experiments.shell.test_workflow_resources import resource_fixture,write_fixture
        from experiments.shell.workflow_resources import write_resource_observations
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);observations,native,calibration,marks=phase_fixture(out)
            rows=resource_fixture();rows[2]['elapsed_ns']=500000004;rows[2]['actual_interval_ns']=500000004
            rows[3]['actual_interval_ns']=1499999996
            write_fixture(out/'raw.jsonl',rows);observations.unlink()
            write_resource_observations(out/'raw.jsonl',observations,expected_identity=(41,1000.0))
            result=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            first=next(w for w in result['windows'] if w['kind']=='six-resident')
            self.assertEqual(first['sample_sequences'],[3])
            self.assertIsNone(first['launcher_tree']['observed_accounted_cpu_seconds'])

    def test_incompatible_or_uncovered_clocks_remain_unknown_without_expanding_offset(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,original,marks=phase_fixture(Path(directory))
            for name,mutate,reason in (
                ('offset',lambda c:c['points'][-1].update(perf_before_ns=21_000_000_000,perf_after_ns=21_000_000_001),'clock-offset-intersection-empty'),
                ('frequency',lambda c:c['points'][-1].update(qpc_frequency=999),'clock-frequency-mismatch'),
                ('unsupported',lambda c:c.update(implementation='CLOCK_MONOTONIC'),'clock-implementation-incompatible'),
                ('quantization',lambda c:c.update(resolution_ns=0),'clock-resolution-invalid'),
                ('truncated',lambda c:c['points'].pop(),'clock-bracket-count')):
                with self.subTest(name=name):
                    calibration=copy.deepcopy(original);mutate(calibration)
                    result=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
                    self.assertEqual((result['status'],result['reason']),('unknown',reason))
                    self.assertIsNone(result['windows'])
                    if name=='offset':self.assertGreater(*result['calibration']['offset_bounds_ns'])

    def test_fractional_tick_mapping_rounds_outward_and_exposes_large_uncertainty(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            points=[]
            for batch,values in [('before-go',(1,2,3,4)),('after-cleanup',(90,91,92,93))]:
                # Worked literals for thirds of a second; no implementation formula in oracle.
                times={1:(333333333,333333334),2:(666666666,666666667),3:(1000000000,1000000000),4:(1333333333,1333333334),
                       90:(30000000000,30000000000),91:(30333333333,30333333334),92:(30666666666,30666666667),93:(31000000000,31000000000)}
                for value in values:
                    before,after=times[value]
                    points.append(dict(batch=batch,perf_before_ns=before,perf_after_ns=after,qpc_counter=value,qpc_frequency=3))
            calibration['points']=points
            rows=[json.loads(line) for line in native.read_text().splitlines()]
            for index,row in enumerate(rows):row.update(qpc_counter=4+index,qpc_frequency=3)
            native.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            result=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            self.assertEqual(result['status'],'mapped')
            self.assertEqual(result['calibration']['offset_bounds_ns'],[-1,1])
            self.assertEqual(result['markers'][0]['elapsed_bounds_ns'],[999999998,1666666669])
            self.assertTrue(any(w['reason']=='phase-boundary-uncertainty-overlap' for w in result['windows']))

    def test_marker_contract_rejects_wrong_residency_creation_frequency_and_truncation(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            original=native.read_bytes();rows=[json.loads(line) for line in original.splitlines()]
            cases=[]
            for field,value,reason in [('window_creations',9,'native-marker-contract'),('live_sessions',7,'native-marker-contract'),
                                      ('qpc_frequency',2,'native-frequency-mismatch'),('qpc_counter',1,'native-outside-calibration-coverage')]:
                changed=copy.deepcopy(rows);changed[3][field]=value
                cases.append((field,(''.join(json.dumps(row)+'\n' for row in changed)).encode(),reason))
            cases.extend([('truncated',original[:-1],'native-marker-budget-or-truncation'),
                          ('oversized',b' '*65536+b'\n','native-marker-budget-or-truncation')])
            for name,raw,reason in cases:
                with self.subTest(name=name):
                    native.write_bytes(raw)
                    report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
                    self.assertEqual((report['status'],report['reason']),('unknown',reason))
                    self.assertIsNone(report['windows'])

    def test_cpu_pair_does_not_bridge_unavailable_record_or_pid_reuse(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        from experiments.shell.test_workflow_resources import resource_fixture,write_fixture
        from experiments.shell.workflow_resources import write_resource_observations
        for reused in (False,True):
            with self.subTest(reused=reused),tempfile.TemporaryDirectory() as directory:
                out=Path(directory);observations,native,calibration,marks=phase_fixture(out)
                rows=resource_fixture();rows[3]['elapsed_ns']=1_500_000_000;rows[3]['actual_interval_ns']=500_000_000
                rows[3]['processes'][1]['status']='AccessDenied'
                last=copy.deepcopy(rows[2]);last.update(elapsed_ns=2_000_000_000,actual_interval_ns=500_000_000)
                last['processes'][0].update(cpu_seconds_lifetime=14,cpu_seconds_since_first_observation=4)
                last['processes'][1].update(cpu_seconds_lifetime=8,cpu_seconds_since_first_observation=4)
                last['processes'][2].update(cpu_seconds_lifetime=7,cpu_seconds_since_first_observation=0)
                last['processes'][3].update(cpu_seconds_lifetime=23,cpu_seconds_since_first_observation=3)
                if reused:last['processes'][1].update(create_time=1001.0,cpu_seconds_lifetime=3,cpu_seconds_since_first_observation=0)
                rows.insert(4,last);write_fixture(out/'raw.jsonl',rows);observations.unlink()
                write_resource_observations(out/'raw.jsonl',observations,expected_identity=(41,1000.0))
                report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
                first=next(w for w in report['windows'] if w['kind']=='six-resident')
                self.assertEqual(first['sample_sequences'],[2,3,4])
                self.assertEqual(first['launcher_tree']['observed_accounted_cpu_seconds'],2)
                self.assertIsNone(first['groups']['descendant']['observed_accounted_cpu_seconds'])

    def test_module_import_is_pure_and_published_offset_cannot_be_changed(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        code='import sys; import experiments.shell.workflow_resource_phases, experiments.shell.workflow_resources; assert "psutil" not in sys.modules and "ctypes" not in sys.modules'
        result=subprocess.run([sys.executable,'-c',code],cwd=ROOT,capture_output=True,timeout=5)
        self.assertEqual((result.returncode,result.stderr),(0,b''))
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            published=report['calibration'];published['offset_bounds_ns'][0]+=1
            changed=summarize_resource_phases(observations,native,calibration=published,marks=marks)
            self.assertEqual((changed['status'],changed['reason']),('unknown','clock-derived-evidence-mismatch'))

    def test_published_clock_bounds_keep_integer_evidence_types(self):
        from experiments.shell.workflow_resource_phases import summarize_resource_phases
        with tempfile.TemporaryDirectory() as directory:
            observations,native,calibration,marks=phase_fixture(Path(directory))
            report=summarize_resource_phases(observations,native,calibration=calibration,marks=marks)
            published=report['calibration'];published['offset_bounds_ns'][0]=float(published['offset_bounds_ns'][0])
            changed=summarize_resource_phases(observations,native,calibration=published,marks=marks)
            self.assertEqual((changed['status'],changed['reason']),('unknown','clock-derived-evidence-mismatch'))


@unittest.skipUnless(os.name=='nt','actual Windows QPC/private bytes/owned Job')
class ActualClockMapping(unittest.TestCase):
    def test_owned_go_qpc_markers_map_real_collections_without_assuming_epoch(self):
        from experiments.conpty.run import OwnedJob
        from experiments.shell.workflow_resources import WorkflowResources
        artifact_root=os.environ.get('TERMFORGE_PHASE_ARTIFACTS')
        if artifact_root:
            destination=Path(artifact_root)/('run-'+uuid.uuid4().hex);destination.mkdir(parents=True,exist_ok=False)
            context=nullcontext(str(destination))
        else:context=tempfile.TemporaryDirectory()
        with context as directory:
            out=Path(directory);job=OwnedJob();process=trace=None
            try:
                process=subprocess.Popen([getattr(sys,'_base_executable',sys.executable),str(Path(__file__).resolve()),'--phase-root',str(out)],
                    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
                job.assign(process.pid)
                trace=WorkflowResources(process,out/'_resources-private',seconds=10,interval=.05,discovery_interval=.1)
                trace.start();trace.mark('launcher-go');process.stdin.write(b'GO\n');process.stdin.flush()
                stdout,stderr=process.communicate(timeout=8)
                self.assertEqual((process.returncode,stderr,stdout),(0,b'',b'phase-child-exit0\r\n'))
                self.assertEqual(job.drain()['active'],0);trace.mark('cleanup-finished')
                report=trace.finish(out/'resource-phases.jsonl',observations_path=out/'resource-observations.jsonl')
                phases=report['phase_windows']
                self.assertEqual(phases['status'],'mapped')
                self.assertEqual(len(phases['calibration']['points']),8)
                self.assertEqual(len(phases['windows']),19)
                stable=[w for w in phases['windows'] if w['kind']=='six-resident']
                self.assertEqual([w['cycle'] for w in stable],[1,2,3,4,5])
                self.assertTrue(any(w['sample_count']>=2 for w in stable))
                self.assertEqual(sum(w['kind']=='creation' for w in phases['windows']),5)
                origin=json.loads(trace.trace_path.read_text().splitlines()[0])['start_counter_ns']
                independent=[json.loads(line) for line in (out/'independent-clock.private.jsonl').read_text().splitlines()]
                for measured,mapped in zip(independent,phases['markers']):
                    low,high=mapped['elapsed_bounds_ns']
                    self.assertLessEqual(low,measured['perf_after_ns']-origin)
                    self.assertGreaterEqual(high,measured['perf_before_ns']-origin)
                self.assertTrue(report['sampler_joined'])
                (out/'validation.json').write_text(json.dumps(dict(status='passed',scope='data-only-owned-clock-fixture-not-eight-session-acceptance',
                    phase_summary=phases,job_after=job.accounting(),sampler_joined=True),indent=2)+'\n')
            finally:
                if process is not None and process.poll() is None:
                    job.terminate();process.communicate(timeout=5)
                if trace is not None:trace.finish()
                self.assertEqual(job.drain()['active'],0);job.close()


def phase_child(out):
    # Synthetic marker/session labels exercise the phase protocol; only QPC,
    # perf_counter, allocation and process lifecycle below are actual measures.
    import ctypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    counter=kernel.QueryPerformanceCounter;frequency=kernel.QueryPerformanceFrequency
    counter.argtypes=frequency.argtypes=[ctypes.POINTER(ctypes.c_longlong)]
    counter.restype=frequency.restype=ctypes.c_int
    freq=ctypes.c_longlong();assert frequency(ctypes.byref(freq))
    allocation=bytearray(16*1024*1024)
    sequence=0
    with (out/'resource-phases.jsonl').open('x') as stream,(out/'independent-clock.private.jsonl').open('x') as clocks:
        def marker(phase,cycle,live,windows):
            nonlocal sequence
            before=time.perf_counter_ns();value=ctypes.c_longlong();assert counter(ctypes.byref(value));after=time.perf_counter_ns()
            sequence+=1
            stream.write(json.dumps(dict(schema='termforge-workflow-resource-phase-v1',seq=sequence,phase=phase,cycle=cycle,
                qpc_counter=value.value,qpc_frequency=freq.value,live_sessions=live,window_creations=windows))+'\n');stream.flush()
            clocks.write(json.dumps(dict(perf_before_ns=before,perf_after_ns=after))+'\n');clocks.flush()
        time.sleep(.3);marker('eight-ready',0,8,8)
        time.sleep(.03);marker('load-started',0,8,8)
        time.sleep(.08);marker('background-complete',0,8,8)
        for cycle in range(1,6):
            marker('cycle-six',cycle,6,6+cycle*2)
            deadline=time.perf_counter()+.6
            while time.perf_counter()<deadline:allocation[0]=sum(range(1000))%256
            marker('cycle-six-end',cycle,6,6+cycle*2)
            time.sleep(.01);marker('cycle-eight',cycle,8,8+cycle*2)
        marker('after-paste-close',5,7,18)
    return 0


def phase_root(out):
    if sys.stdin.buffer.readline(4)!=b'GO\n':return 8
    child=subprocess.Popen([getattr(sys,'_base_executable',sys.executable),str(Path(__file__).resolve()),'--phase-child',str(out)],creationflags=subprocess.CREATE_NO_WINDOW)
    result=child.wait(timeout=7)
    if result==0:print('phase-child-exit0',flush=True)
    return result


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1] in ('--phase-root','--phase-child'):
        raise SystemExit((phase_root if sys.argv[1]=='--phase-root' else phase_child)(Path(sys.argv[2])))
    unittest.main()
