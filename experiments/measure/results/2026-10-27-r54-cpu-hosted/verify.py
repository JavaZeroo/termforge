"""Read retained evidence only; never build or launch a measurement."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from experiments.measure.r54_contract import SOURCES, canonical, expected_sample, protocol, validate_sample
from experiments.measure.r54_inputs import IDENTITY, OVERLAYS
from experiments.measure.run import validate_result

SOURCE = '4772f4f937c4f0f220be1630592f84bed3a591c8'
RUN = '40512310499'
ORDER = ('baseline', 'candidate', 'candidate', 'baseline', 'baseline', 'candidate')

def digest(data):
    return hashlib.sha256(data).hexdigest()

def source_file_matches(commit, name, sha):
    data = subprocess.check_output(['git','show',commit+':'+name],cwd=ROOT)
    if digest(data) == sha:
        return 'git_blob_bytes'
    # Windows checkout can convert text without explicit eol attributes to CRLF.
    # Require the complete captured SHA to match that exact conversion.
    assert b'\0' not in data and b'\r\n' not in data, name
    assert digest(data.replace(b'\n', b'\r\n')) == sha, name
    return 'git_blob_lf_to_crlf'

def main(path):
    assert path.stat().st_size == 123388
    assert digest(path.read_bytes()) == '263a3bf8e229b89f1e443b12e522daf69d30de403affa0dc49fe674c0dd8f241'
    with zipfile.ZipFile(path) as archive:
        read = lambda name: json.loads(archive.read(name))
        publication = read('publication.json')
        assert publication['schema'] == 'termforge-r54-cpu-hosted-publication-v1'
        inventory = {v['path']: v for v in publication['artifacts']}
        assert len(inventory) == len(publication['artifacts']) == 74
        assert len(archive.namelist()) == 75
        assert set(archive.namelist()) == set(inventory) | {'publication.json'}
        exact = []
        for name, row in inventory.items():
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            data = archive.read(name)
            assert len(data) == row['published_bytes'] and digest(data) == row['published_sha256'], name
            assert not re.search(rb'[A-Za-z]:[\\/]+(?:Users|a)[\\/]', data, re.I), name
            assert b'child_pid' not in data and b'process_inventory' not in data, name
            if row['treatment'] == 'byte_exact_owned_synthetic_stream':
                assert row['original_bytes'] == len(data) and row['original_sha256'] == digest(data), name
                exact.append(name)
        assert len(exact) == 24
        original = lambda name: inventory[name]['original_sha256']
        identity = publication['hosted_identity']
        assert identity['GITHUB_SHA'] == SOURCE and identity['GITHUB_RUN_ID'] == RUN
        assert identity['GITHUB_RUN_ATTEMPT'] == '1' and identity['GITHUB_EVENT_NAME'] == 'push'
        assert identity['GITHUB_REF'] == 'refs/heads/perf/grapheme-cpu-hosted-r54-entry3'
        assert identity['GITHUB_REPOSITORY'] == 'JavaZeroo/termforge'
        assert identity['RUNNER_ENVIRONMENT'] == 'github-hosted' and identity['RUNNER_OS'] == 'Windows'
        hosted = read('hosted.json')
        assert hosted['source'] == SOURCE and hosted['hosted_identity'] == identity
        assert hosted['status'] == 'accounting_complete' and hosted['performance_gate'] == 'not_evaluated'
        for key, name in (('runner_sha256','experiments/measure/r54_cpu_hosted.py'),
                          ('workflow_sha256','.github/workflows/r54-cpu.yml')):
            assert digest(subprocess.check_output(['git','show',SOURCE+':'+name],cwd=ROOT)) == hosted[key]
        runner = read('diagnostic/runner.json')
        assert runner['commit'] == SOURCE and runner['worktree_status'] == ''
        for name, sha in runner['sources'].items():
            assert digest(subprocess.check_output(['git','show',SOURCE+':'+name],cwd=ROOT)) == sha, name
        preparation = read('bundle/preparation.json')
        assert preparation['status'] == 'built_and_audited' and preparation['source'] == SOURCE
        plan, builds = read('bundle/plan-v1.json'), read('bundle/build-plan.json')
        assert plan['protocol'] == protocol()
        assert plan['protocol_sha256'] == digest(canonical(protocol()))
        assert builds['harness_source'] == SOURCE and builds['frozen_plan']['sha256'] == original('bundle/plan-v1.json')
        assert builds['protocol_sha256'] == plan['protocol_sha256']
        checkout_forms = {name: source_file_matches(SOURCE, name, sha) for name, sha in plan['harness_sha256'].items()}
        binding, audit = read('diagnostic/binding.json'), read('bundle/build-audit.json')
        assert audit['passed'] is True and audit['formal_samples'] == 0
        assert binding['plan_sha256'] == original('bundle/plan-v1.json')
        assert binding['harness_source'] == SOURCE and binding['protocol_sha256'] == plan['protocol_sha256']
        for name, sha in binding['evidence'].items():
            assert original('bundle/'+name) == sha, name
        for variant, source in SOURCES.items():
            spec, observed = builds['builds'][variant], audit['builds'][variant]
            receipt = read('bundle/'+variant+'-build.json')
            assert spec['commit'] == observed['source'] == source['commit']
            assert spec['version'] == observed['version'] == source['version']
            assert len(spec['before_files']) == len(spec['after_files']) == (80 if variant == 'baseline' else 84)
            assert set(spec['before_files']) == set(spec['after_files'])
            assert set(spec['changed_paths']) == set(spec['overlay_files']) == set(OVERLAYS)
            for name, before in spec['before_files'].items():
                checkout_forms[variant+'/'+name] = source_file_matches(source['commit'], name, before)
                expected = plan['harness_sha256'][name] if name in OVERLAYS else before
                assert spec['after_files'][name] == expected, name
            assert spec['overlay_patch_sha256'] == original('bundle/'+variant+'-harness.patch')
            assert receipt['status'] == 'built_identity_only' and receipt['exit_code'] == 0 and not receipt['timed_out']
            assert receipt['build_plan_sha256'] == original('bundle/build-plan.json')
            assert observed['receipt_sha256'] == original('bundle/'+variant+'-build.json')
            assert receipt['binary'] == observed['binary'] == binding['builds'][variant]['binary']
            assert receipt['identity'] == observed['identity'] == read('bundle/'+variant+'-identity.stdout') == IDENTITY
            assert read('diagnostic/'+variant+'-identity/stdout.bin') == IDENTITY
            assert read('diagnostic/'+variant+'-identity/record.json')['exit_code'] == 0
            assert receipt['command'] == spec['command']
            for flag in ('--release','--locked','--offline','--no-default-features','--jobs','1','x86_64-pc-windows-msvc'):
                assert flag in spec['command']
            for name, log in receipt['logs'].items():
                assert log['sha256'] == original('bundle/'+name)
                assert log['bytes'] == inventory['bundle/'+name]['original_bytes']
        assert audit['builds']['baseline']['linker'] == audit['builds']['candidate']['linker']
        assert len(audit['short_sanity_calls']) == 4
        for call in audit['short_sanity_calls']:
            assert call['passed'] and call['exit_code'] == 0 and not call['timed_out']
            prefix = 'bundle/sanity-'+call['variant']+'-'+call['route']+'/'
            for name, stream in call['streams'].items():
                data = archive.read(prefix+name)
                assert digest(data) == stream['sha256'] and len(data) == stream['bytes']
            validate_result(read(prefix+'stdout.bin'), .1, expected_sample(dict(variant=call['variant'], route=call['route'], chunk=4096)))
        manifest = read('diagnostic/manifest.json')
        observations = read('diagnostic/observations/manifest.json')
        assert hosted['diagnostic'] == manifest
        assert manifest['status'] == observations['status'] == 'accounting_complete'
        assert manifest['observations'] == observations
        assert observations['attempted'] == observations['accounted_observations'] == 6
        assert observations['performance_gate'] == 'not_evaluated'
        for key, name in (('context_sha256','context.json'), ('protocol_sha256','protocol.json')):
            assert observations[key] == original('diagnostic/observations/'+name)
        rows = []
        for index, variant in enumerate(ORDER):
            prefix = f'diagnostic/observations/{index:03d}/'
            row = read(prefix+'record.json')
            assert row['item'] == dict(sequence=index, variant=variant, route='interactive', chunk=1)
            assert row['status'] == 'accounted' and row['exit_code'] == 0 and not row['timed_out']
            for name, stream in row['streams'].items():
                data = archive.read(prefix+name)
                assert digest(data) == stream['sha256'] and len(data) == stream['bytes']
            raw = read(prefix+'stdout.bin')
            assert raw == row['result']
            validate_sample(raw,row['item'])
            env, child, accounting = row['environment'], row['child_cpu'], row['accounting']
            before, after = env['before'], env['after']
            busy = after['user']+after['system']-before['user']-before['system']
            total = busy+after['idle']-before['idle']
            assert abs(busy-env['busy_seconds']) < 1e-8 and abs(total-env['total_seconds']) < 1e-8
            assert child['total_seconds'] == child['kernel_seconds']+child['user_seconds']
            assert child['exit_filetime'] > child['creation_filetime']
            inner, runner_cpu = raw['interval_cpu_seconds'], row['runner_cpu_after_seconds']-row['runner_cpu_before_seconds']
            assert runner_cpu == row['runner_cpu_seconds'] == accounting['runner_cpu_seconds']
            assert child['total_seconds'] >= inner
            assert accounting['child_outside_inner_seconds'] == child['total_seconds']-inner
            assert accounting['inner_only_balance_seconds'] == busy-inner
            assert abs(accounting['inner_only_balance_percent']-(busy-inner)*100/total) < 1e-8
            assert accounting['unassigned_balance_seconds'] == busy-child['total_seconds']-runner_cpu
            assert abs(accounting['unassigned_balance_percent']-accounting['unassigned_balance_seconds']*100/total) < 1e-8
            rows.append(dict(item=row['item'],inner_cpu_seconds=inner,child_cpu=child,runner_cpu_seconds=runner_cpu,
                             environment=env,accounting=accounting,state_fnv1a64=raw['state_fnv1a64']))
        result = dict(schema='termforge-r54-cpu-hosted-validation-v1',source=SOURCE,hosted_identity=identity,
                      host=read('diagnostic/environment.json'),builds=audit['builds'],observations=rows,
                      archive=dict(bytes=path.stat().st_size,sha256=digest(path.read_bytes()),files=75,inventory_files=74,byte_exact_streams=24),
                      source_checkout_forms=checkout_forms,sanity_oracles=4,observation_oracles=6,formal_cost_samples=0,
                      performance_gate='not_evaluated',old_hosted_event_cause='unknown')
        retained = json.loads(path.with_name('validation.json').read_text(encoding='utf-8'))
        for name, value in result.items():
            assert retained[name] == value, name
        provider = retained['provider']
        assert provider['run'] == int(RUN) and provider['attempt'] == 1 and provider['source'] == SOURCE
        assert provider['job'] == 122217071656 and provider['conclusion'] == 'success'
        assert provider['artifact_digest'] == 'sha256:'+digest(path.read_bytes())
        assert sum(value == 'git_blob_lf_to_crlf' for value in checkout_forms.values()) == 115
        assert len(checkout_forms) == 168
        print(json.dumps(dict(files=75,hashed_evidence=74,byte_exact_streams=24,sanity_oracles=4,observation_oracles=6,
                              formal_cost_samples=0,performance_gate='not_evaluated')))

if __name__ == '__main__':
    main(Path(__file__).with_name('evidence.zip'))
