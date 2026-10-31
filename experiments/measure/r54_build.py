"""Create the audited R54 Windows build bundle before any cost measurements."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from experiments.measure.r54_batch import capture, sha, utc, write_json
from experiments.measure.r54_contract import ROOT, SOURCES, expected_sample, prepare
from experiments.measure.r54_inputs import IDENTITY, OVERLAYS, git, load_builds, require
from experiments.measure.run import validate_result

OVERRIDES = ('RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS', 'CARGO_BUILD_TARGET', 'CARGO_BUILD_RUSTFLAGS',
             'CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS', 'CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER',
             'RUSTC', 'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER')
RECORDED_ENVIRONMENT = OVERRIDES + ('CARGO_HOME', 'RUSTUP_HOME', 'RUSTUP_TOOLCHAIN', 'CARGO_TERM_COLOR', 'LIB', 'INCLUDE')


def run_logged(command, directory, prefix, *, cwd=ROOT, environment=None, timeout=300):
    """Build/setup logs are byte-exact; failure prevents entering the cost runner."""
    result = dict(command=command, started_utc=utc(), exit_code=None, timed_out=False)
    stdout, stderr = directory / (prefix + '.stdout'), directory / (prefix + '.stderr')
    try:
        with stdout.open('xb') as output, stderr.open('xb') as error:
            try:
                process = subprocess.run(command, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
                                         stdout=output, stderr=error, timeout=timeout)
                result['exit_code'] = process.returncode
            except subprocess.TimeoutExpired:
                result.update(timed_out=True, error='build/setup direct-child timeout; no process-tree cleanup claim')
        require(result['exit_code'] == 0, f'{prefix} failed; preserve logs, no automatic retry')
    except BaseException as error:
        result['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        result['finished_utc'] = utc()
        result['logs'] = {path.name: dict(bytes=path.stat().st_size, sha256=sha(path))
                          for path in (stdout, stderr) if path.exists()}
        write_json(directory / (prefix + '.process.json'), result)
    return result


def reject_hidden_configuration(checkout, environment):
    # Cargo searches the working directory's ancestors and CARGO_HOME. A clean,
    # explicit cache prevents inherited config from changing the fixed flags.
    cargo_home = environment.get('CARGO_HOME')
    require(bool(cargo_home), 'CARGO_HOME must name an explicit task-owned cache')
    locations = [checkout, *checkout.parents, Path(cargo_home)]
    for location in locations:
        config_dir = location if location == Path(cargo_home) else location / '.cargo'
        require(not any((config_dir / name).exists() for name in ('config', 'config.toml')),
                'unreviewed Cargo configuration file')


def prepare_builds(output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    status = dict(schema='termforge-r54-build-preparation-v1', status='preparing', started_utc=utc(),
                  source=git(ROOT, 'rev-parse', 'HEAD'), runner_sha256=sha(Path(__file__)),
                  formal_cost_samples=0, performance_gate='not_evaluated')
    write_json(output / 'preparation.json', status)
    try:
        environment = {name: os.environ.get(name) for name in RECORDED_ENVIRONMENT}
        for name in OVERRIDES:
            require(environment[name] in (None, ''), f'unreviewed compiler override: {name}')
        for name, value in os.environ.items():
            if value and name.startswith(('CARGO_', 'RUST')) and name not in RECORDED_ENVIRONMENT:
                raise ValueError(f'unreviewed compiler environment name: {name}')
        require(os.name == 'nt', 'the frozen build pair targets Windows x64')
        require(not git(ROOT, 'status', '--porcelain'), 'prepare from a committed, clean source tree')
        frozen = prepare(output / 'plan-v1.json')
        builds = {}
        for variant, source in SOURCES.items():
            checkout = output / ('source-' + variant)
            run_logged(['git', 'worktree', 'add', '--detach', str(checkout), source['commit']],
                       output, variant + '-checkout', timeout=60)
            require(git(checkout, 'rev-parse', 'HEAD') == source['commit'] and
                    not git(checkout, 'status', '--porcelain'), 'checkout identity or cleanliness mismatch')
            reject_hidden_configuration(checkout, environment)
            files = git(checkout, 'ls-files', 'experiments/candidates/alacritty').splitlines()
            before = {name: sha(checkout / name) for name in files}
            for name in OVERLAYS:
                (checkout / name).write_bytes((ROOT / name).read_bytes())
            after = {name: sha(checkout / name) for name in files}
            changed = git(checkout, 'diff', '--name-only').splitlines()
            require(set(changed) == set(OVERLAYS), 'unexpected source changes during overlay')
            require(all(before[name] == after[name] for name in files if name not in OVERLAYS),
                    'original lockfile, adapter or vendor changed')
            patch = output / (variant + '-harness.patch')
            patch.write_bytes(subprocess.check_output(['git', 'diff', '--', *OVERLAYS], cwd=checkout, timeout=30))
            target = output / ('target-' + variant + '-unwrapped')
            require(not target.exists(), 'target directory already exists')
            command = ['cargo', '+1.92.0', 'build', '--manifest-path',
                       str(checkout / 'experiments/candidates/alacritty/Cargo.toml'),
                       '--release', '--locked', '--offline', '--no-default-features', '--jobs', '1',
                       '--bin', 'alacritty-measure', '--target', 'x86_64-pc-windows-msvc',
                       '--target-dir', str(target), '-vv']
            builds[variant] = dict(commit=source['commit'], tree=git(checkout, 'rev-parse', 'HEAD^{tree}'),
                                   candidate_tree=git(checkout, 'rev-parse', 'HEAD:experiments/candidates/alacritty'),
                                   version=source['version'], overlay_files=list(OVERLAYS), changed_paths=changed,
                                   before_files=before, after_files=after, overlay_patch_sha256=sha(patch),
                                   command=command, target_dir=str(target), status='not_built')
        plan = dict(schema='termforge-r54-local-build-plan-v1', harness_source=status['source'],
                    protocol_sha256=frozen['protocol_sha256'], frozen_plan=dict(sha256=sha(output / 'plan-v1.json')),
                    rustc_vv=subprocess.check_output(['rustc', '+1.92.0', '-Vv'], timeout=30).decode('utf-8'),
                    cargo_vv=subprocess.check_output(['cargo', '+1.92.0', '-Vv'], timeout=30).decode('utf-8'),
                    environment=environment, additional_build_environment={'RUSTC_LOG': 'rustc_codegen_ssa::back::link=info'},
                    builds=builds, formal_samples=0, performance_gate='not_evaluated')
        write_json(output / 'build-plan.json', plan)
        for variant, spec in builds.items():
            checkout = output / ('source-' + variant)
            run_logged(['cargo', '+1.92.0', 'fetch', '--locked', '--manifest-path',
                        str(checkout / 'experiments/candidates/alacritty/Cargo.toml'),
                        '--target', 'x86_64-pc-windows-msvc'], output, variant + '-fetch', cwd=checkout)
            receipt = dict(variant=variant, build_plan_sha256=sha(output / 'build-plan.json'),
                           command=spec['command'], status='running', formal_samples=0, performance_gate='not_evaluated')
            write_json(output / (variant + '-build.json'), receipt)
            try:
                require(all(sha(checkout / name) == digest for name, digest in spec['after_files'].items()),
                        'fetch changed fixed build inputs')
                receipt.update(run_logged(spec['command'], output, variant + '-build', cwd=checkout,
                                          environment=dict(os.environ, **plan['additional_build_environment']), timeout=600))
                require(all(sha(checkout / name) == digest for name, digest in spec['after_files'].items()),
                        'build changed fixed inputs')
                binary = Path(spec['target_dir']) / 'x86_64-pc-windows-msvc/release/alacritty-measure.exe'
                receipt['binary'] = dict(bytes=binary.stat().st_size, sha256=sha(binary))
                run_logged([str(binary), '--identity'], output, variant + '-identity', cwd=checkout, timeout=10)
                receipt['identity'] = json.loads((output / (variant + '-identity.stdout')).read_bytes().decode('utf-8'))
                require(receipt['identity'] == IDENTITY, 'compiled allocator mode mismatch')
                receipt['status'] = 'built_identity_only'
            except BaseException as error:
                receipt.update(status='failed', error=f'{type(error).__name__}: {error}')
                raise
            finally:
                write_json(output / (variant + '-build.json'), receipt)
        audit(output, plan)
        load_builds(output / 'plan-v1.json', output)
        status['status'] = 'built_and_audited'
    except BaseException as error:
        status.update(status='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        status['finished_utc'] = utc()
        write_json(output / 'preparation.json', status)
    return status


def audit(output, plan):
    import tomllib
    rustc = Path(subprocess.check_output(['rustup', 'which', '--toolchain', '1.92.0', 'rustc'], timeout=30).decode('utf-8').strip())
    report = dict(schema='termforge-r54-local-build-audit-v1', builds={}, short_sanity_calls=[],
                  compiler=dict(sha256=sha(rustc), rustc_vv=plan['rustc_vv']), passed=False,
                  formal_samples=0, performance_gate='not_evaluated', environment_idle='not_measured')
    try:
        for variant, spec in plan['builds'].items():
            receipt_path = output / (variant + '-build.json')
            receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
            checkout = output / ('source-' + variant)
            metadata = tomllib.loads((checkout / 'experiments/candidates/alacritty/vendor/alacritty_terminal/Cargo.toml').read_text(encoding='utf-8'))
            require(metadata['package']['version'] == SOURCES[variant]['version'], 'vendor version mismatch')
            log = (output / (variant + '-build.stderr')).read_text(encoding='utf-8')
            names = {json.loads('"' + value + '"') for value in re.findall(r'invoking linker "([^\n]+?)"', log)}
            require(len(names) == 1, 'build log must identify one actual linker')
            linker = Path(next(iter(names)))
            require(linker.name == 'link.exe' and linker.is_file(), 'actual MSVC linker missing')
            report['builds'][variant] = dict(source=spec['commit'], version=metadata['package']['version'],
                candidate_tree=spec['candidate_tree'], tracked_candidate_files=len(spec['after_files']),
                changed_paths=spec['changed_paths'], binary=receipt['binary'], identity=receipt['identity'],
                linker=dict(path=str(linker), sha256=sha(linker)), receipt_sha256=sha(receipt_path))
            binary = Path(spec['target_dir']) / 'x86_64-pc-windows-msvc/release/alacritty-measure.exe'
            for route in ('project', 'interactive'):
                directory = output / f'sanity-{variant}-{route}'
                directory.mkdir()
                response = capture([str(binary), route, '4096', '0', '0.1'], directory, timeout=10)
                report['short_sanity_calls'].append(dict(variant=variant, route=route, passed=False, **response))
                require(response['exit_code'] == 0 and not response['timed_out'], 'short correctness call failed')
                result = json.loads((directory / 'stdout.bin').read_bytes().decode('utf-8'))
                validate_result(result, .1, expected_sample(dict(variant=variant, route=route, chunk=4096)))
                report['short_sanity_calls'][-1]['passed'] = True
        require(report['builds']['baseline']['linker'] == report['builds']['candidate']['linker'], 'source pair used different linkers')
        report['passed'] = True
    finally:
        write_json(output / 'build-audit.json', report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    value = prepare_builds(args.out)
    print(json.dumps(value))


if __name__ == '__main__':
    main()
