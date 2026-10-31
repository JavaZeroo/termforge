"""Build input and publication boundaries; no performance workloads in tests."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BuildBoundaryTests(unittest.TestCase):
    def test_unreviewed_compiler_override_stops_before_source_checkout(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'bundle'
            result = subprocess.run(
                [sys.executable, '-m', 'experiments.measure.r54_build', '--out', str(output)],
                cwd=ROOT, env=dict(os.environ, RUSTFLAGS='-C opt-level=0'),
                capture_output=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            manifest = json.loads((output / 'preparation.json').read_text(encoding='utf-8'))
            self.assertEqual(manifest['status'], 'failed')
            self.assertIn('RUSTFLAGS', manifest['error'])
            self.assertFalse((output / 'source-baseline').exists())
            self.assertFalse((output / 'plan-v1.json').exists())


class PublicationBoundaryTests(unittest.TestCase):
    def test_public_logs_redact_long_short_escaped_and_case_variants_without_changing_other_bytes(self):
        from experiments.measure.r54_publish import replacement_forms, redact_paths
        forms = replacement_forms([(r'C:\Users\runneradmin', '${PROFILE}'),
                                   (r'C:\Users\RUNNER~1', '${PROFILE}')])
        original = (b'C:\\Users\\runneradmin\\Temp\\a C:\\\\Users\\\\RUNNER~1\\\\Temp\\\\b '
                    b'c:/users/runner~1/Temp/c\xff\n')
        self.assertEqual(redact_paths(original, forms),
                         b'${PROFILE}\\Temp\\a ${PROFILE}\\\\Temp\\\\b ${PROFILE}/Temp/c\xff\n')

    def test_interrupted_jsonl_tail_remains_visible_without_losing_complete_samples(self):
        from experiments.measure.r54_publish import publish_jsonl
        original = b'{"command":["/ci/work/tool.exe"],"cpu":2.0}\n{"command":['
        data, status = publish_jsonl(original, [('/ci/work', '${WORKSPACE}')])
        self.assertEqual(data, b'{"command":["${WORKSPACE}/tool.exe"],"cpu":2.0}\n{"command":[')
        self.assertFalse(status['complete'])
        self.assertIn('JSONDecodeError', status['error'])
        self.assertEqual(status['error_line'], 2)

    def test_missing_or_different_raw_run_identity_is_rejected(self):
        from experiments.measure.r54_publish import validate_raw_identity
        identity = dict(GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='1', GITHUB_SHA='a' * 40)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(FileNotFoundError):
                validate_raw_identity(root, identity)
            path = root / 'hosted.json'
            path.write_text(json.dumps(dict(schema='termforge-r54-hosted-v1', source='a' * 40,
                                            hosted_identity=identity)), encoding='utf-8')
            validate_raw_identity(root, identity)
            with self.assertRaises(ValueError):
                validate_raw_identity(root, dict(identity, GITHUB_RUN_ID='124'))

    def test_export_inventory_does_not_include_unrelated_or_arbitrary_files(self):
        from experiments.measure.r54_publish import selected_paths
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            files = ['bundle/plan-v1.json', 'bundle/credentials.json',
                     'batch-20261026/samples/000/stdout.bin',
                     'batch-20261026/samples/000/unrelated.txt',
                     'batch-20261026/samples/999/stdout.bin', 'personal-session.log']
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
            self.assertEqual({path.as_posix() for path in selected_paths(root)},
                             {'bundle/plan-v1.json', 'batch-20261026/samples/000/stdout.bin'})

    def test_environment_export_keeps_cpu_evidence_and_omits_process_identities(self):
        from experiments.measure.r54_publish import public_environment
        value = dict(status='rejected_busy_host', host=dict(platform='Windows', cpu_logical=4),
                     intervals=[dict(busy_percent=7.5, total_seconds=4, busy_seconds=.3)],
                     processes_before=[dict(pid=123, name='private-name', cpu_times=dict(user=2))],
                     processes_after=[dict(pid=123, name='private-name', cpu_times=dict(user=3))],
                     unexpected_private_field='do-not-publish')
        result = public_environment(value)
        self.assertEqual(result['intervals'], value['intervals'])
        self.assertEqual(result['host']['cpu_logical'], 4)
        self.assertEqual(result['status'], 'rejected_busy_host')
        self.assertNotIn('private-name', json.dumps(result))
        self.assertNotIn('do-not-publish', json.dumps(result))
        self.assertNotIn('processes_before', result)



if __name__ == '__main__':
    unittest.main()
