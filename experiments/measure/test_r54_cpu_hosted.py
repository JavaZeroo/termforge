"""Synthetic publication and hosted-identity boundaries, never actual hosted runs."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


class CpuHostedBoundaryTests(unittest.TestCase):
    def test_wrong_experiment_or_stale_run_is_rejected_before_publication_staging(self):
        from experiments.measure import r54_publish as publisher
        identity = dict(GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='1', GITHUB_SHA='a' * 40)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            raw = root / 'local/raw'
            raw.mkdir(parents=True)
            output = root / 'local/public'
            for schema, recorded in (('termforge-r54-hosted-v1', identity),
                                     ('termforge-r54-cpu-hosted-v1', dict(identity, GITHUB_RUN_ID='122'))):
                (raw / 'hosted.json').write_text(json.dumps(dict(schema=schema, source='a' * 40,
                    hosted_identity=recorded)), encoding='utf-8')
                with patch.object(publisher, 'ROOT', root), \
                        patch.object(publisher, 'hosted_identity', return_value=identity), self.assertRaises(ValueError):
                    publisher.publish(raw, output, experiment='cpu-boundary')
                self.assertFalse(output.exists())
                self.assertFalse(output.with_name('public.partial').exists())

    def test_non_hosted_entry_preserves_failure_without_building(self):
        root = Path(__file__).resolve().parents[2]
        (root / 'local').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root / 'local') as folder:
            output = Path(folder) / 'raw'
            result = subprocess.run([sys.executable, '-m', 'experiments.measure.r54_cpu_hosted', '--out', str(output)],
                                    cwd=root, env=dict(os.environ, GITHUB_ACTIONS='false'),
                                    capture_output=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            manifest = json.loads((output / 'hosted.json').read_text(encoding='utf-8'))
            self.assertEqual(manifest['status'], 'failed')
            self.assertIn('actual GitHub-hosted Windows job', manifest['error'])
            self.assertFalse((output / 'bundle').exists())
            self.assertFalse((output / 'diagnostic').exists())

    def test_branch_attempt_and_source_cannot_be_mixed(self):
        from experiments.measure import r54_hosted as hosted
        identity = dict(GITHUB_ACTIONS='true', RUNNER_ENVIRONMENT='github-hosted', RUNNER_OS='Windows',
                        GITHUB_REPOSITORY='JavaZeroo/termforge', GITHUB_EVENT_NAME='push',
                        GITHUB_REF='refs/heads/perf/grapheme-cpu-hosted-r54', GITHUB_RUN_ATTEMPT='1',
                        GITHUB_SHA='a' * 40)
        with patch.dict(os.environ, identity, clear=True), patch.object(os, 'name', 'nt'), \
                patch.object(hosted, 'git', return_value='a' * 40):
            self.assertEqual(hosted.hosted_identity('perf/grapheme-cpu-hosted-r54')['GITHUB_SHA'], 'a' * 40)
            for branch in ('perf/grapheme-cpu-hosted-r54-setup2', 'perf/grapheme-cpu-hosted-r54-entry3'):
                with patch.dict(os.environ, GITHUB_REF='refs/heads/' + branch):
                    self.assertEqual(hosted.hosted_identity(hosted.CPU_BOUNDARY_BRANCHES)['GITHUB_SHA'], 'a' * 40)
                    with self.assertRaises(ValueError):
                        hosted.hosted_identity()
            with self.assertRaises(ValueError):
                hosted.hosted_identity()
            for key, value in (('GITHUB_RUN_ATTEMPT', '2'), ('GITHUB_SHA', 'b' * 40)):
                with patch.dict(os.environ, {key: value}), self.assertRaises(ValueError):
                    hosted.hosted_identity('perf/grapheme-cpu-hosted-r54')

    def test_publication_keeps_failed_streams_and_separates_the_six_observation_scope(self):
        from experiments.measure import r54_publish as publisher
        identity = dict(GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='1', GITHUB_SHA='a' * 40)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            raw = root / 'local/raw'
            raw.mkdir(parents=True)
            (raw / 'hosted.json').write_text(json.dumps(dict(schema='termforge-r54-cpu-hosted-v1',
                source='a' * 40, hosted_identity=identity, status='failed')), encoding='utf-8')
            record = raw / 'diagnostic/observations/000/record.json'
            record.parent.mkdir(parents=True)
            record.write_text(json.dumps(dict(child_pid=12345, status='failed',
                child_cpu=dict(total_seconds=2.25), command=[str(root / 'tool.exe')])), encoding='utf-8')
            payload = b'partial\xff\x00\n'
            (record.parent / 'stdout.bin').write_bytes(payload)
            for extra in ('diagnostic/observations/006/stdout.bin', 'batch-20261026/samples/000/stdout.bin',
                          'diagnostic/observations/000/private.json', 'credentials.json'):
                path = raw / extra
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'do-not-publish')
            output = root / 'local/public'
            # Host and short-path queries are OS boundaries. No fixture claims
            # a real hosted identity or CPU measurement.
            with patch.object(publisher, 'ROOT', root), \
                    patch.object(publisher, 'hosted_identity', return_value=identity), \
                    patch.object(publisher, 'windows_short_path', side_effect=lambda path: path):
                publisher.publish(raw, output, experiment='cpu-boundary')
            publication = json.loads((output / 'publication.json').read_text(encoding='utf-8'))
            self.assertEqual(publication['schema'], 'termforge-r54-cpu-hosted-publication-v1')
            self.assertEqual({item['path'] for item in publication['artifacts']},
                {'hosted.json', 'diagnostic/observations/000/record.json', 'diagnostic/observations/000/stdout.bin'})
            published = json.loads((output / 'diagnostic/observations/000/record.json').read_text(encoding='utf-8'))
            self.assertNotIn('child_pid', published)
            self.assertEqual(published['child_cpu']['total_seconds'], 2.25)
            self.assertEqual(published['status'], 'failed')
            self.assertTrue(published['command'][0].startswith('${WORKSPACE}'))
            self.assertEqual((output / 'diagnostic/observations/000/stdout.bin').read_bytes(), payload)


if __name__ == '__main__':
    unittest.main()
