"""Synthetic process/artifact contracts; these tests do not measure core speed."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from experiments.measure.r54_contract import SOURCES, sample_order
from experiments.measure.test_r54_contract import sample


def records(seed):
    baseline = (2, 4, 6, 8, 10, 12, 20)
    candidate = (4, 20, 12, 40, 20, 48, 40)
    result = []
    for item in sample_order(seed):
        value = sample()
        value.update(route=item['route'], chunk=item['chunk'],
                     bytes_per_round=78000 if item['route'] == 'project' else 22000,
                     state_fnv1a64=SOURCES[item['variant']]['states'][item['route']])
        cpu = (baseline if item['variant'] == 'baseline' else candidate)[item['repeat']]
        value.update(interval_cpu_seconds=cpu, interval_wall_seconds=cpu + 1)
        result.append(dict(item=item, result=value, status='valid'))
    return result


class SummaryTests(unittest.TestCase):
    def test_pairs_are_not_ratio_of_independent_medians(self):
        from experiments.measure.r54_batch import summarize
        value = summarize(records(20261026), 20261026)
        self.assertEqual(len(value['groups']), 8)
        self.assertEqual(value['performance_gate'], 'not_evaluated')
        for group in value['groups']:
            self.assertEqual(group['paired_cpu_ratios'], [2, 5, 2, 5, 2, 4, 2])
            self.assertEqual(group['paired_cpu_ratio_median'], 2)
            cpu = group['samples']
            self.assertAlmostEqual(cpu['candidate']['cpu_ns_per_byte']['median'] /
                                   cpu['baseline']['cpu_ns_per_byte']['median'], 2.5)
            self.assertEqual(cpu['baseline']['cpu_ns_per_byte']['count'], 7)

    def test_missing_reordered_failed_or_wrong_identity_never_gets_summary(self):
        from experiments.measure.r54_batch import summarize
        valid = records(20261027)
        broken = []
        broken.append(valid[:-1])
        broken.append([valid[1], valid[0], *valid[2:]])
        for field, value in (('status', 'failed'), ('item', valid[1]['item'])):
            changed = copy.deepcopy(valid)
            changed[0][field] = value
            broken.append(changed)
        wrong_state = copy.deepcopy(valid)
        wrong_state[0]['result']['state_fnv1a64'] = 'bad'
        broken.append(wrong_state)
        for values in broken:
            with self.assertRaises(ValueError):
                summarize(values, 20261027)


class ProcessArtifactsTests(unittest.TestCase):
    def test_interrupted_temporary_file_does_not_block_final_manifest(self):
        from experiments.measure.r54_batch import write_json
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'manifest.json'
            path.write_text('{"status":"running","active_sequence":3}', encoding='utf-8')
            stale = Path(folder) / 'manifest.json.tmp'
            stale.write_bytes(b'{"status":')
            write_json(path, dict(status='interrupted', active_sequence=3))
            self.assertEqual(json.loads(path.read_text()), dict(status='interrupted', active_sequence=3))
            self.assertEqual(stale.read_bytes(), b'{"status":')

    def test_non_utf8_json_is_preserved_and_rejected_before_second_sample(self):
        from experiments.measure.r54_batch import run_schedule
        payload = json.dumps(records(20261026)[0]['result']).encode('utf-16')
        with tempfile.TemporaryDirectory() as folder:
            command = [sys.executable, '-c', 'import os,sys; os.write(1,bytes.fromhex(sys.argv[1]))', payload.hex()]
            output = Path(folder) / 'batch'
            manifest = run_schedule({variant: command for variant in SOURCES}, 20261026, output,
                                    {'evidence_kind': 'synthetic_process_fixture'})
            self.assertEqual(manifest['status'], 'failed')
            self.assertEqual(manifest['attempted'], 1)
            self.assertEqual(manifest['valid_samples'], 0)
            self.assertEqual((output / '000/stdout.bin').read_bytes(), payload)

    def test_timeout_keeps_exact_stream_bytes(self):
        from experiments.measure.r54_batch import capture
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            code = "import os,time; os.write(1,b'partial\\xff'); os.write(2,b'error\\xfe'); time.sleep(10)"
            value = capture([sys.executable, '-c', code], output, timeout=1)
            self.assertTrue(value['timed_out'])
            self.assertEqual((output / 'stdout.bin').read_bytes(), b'partial\xff')
            self.assertEqual((output / 'stderr.bin').read_bytes(), b'error\xfe')

    def test_failure_preserves_attempt_and_stops_without_overwriting(self):
        from experiments.measure.r54_batch import run_schedule
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fixture = root / 'fixture.py'
            fixture.write_text("import os; os.write(1,b'invalid\\xff'); os.write(2,b'diagnostic\\xfe')", encoding='utf-8')
            commands = {variant: [sys.executable, str(fixture)] for variant in SOURCES}
            output = root / 'batch'
            manifest = run_schedule(commands, 20261026, output, {'evidence_kind': 'synthetic_process_fixture'})
            self.assertEqual(manifest['status'], 'failed')
            self.assertEqual(manifest['attempted'], 1)
            self.assertEqual(manifest['valid_samples'], 0)
            self.assertFalse((output / 'summary.json').exists())
            self.assertEqual((output / '000/stdout.bin').read_bytes(), b'invalid\xff')
            self.assertEqual((output / '000/stderr.bin').read_bytes(), b'diagnostic\xfe')
            original = (output / 'manifest.json').read_bytes()
            with self.assertRaises(FileExistsError):
                run_schedule(commands, 20261026, output, {})
            self.assertEqual((output / 'manifest.json').read_bytes(), original)

    def test_complete_real_subprocess_sequence_preserves_all_pairs(self):
        from experiments.measure.r54_batch import run_schedule
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            # Independent literal fake results exercise process ordering and artifact
            # handling. They are labelled synthetic and are never timing evidence.
            (root / 'values.json').write_text(json.dumps(records(20261027)), encoding='utf-8')
            fixture = root / 'fixture.py'
            fixture.write_text('''import json,pathlib,sys
p=pathlib.Path(__file__).parent
count=p/'count'
i=int(count.read_text()) if count.exists() else 0
record=json.loads((p/'values.json').read_text())[i]
item=record['item']
assert sys.argv[1:]==[item['variant'],item['route'],str(item['chunk']),'0','2.0']
count.write_text(str(i+1))
print(json.dumps(record['result']))
''', encoding='utf-8')
            commands = {variant: [sys.executable, str(fixture), variant] for variant in SOURCES}
            output = root / 'batch'
            manifest = run_schedule(commands, 20261027, output, {'evidence_kind': 'synthetic_process_fixture'})
            self.assertEqual(manifest['status'], 'samples_complete')
            self.assertEqual(manifest['valid_samples'], 112)
            self.assertEqual(int((root / 'count').read_text()), 112)
            rows = [json.loads(line) for line in (output / 'samples.jsonl').read_text().splitlines()]
            self.assertEqual([row['item'] for row in rows], sample_order(20261027))
            self.assertEqual(len(list(output.glob('*/stdout.bin'))), 112)
            self.assertEqual(json.loads((output / 'summary.json').read_text())['groups'][0]['paired_cpu_ratio_median'], 2)


class EnvironmentTests(unittest.TestCase):
    def test_pure_environment_statistics_import_without_site_packages(self):
        command = [sys.executable, '-S', '-c',
                   'from experiments.measure.r54_environment import cpu_delta, qualify_idle; print("imported")']
        result = subprocess.run(command, cwd=Path(__file__).resolve().parents[2],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
        self.assertEqual(result.stdout.strip(), b'imported')

    def test_environment_qualification_does_not_hide_one_busy_interval(self):
        from experiments.measure.r54_environment import qualify_idle, cpu_delta
        # Windows user + system + idle count all logical CPUs. interrupt/dpc
        # are already included in system and must not be added again.
        before = dict(user=10, system=20, idle=70, interrupt=2, dpc=3)
        after = dict(user=11, system=21, idle=118, interrupt=2.5, dpc=3.5)
        observed = cpu_delta(before, after)
        self.assertEqual(observed['busy_seconds'], 2)
        self.assertEqual(observed['total_seconds'], 50)
        self.assertEqual(observed['busy_percent'], 4)
        self.assertTrue(qualify_idle([observed] * 10))
        self.assertFalse(qualify_idle([observed] * 9))
        self.assertFalse(qualify_idle([observed] * 9 + [dict(observed, busy_percent=6)]))
        with self.assertRaises(ValueError):
            cpu_delta(after, before)


class FrozenInputTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'the execution entry point binds Windows builds')
    def test_failed_preparation_still_identifies_the_runner(self):
        from experiments.measure.r54_batch import run
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'failed'
            manifest = run(Path(folder) / 'absent-plan', folder, Path(folder) / 'absent-environment', output, 20261026)
            self.assertEqual(manifest['status'], 'failed')
            self.assertEqual(manifest['attempted'], 0)
            identity = json.loads((output / 'runner.json').read_text())
            self.assertEqual(len(identity['commit']), 40)
            self.assertIn('experiments/measure/r54_batch.py', identity['sources'])
            self.assertIn('worktree_status', identity)

    def test_changed_schedule_harness_or_result_injection_is_rejected(self):
        from experiments.measure.r54_contract import prepare
        from experiments.measure.r54_inputs import load_plan
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'plan.json'
            original = prepare(path)
            self.assertEqual(load_plan(path), original)
            changes = []
            changed = copy.deepcopy(original)
            changed['protocol']['schedules'][0]['samples'].reverse()
            changes.append(changed)
            changed = copy.deepcopy(original)
            changed['harness_sha256'].pop('experiments/measure/run.py')
            changes.append(changed)
            changed = copy.deepcopy(original)
            changed['environment_evidence'] = {'idle': True}
            changes.append(changed)
            for changed in changes:
                path.write_text(json.dumps(changed), encoding='utf-8')
                with self.assertRaises(ValueError):
                    load_plan(path)


if __name__ == '__main__':
    unittest.main()
