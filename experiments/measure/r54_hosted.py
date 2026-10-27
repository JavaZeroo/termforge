"""One owned GitHub-hosted Windows build followed by the original R54 batches."""
import argparse
import json
import os
from pathlib import Path

from experiments.measure.r54_batch import run, sha, utc, write_json
from experiments.measure.r54_build import prepare_builds
from experiments.measure.r54_contract import ROOT, SEEDS
from experiments.measure.r54_inputs import git, require


def hosted_identity():
    names = ('GITHUB_ACTIONS', 'RUNNER_ENVIRONMENT', 'RUNNER_OS', 'GITHUB_REPOSITORY',
             'GITHUB_SHA', 'GITHUB_REF', 'GITHUB_RUN_ID', 'GITHUB_RUN_NUMBER', 'GITHUB_RUN_ATTEMPT',
             'GITHUB_EVENT_NAME', 'ImageOS', 'ImageVersion')
    identity = {name: os.environ.get(name) for name in names}
    require(identity['GITHUB_ACTIONS'] == 'true' and identity['RUNNER_ENVIRONMENT'] == 'github-hosted' and
            identity['RUNNER_OS'] == 'Windows' and os.name == 'nt', 'requires an actual GitHub-hosted Windows job')
    require(identity['GITHUB_REPOSITORY'] == 'JavaZeroo/termforge' and identity['GITHUB_EVENT_NAME'] == 'push',
            'unexpected repository or event')
    require(identity['GITHUB_REF'] == 'refs/heads/perf/grapheme-hosted-r54', 'unexpected experiment branch')
    require(identity['GITHUB_RUN_ATTEMPT'] == '1', 'a workflow rerun must not replace the first attempt')
    require(identity['GITHUB_SHA'] == git(ROOT, 'rev-parse', 'HEAD'), 'workflow/source identity mismatch')
    return identity


def execute(output):
    output = Path(output).resolve()
    require(output.is_relative_to(ROOT / 'local'), 'hosted artifacts must stay under the workspace local directory')
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema='termforge-r54-hosted-v1', status='preparing', started_utc=utc(),
                    source=git(ROOT, 'rev-parse', 'HEAD'), runner_sha256=sha(Path(__file__)),
                    performance_gate='not_evaluated', batches=[])
    try:
        manifest['hosted_identity'] = hosted_identity()
        write_json(output / 'hosted.json', manifest)
        prepare_builds(output / 'bundle')
        declaration = dict(host_kind='hosted_vm', no_known_concurrent_build_or_test=True,
            known_concurrent_work=['This job completed setup, tests and both builds before measurement.',
                                   'GitHub runner service and operating-system background services remain.',
                                   'Physical host scheduling and neighboring virtual machines are unobserved.'])
        write_json(output / 'environment-declaration.json', declaration)
        for seed in SEEDS:
            manifest['status'] = 'running'
            manifest['active_seed'] = seed
            write_json(output / 'hosted.json', manifest)
            result = run(output / 'bundle/plan-v1.json', output / 'bundle', output / 'environment-declaration.json',
                         output / f'batch-{seed}', seed)
            manifest['batches'].append(result)
            require(result['status'] == 'complete_diagnostic', f'batch {seed} stopped; remaining seeds not launched')
        manifest.update(status='complete_diagnostic', active_seed=None)
    except BaseException as error:
        manifest.update(status='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        manifest['finished_utc'] = utc()
        write_json(output / 'hosted.json', manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(execute(args.out)))


if __name__ == '__main__':
    main()
