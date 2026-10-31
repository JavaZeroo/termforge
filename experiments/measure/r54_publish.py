"""Publish a fixed synthetic-artifact whitelist from the owned hosted job."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

from experiments.measure.r54_batch import sha, write_json
from experiments.measure.r54_contract import ROOT, SEEDS
from experiments.measure.r54_hosted import hosted_identity
from experiments.measure.r54_inputs import require


def public_environment(value):
    """CPU counters remain; process names/PIDs and unrecognized fields do not."""
    fields = ('status', 'host', 'started_utc', 'finished_utc', 'intervals', 'declaration', 'policy', 'power_policy', 'error')
    result = {name: value[name] for name in fields if name in value}
    if 'host' in result:
        host = value['host']
        result['host'] = {name: host[name] for name in ('platform', 'python', 'psutil', 'cpu_logical', 'cpu_physical', 'cpu', 'windows') if name in host}
    return result


def validate_raw_identity(root, identity):
    value = json.loads((root / 'hosted.json').read_text(encoding='utf-8'))
    require(value['schema'] == 'termforge-r54-hosted-v1' and value['hosted_identity'] == identity and
            value['source'] == identity['GITHUB_SHA'], 'raw evidence belongs to another run, attempt or source')


def windows_short_path(path):
    """Ask Windows for the actual alias; guessing RUNNER~1 is not reliable."""
    import ctypes
    from ctypes import wintypes
    function = ctypes.WinDLL('kernel32', use_last_error=True).GetShortPathNameW
    function.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    function.restype = wintypes.DWORD
    length = function(path, None, 0)
    if not length:
        raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_unicode_buffer(length)
    copied = function(path, buffer, length)
    if not copied or copied >= length:
        raise ValueError('short-path lookup failed or changed during publication')
    return buffer.value


def replacement_forms(prefixes):
    forms = []
    for prefix, alias in prefixes:
        forms.extend((form, alias) for form in (prefix.replace('\\', '\\\\'), prefix, prefix.replace('\\', '/')))
    return sorted(set(forms), key=lambda item: len(item[0]), reverse=True)


def redact_paths(data, replacements):
    # Preserve arbitrary log bytes. The hosted Windows path prefixes are ASCII
    # and may use a different case or a compiler-escaped 8.3 alias.
    for before, after in replacements:
        data = re.sub(re.escape(before.encode('utf-8')), lambda _: after.encode('utf-8'), data, flags=re.IGNORECASE)
    return data


def publish_jsonl(original, replacements):
    # JSONL is appended, unlike the atomic JSON manifests. A stop can leave an
    # incomplete final row. Preserve it and the complete rows, mark the parse
    # problem, and continue exporting independently retained sample streams.
    status = dict(complete=True, error=None)
    for index, line in enumerate(original.splitlines(), start=1):
        try:
            json.loads(line.decode('utf-8'))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            status.update(complete=False, error=f'{type(error).__name__}: {error}', error_line=index)
            break
    return redact_paths(original, replacements), status


def selected_paths(root):
    """No recursive directory export: only named builder and runner artifacts."""
    names = ['hosted.json', 'environment-declaration.json']
    names += ['bundle/' + name for name in ('preparation.json', 'plan-v1.json', 'build-plan.json', 'build-audit.json')]
    for variant in ('baseline', 'candidate'):
        names += [f'bundle/{variant}-harness.patch', f'bundle/{variant}-build.json']
        for step in ('checkout', 'fetch', 'build', 'identity'):
            names += [f'bundle/{variant}-{step}.{suffix}' for suffix in ('stdout', 'stderr', 'process.json')]
        for route in ('project', 'interactive'):
            names += [f'bundle/sanity-{variant}-{route}/{stream}.bin' for stream in ('stdout', 'stderr')]
    for seed in SEEDS:
        prefix = f'batch-{seed}/'
        names += [prefix + name for name in ('manifest.json', 'runner.json', 'environment.json', 'environment-policy.json',
                  'environment-declaration.json', 'samples/context.json', 'samples/manifest.json', 'samples/samples.jsonl', 'samples/summary.json')]
        for directory in ('baseline-identity', 'candidate-identity', 'power-policy', 'power-policy-after'):
            names += [prefix + directory + '/' + name for name in ('record.json', 'stdout.bin', 'stderr.bin')]
        for sequence in range(112):
            names += [prefix + f'samples/{sequence:03d}/' + name for name in ('record.json', 'stdout.bin', 'stderr.bin')]
    return [Path(name) for name in names if (root / name).is_file()]


def publish(root, output):
    identity = hosted_identity()
    root, output = Path(root).resolve(), Path(output).resolve()
    require(root.is_relative_to(ROOT / 'local') and output.is_relative_to(ROOT / 'local'), 'publication paths must stay in workspace local')
    require(root != output and not output.is_relative_to(root), 'public output must be separate from raw records')
    require(not output.exists(), 'public output already exists')
    validate_raw_identity(root, identity)
    staging = output.with_name(output.name + '.partial')
    require(staging.is_relative_to(ROOT / 'local'), 'staging path escaped workspace')
    staging.mkdir(parents=True, exist_ok=False)
    prefixes = [(str(ROOT), '${WORKSPACE}'), (os.environ.get('USERPROFILE', ''), '${PROFILE}'),
                (os.environ.get('RUNNER_TEMP', ''), '${RUNNER_TEMP}')]
    prefixes = [(prefix, alias) for prefix, alias in prefixes if prefix]
    prefixes += [(windows_short_path(prefix), alias) for prefix, alias in prefixes]
    replacements = replacement_forms(prefixes)

    def text(value):
        return redact_paths(value.encode('utf-8'), replacements).decode('utf-8')

    def transform(value):
        if isinstance(value, dict):
            return {key: transform(item) for key, item in value.items()}
        if isinstance(value, list):
            return [transform(item) for item in value]
        return text(value) if isinstance(value, str) else value

    inventory = []
    for relative in selected_paths(root):
        source = root / relative
        require(source.resolve().is_relative_to(root), 'artifact path escaped raw directory')
        original = source.read_bytes()
        jsonl_status = None
        # The measurement EXE consumes only public, fixed synthetic workloads.
        # Preserve its raw streams even on failure. Other logs can contain host
        # path prefixes; redact those without discarding their full contents.
        raw_sample = bool(re.fullmatch(r'(?:batch-\d+/samples/\d{3}|bundle/sanity-(?:baseline|candidate)-(?:project|interactive))/(?:stdout|stderr)\.bin', relative.as_posix()))
        if raw_sample:
            data, treatment = original, 'byte_exact_owned_synthetic_stream'
        elif relative.suffix == '.json':
            value = json.loads(original.decode('utf-8'))
            if relative.name == 'environment.json':
                value = public_environment(value)
            data = (json.dumps(transform(value), ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')
            treatment = 'json_whitelist_and_path_redaction'
        elif relative.suffix == '.jsonl':
            data, jsonl_status = publish_jsonl(original, replacements)
            treatment = 'jsonl_path_redaction'
        else:
            data = redact_paths(original, replacements)
            treatment = 'complete_log_with_path_prefix_redaction'
        target = staging / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        inventory.append(dict(path=relative.as_posix(), original_bytes=len(original),
                              original_sha256=hashlib.sha256(original).hexdigest(),
                              published_bytes=len(data), published_sha256=sha(target), treatment=treatment,
                              jsonl_status=jsonl_status))
    write_json(staging / 'publication.json', dict(schema='termforge-r54-hosted-publication-v1',
        hosted_identity=identity, artifacts=inventory, raw_process_identities_published=False,
        full_environment_variables_published=False, performance_gate='not_evaluated',
        limitation='guest CPU evidence cannot establish exclusive physical host or stable frequency'))
    staging.rename(output)
    return dict(files=len(inventory), output=str(output), performance_gate='not_evaluated')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(publish(args.raw, args.out)))


if __name__ == '__main__':
    main()
