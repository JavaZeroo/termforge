"""Verify the frozen plan against a retained Windows common-harness build bundle."""
import json
from pathlib import Path
import subprocess

from experiments.measure.r54_batch import sha
from experiments.measure.r54_contract import ROOT, SOURCES, canonical, protocol
import hashlib

OVERLAYS = ('experiments/candidates/alacritty/Cargo.toml',
            'experiments/candidates/alacritty/src/measure.rs')
IDENTITY = dict(schema='termforge-core-cost-build-v1', allocator_wrapper_compiled=False,
                timing_gates_compiled=True, projection_selectable=True)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(directory, *args):
    return subprocess.check_output(['git', *args], cwd=directory, timeout=30).decode('utf-8').strip()


def load_plan(path):
    value = read(path)
    require(canonical(value['protocol']) == canonical(protocol()), 'frozen protocol changed')
    require(value['protocol_sha256'] == hashlib.sha256(canonical(protocol())).hexdigest(), 'protocol hash mismatch')
    require(value['phase'] == 'prepared_not_built_or_measured', 'plan phase changed')
    require(value['performance_gate'] == 'not_evaluated', 'plan cannot pass performance')
    require(value['builds'] == {name: None for name in SOURCES} and value['environment_evidence'] is None,
            'frozen preparation plan was rewritten with results')
    for name, expected in value['harness_sha256'].items():
        require(sha(ROOT / name) == expected, f'harness changed: {name}')
    require(set(value['harness_sha256']) == {*OVERLAYS, 'experiments/measure/run.py',
                                           'experiments/measure/r54_contract.py'}, 'incomplete harness identity')
    return value


