"""One fresh hosted build followed by the separately declared CPU-boundary diagnosis."""
import argparse
import json
from pathlib import Path

from experiments.measure.r54_batch import sha, utc, write_json
from experiments.measure.r54_build import prepare_builds
from experiments.measure.r54_contract import ROOT
from experiments.measure.r54_cpu_diagnostic import execute as diagnose
from experiments.measure.r54_hosted import hosted_identity
from experiments.measure.r54_inputs import git, require

BRANCH = 'perf/grapheme-cpu-hosted-r54'


def execute(output):
    output = Path(output).resolve()
    require(output.is_relative_to(ROOT / 'local'), 'hosted artifacts must stay under the workspace local directory')
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema='termforge-r54-cpu-hosted-v1', status='preparing', started_utc=utc(),
                    source=git(ROOT, 'rev-parse', 'HEAD'), runner_sha256=sha(Path(__file__)),
                    workflow_sha256=sha(ROOT / '.github/workflows/r54-cpu.yml'),
                    performance_gate='not_evaluated', diagnostic=None)
    try:
        manifest['hosted_identity'] = hosted_identity(BRANCH)
        write_json(output / 'hosted.json', manifest)
        prepare_builds(output / 'bundle')
        declaration = dict(host_kind='hosted_vm', no_known_concurrent_build_or_test=True,
            known_concurrent_work=['This job completed setup, tests and both audited builds before diagnosis.',
                                   'GitHub runner service and operating-system background services remain.',
                                   'Physical host scheduling and neighboring virtual machines are unobserved.'],
            purpose='One six-observation CPU-boundary diagnostic; no idle qualification or cost acceptance.')
        write_json(output / 'environment-declaration.json', declaration)
        manifest['status'] = 'diagnosing'
        write_json(output / 'hosted.json', manifest)
        result = diagnose(output / 'bundle/plan-v1.json', output / 'bundle',
                          output / 'environment-declaration.json', output / 'diagnostic')
        manifest['diagnostic'] = result
        require(result['status'] == 'accounting_complete', 'diagnosis stopped; no replacement observations')
        manifest['status'] = 'accounting_complete'
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
