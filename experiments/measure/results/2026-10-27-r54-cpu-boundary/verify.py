"""Recheck retained synthetic evidence without extracting files or launching a cost run."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from experiments.measure.r54_contract import validate_sample


def verify():
    path = Path(__file__).with_name('evidence.zip')
    assert hashlib.sha256(path.read_bytes()).hexdigest() == '9bec0731e08d7f2ef99d140a9e56acbc01468814cc68fb01fbdcfe30eaa8e10c'
    with zipfile.ZipFile(path) as archive:
        read = lambda name: json.loads(archive.read(name))
        inventory = read('publication.json')['files']
        assert len(archive.namelist()) == 47
        assert len({row['path'] for row in inventory}) == 46
        assert set(archive.namelist()) == {row['path'] for row in inventory} | {'publication.json'}
        for row in inventory:
            data = archive.read(row['path'])
            assert len(data) == row['published_bytes']
            assert hashlib.sha256(data).hexdigest() == row['published_sha256']
            if row['treatment'] == 'byte_exact_owned_synthetic_stream':
                assert row['published_sha256'] == row['original_sha256']
        assert read('manifest.json')['status'] == 'accounting_complete'
        manifest = read('observations/manifest.json')
        assert manifest['attempted'] == manifest['accounted_observations'] == 6
        assert manifest['performance_gate'] == 'not_evaluated'
        builds = read('binding.json')['builds']
        prior = json.loads((path.parent.parent / '2026-10-27-r54-runner/validation.json').read_text(encoding='utf-8'))
        for variant in ('baseline', 'candidate'):
            assert builds[variant]['binary'] == prior['builds'][variant]['binary']
            assert builds[variant]['source']['commit'] == prior['builds'][variant]['source']
        for index, variant in enumerate(('baseline', 'candidate', 'candidate', 'baseline', 'baseline', 'candidate')):
            prefix = f'observations/{index:03d}/'
            row = read(prefix + 'record.json')
            assert row['item'] == dict(sequence=index, variant=variant, route='interactive', chunk=1)
            assert row['status'] == 'accounted' and row['exit_code'] == 0 and not row['timed_out']
            assert 'child_pid' not in row
            for name in ('stdout.bin', 'stderr.bin'):
                data = archive.read(prefix + name)
                assert len(data) == row['streams'][name]['bytes']
                assert hashlib.sha256(data).hexdigest() == row['streams'][name]['sha256']
            raw = read(prefix + 'stdout.bin')
            assert raw == row['result']
            validate_sample(raw, row['item'])
            env, child = row['environment'], row['child_cpu']
            before, after = env['before'], env['after']
            busy = after['user'] + after['system'] - before['user'] - before['system']
            total = busy + after['idle'] - before['idle']
            assert abs(busy - env['busy_seconds']) < 1e-8
            assert abs(total - env['total_seconds']) < 1e-8
            assert child['total_seconds'] == child['kernel_seconds'] + child['user_seconds']
            assert child['exit_filetime'] > child['creation_filetime']
            runner = row['runner_cpu_after_seconds'] - row['runner_cpu_before_seconds']
            assert runner == row['runner_cpu_seconds']
            accounted = row['accounting']
            inner = raw['interval_cpu_seconds']
            assert child['total_seconds'] >= inner
            assert accounted['child_outside_inner_seconds'] == child['total_seconds'] - inner
            assert accounted['inner_only_balance_seconds'] == busy - inner
            assert accounted['runner_cpu_seconds'] == runner
            assert accounted['unassigned_balance_seconds'] == busy - child['total_seconds'] - runner
            assert abs(accounted['unassigned_balance_percent'] - accounted['unassigned_balance_seconds'] * 100 / total) < 1e-8
    return dict(files=47, hashed_evidence=46, byte_exact_streams=16, observations=6,
                oracles_passed=6, accepted_cost_samples=0, performance_gate='not_evaluated')


if __name__ == '__main__':
    print(json.dumps(verify()))