def load_builds(plan_path, bundle):
    """Audit artifacts and current sources; logs remain evidence, not a signature.

    This consumes the retained v1 preparation layout. No source, build receipt or
    binary is reconstructed, edited or retried by the measurement entry point.
    """
    bundle = Path(bundle).resolve(strict=True)
    plan = load_plan(plan_path)
    build_path = bundle / 'build-plan.json'
    build, audit = read(build_path), read(bundle / 'build-audit.json')
    require(build['schema'] == 'termforge-r54-local-build-plan-v1', 'unknown build bundle')
    require(build['frozen_plan']['sha256'] == sha(plan_path), 'build belongs to another plan')
    require(build['protocol_sha256'] == plan['protocol_sha256'], 'build protocol mismatch')
    require(set(build['builds']) == set(SOURCES) and audit['passed'] is True, 'incomplete build audit')
    overrides = ('RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS', 'CARGO_BUILD_TARGET', 'CARGO_BUILD_RUSTFLAGS',
                 'CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS', 'CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER',
                 'RUSTC', 'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER')
    require(all(build['environment'][name] in (None, '') for name in overrides), 'unreviewed compiler override')
    require(build['additional_build_environment'] == {'RUSTC_LOG': 'rustc_codegen_ssa::back::link=info'},
            'unreviewed build environment')
    for name in plan['harness_sha256']:
        original = subprocess.check_output(['git', 'show', f"{build['harness_source']}:{name}"], cwd=ROOT, timeout=30)
        require(original.replace(b'\r\n', b'\n') == (ROOT / name).read_bytes().replace(b'\r\n', b'\n'),
                f'harness source mismatch: {name}')
    commands, identities, evidence = {}, {}, {}
    evidence['build-plan.json'] = sha(build_path)
    evidence['build-audit.json'] = sha(bundle / 'build-audit.json')
    targets = []
    for variant, source in SOURCES.items():
        spec = build['builds'][variant]
        checkout = bundle / ('source-' + variant)
        require(spec['commit'] == source['commit'] == git(checkout, 'rev-parse', 'HEAD'), 'source commit mismatch')
        require(spec['tree'] == git(checkout, 'rev-parse', 'HEAD^{tree}'), 'source tree mismatch')
        require(spec['candidate_tree'] == git(checkout, 'rev-parse', 'HEAD:experiments/candidates/alacritty'), 'candidate tree mismatch')
        require(spec['version'] == source['version'], 'core version mismatch')
        require(set(spec['overlay_files']) == set(OVERLAYS), 'unexpected harness overlay')
        changed = git(checkout, 'diff', '--name-only', 'HEAD').splitlines()
        require(set(changed) == set(spec['changed_paths']) == set(OVERLAYS), 'source changes outside harness')
        files = set(git(checkout, 'ls-files', 'experiments/candidates/alacritty').splitlines())
        require(files == set(spec['before_files']) == set(spec['after_files']), 'incomplete source manifest')
        for name in files:
            require(sha(checkout / name) == spec['after_files'][name], f'changed build input: {name}')
            if name in OVERLAYS:
                require(spec['after_files'][name] == plan['harness_sha256'][name], 'different common harness')
            else:
                require(spec['before_files'][name] == spec['after_files'][name], 'changed original source')
        target = Path(spec['target_dir']).resolve(strict=True)
        targets.append(target)
        expected_command = ['cargo', '+1.92.0', 'build', '--manifest-path',
                            str(checkout / 'experiments/candidates/alacritty/Cargo.toml'),
                            '--release', '--locked', '--offline', '--no-default-features', '--jobs', '1',
                            '--bin', 'alacritty-measure', '--target', 'x86_64-pc-windows-msvc',
                            '--target-dir', str(target), '-vv']
        require(spec['command'] == expected_command, 'build command is outside the fixed Windows configuration')
        receipt_path = bundle / (variant + '-build.json')
        receipt, observed = read(receipt_path), audit['builds'][variant]
        require(observed['source'] == source['commit'] and observed['version'] == source['version'] and
                observed['candidate_tree'] == spec['candidate_tree'], 'build audit source mismatch')
        require(receipt['status'] == 'built_identity_only' and receipt['exit_code'] == 0, 'build failed')
        require(receipt['variant'] == variant and receipt['command'] == spec['command'], 'build receipt command mismatch')
        require(receipt['build_plan_sha256'] == sha(build_path), 'receipt belongs to another build plan')
        require(observed['receipt_sha256'] == sha(receipt_path), 'build receipt changed')
        require(receipt['identity'] == observed['identity'] == IDENTITY, 'unexpected compiled allocator mode')
        binary = target / 'x86_64-pc-windows-msvc/release/alacritty-measure.exe'
        binary_identity = dict(bytes=binary.stat().st_size, sha256=sha(binary))
        require(binary_identity == receipt['binary'] == observed['binary'], 'binary changed since build')
        require(sha(Path(observed['linker']['path'])) == observed['linker']['sha256'], 'linker changed since audit')
        require(set(receipt['logs']) == {variant + '-build.stdout', variant + '-build.stderr'}, 'missing build logs')
        for name, identity in receipt['logs'].items():
            require(Path(name).name == name, 'log must stay in the bundle')
            path = bundle / name
            require(path.stat().st_size == identity['bytes'] and sha(path) == identity['sha256'], 'build log changed')
            evidence[name] = sha(path)
        patch = variant + '-harness.patch'
        require(sha(bundle / patch) == spec['overlay_patch_sha256'], 'harness patch changed')
        require(read(bundle / (variant + '-identity.stdout')) == IDENTITY, 'recorded binary identity mismatch')
        evidence.update({receipt_path.name: sha(receipt_path), patch: sha(bundle / patch)})
        for suffix in ('-identity.stdout', '-identity.stderr'):
            evidence[variant + suffix] = sha(bundle / (variant + suffix))
        commands[variant] = [str(binary)]
        identities[variant] = dict(source=source, binary=binary_identity,
                                   candidate_tree=spec['candidate_tree'], tracked_files=len(files))
    require(targets[0] != targets[1], 'shared target directory')
    return dict(commands=commands, builds=identities, evidence=evidence,
                bundle=str(bundle), plan_sha256=sha(plan_path),
                protocol_sha256=plan['protocol_sha256'], harness_source=build['harness_source'],
                rustc_vv=build['rustc_vv'], cargo_vv=build['cargo_vv'],
                build_environment=build['environment'], additional_build_environment=build['additional_build_environment'])
