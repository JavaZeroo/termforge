"""Execute the frozen R54 sequence without retries or a product speed verdict."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

from experiments.measure.r54_contract import CHUNKS, CPU_SECONDS, SEEDS, SOURCES, sample_order, validate_sample


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    """Replace only a live manifest; frozen inputs and stream files use create-only."""
    # A previous interrupted write may leave a partial temporary file. Give
    # every attempt its own name so final failure/interruption recording still
    # works; leave incomplete files for diagnosis rather than overwriting them.
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                     prefix=path.name + '.', suffix='.tmp', delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def capture(command, output, timeout=90):
    """Direct-child streams stay byte-exact even on invalid UTF-8 or timeout."""
    result = dict(command=command, started_utc=utc(), exit_code=None, timed_out=False)
    with (output / 'stdout.bin').open('xb') as stdout, (output / 'stderr.bin').open('xb') as stderr:
        try:
            result['exit_code'] = subprocess.run(command, stdout=stdout, stderr=stderr,
                                                 stdin=subprocess.DEVNULL, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            result.update(timed_out=True, error=f'direct child timed out after {timeout}s')
        except OSError as error:
            result['error'] = str(error)
    result['finished_utc'] = utc()
    result['streams'] = {name: dict(bytes=(output / name).stat().st_size, sha256=sha(output / name))
                         for name in ('stdout.bin', 'stderr.bin')}
    return result


def run_schedule(commands, seed, output, context):
    """Process/artifact seam. Provenance and environment checks belong to the CLI.

    samples_complete means only the ordered process results validated. It never
    establishes host isolation, physical CPU exclusivity or performance parity.
    """
    if seed not in SEEDS or set(commands) != set(SOURCES):
        raise ValueError('unknown seed or source pair')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / 'context.json', context)
    manifest = dict(schema='termforge-r54-batch-v1', seed=seed, status='running',
                    started_utc=utc(), attempted=0, valid_samples=0,
                    active_sequence=None, performance_gate='not_evaluated',
                    context_sha256=sha(output / 'context.json'))
    records = []
    try:
        with (output / 'samples.jsonl').open('x', encoding='utf-8') as samples:
            for item in sample_order(seed):
                directory = output / f"{item['sequence']:03d}"
                directory.mkdir()
                command = list(commands[item['variant']]) + [item['route'], str(item['chunk']), '0', str(CPU_SECONDS)]
                record = dict(item=item, status='started', command=command)
                manifest.update(active_sequence=item['sequence'], attempted=manifest['attempted'] + 1)
                write_json(output / 'manifest.json', manifest)
                write_json(directory / 'record.json', record)
                try:
                    monitored = context.get('environment_policy') is not None
                    if monitored:
                        from experiments.measure.r54_environment import POLICY, cpu_delta, cpu_snapshot
                        expected_sha = context['builds'][item['variant']]['binary']['sha256']
                        if sha(command[0]) != expected_sha:
                            raise ValueError('binary changed before sample')
                        before = cpu_snapshot()
                    record.update(capture(command, directory))
                    if monitored:
                        after = cpu_snapshot()
                        record['environment'] = dict(cpu_delta(before, after), before=before, after=after)
                    if record['timed_out'] or record['exit_code'] != 0:
                        raise ValueError(record.get('error', 'nonzero child exit'))
                    result = json.loads((directory / 'stdout.bin').read_bytes().decode('utf-8'))
                    validate_sample(result, item)
                    record['result'] = result
                    if monitored:
                        if sha(command[0]) != expected_sha or result['cpu_clock'] != 'GetProcessTimes kernel plus user':
                            raise ValueError('runtime binary or clock identity changed')
                        env = record['environment']
                        # The outer host interval includes preparation/oracle and
                        # runner work excluded by the inner measured CPU interval.
                        # Retain this conservative estimate; never subtract it from
                        # costs or claim an exact per-process background partition.
                        remaining = env['busy_seconds'] - result['interval_cpu_seconds']
                        env['unattributed_busy_percent_upper_estimate'] = remaining * 100 / env['total_seconds']
                        if remaining < 0 or env['unattributed_busy_percent_upper_estimate'] > POLICY['max_unattributed_busy_percent']:
                            raise ValueError('host activity guard failed; sample retained, batch stopped')
                    record['status'] = 'valid'
                except Exception as error:
                    record.update(status='failed', error=f'{type(error).__name__}: {error}')
                write_json(directory / 'record.json', record)
                samples.write(json.dumps(record, ensure_ascii=False, allow_nan=False) + '\n')
                samples.flush()
                os.fsync(samples.fileno())
                records.append(record)
                if record['status'] != 'valid':
                    raise ValueError(f"sample {item['sequence']} failed: {record['error']}")
                manifest['valid_samples'] += 1
        write_json(output / 'summary.json', summarize(records, seed))
        manifest.update(status='samples_complete', active_sequence=None)
    except Exception as error:
        manifest.update(status='failed', error=f'{type(error).__name__}: {error}')
    except BaseException:
        manifest['status'] = 'interrupted'
        raise
    finally:
        manifest['finished_utc'] = utc()
        write_json(output / 'manifest.json', manifest)
    return manifest


def distribution(values):
    median = statistics.median(values)
    return dict(count=len(values), median=median, min=min(values), max=max(values),
                mad=statistics.median(abs(value - median) for value in values))


def summarize(records, seed):
    """Require the whole ordered batch before computing within-repeat ratios."""
    if seed not in SEEDS or len(records) != len(sample_order(seed)):
        raise ValueError('incomplete or unknown batch')
    for record, expected in zip(records, sample_order(seed)):
        if record['item'] != expected or record['status'] != 'valid':
            raise ValueError('failed or reordered sample')
        validate_sample(record['result'], expected)
    groups = []
    for route, chunks in CHUNKS.items():
        for chunk in chunks:
            pairs = {}
            samples = {variant: dict(cpu_ns_per_byte=[], wall_ns_per_byte=[]) for variant in ('baseline', 'candidate')}
            for record in records:
                item, result = record['item'], record['result']
                if (item['route'], item['chunk']) != (route, chunk):
                    continue
                volume = result['rounds'] * result['bytes_per_round']
                cpu = result['interval_cpu_seconds'] * 1e9 / volume
                wall = result['interval_wall_seconds'] * 1e9 / volume
                samples[item['variant']]['cpu_ns_per_byte'].append(cpu)
                samples[item['variant']]['wall_ns_per_byte'].append(wall)
                pairs.setdefault(item['repeat'], {})[item['variant']] = cpu
            ratios = [pair['candidate'] / pair['baseline'] for _, pair in sorted(pairs.items())]
            groups.append(dict(route=route, chunk=chunk, paired_cpu_ratios=ratios,
                               paired_cpu_ratio_median=statistics.median(ratios),
                               samples={variant: {metric: dict(distribution(values), values=values)
                                                  for metric, values in metrics.items()}
                                        for variant, metrics in samples.items()}))
    return dict(seed=seed, groups=groups, performance_gate='not_evaluated',
                budget_status='not_set', receive_seconds=None,
                input_latency_seconds=None, presented_done=None)


def run(plan_path, bundle, declaration_path, output, seed):
    """Validate provenance and observe the host before any cost workload runs."""
    from experiments.measure.r54_environment import POLICY, preflight
    from experiments.measure.r54_inputs import IDENTITY, ROOT, git, load_builds, read, require
    if os.name != 'nt':
        raise ValueError('v1 build binding and environment observation require Windows')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema='termforge-r54-execution-v1', status='preparing', seed=seed,
                    started_utc=utc(), attempted=0, valid_samples=0, performance_gate='not_evaluated')
    write_json(output / 'manifest.json', manifest)
    try:
        runner = dict(commit=git(ROOT, 'rev-parse', 'HEAD'), worktree_status=git(ROOT, 'status', '--porcelain'),
                      sources={name: sha(ROOT / name) for name in (
                          'experiments/measure/r54_batch.py', 'experiments/measure/r54_inputs.py',
                          'experiments/measure/r54_environment.py', 'experiments/measure/r54_contract.py',
                          'experiments/measure/run.py', 'experiments/measure/compare.py')})
        write_json(output / 'runner.json', runner)
        manifest['runner_identity_sha256'] = sha(output / 'runner.json')
        write_json(output / 'manifest.json', manifest)
        require(seed in SEEDS, 'unknown batch seed')
        require(not runner['worktree_status'], 'runner source must be committed and clean')
        binding = load_builds(plan_path, bundle)
        inputs = output / 'inputs'
        inputs.mkdir()
        (inputs / 'plan.json').write_bytes(Path(plan_path).read_bytes())
        for name, digest in binding['evidence'].items():
            payload = (Path(bundle) / name).read_bytes()
            require(hashlib.sha256(payload).hexdigest() == digest, 'build evidence changed while copying')
            (inputs / name).write_bytes(payload)
        write_json(inputs / 'binding.json', binding)
        for variant, command in binding['commands'].items():
            directory = output / (variant + '-identity')
            directory.mkdir()
            result = capture(command + ['--identity'], directory, timeout=10)
            write_json(directory / 'record.json', result)
            require(result['exit_code'] == 0 and read(directory / 'stdout.bin') == IDENTITY,
                    'actual compiled identity mismatch')
        environment = preflight(output, read(declaration_path))
        require(environment['status'] == 'low_activity_observed', 'host preflight rejected; no cost samples launched')
        context = dict(binding, evidence_kind='fixed_core_cost', environment_policy=POLICY,
                       environment_sha256=sha(output / 'environment.json'), runner_commit=runner['commit'],
                       runner_sources=runner['sources'], runner_identity_sha256=manifest['runner_identity_sha256'])
        result = run_schedule(binding['commands'], seed, output / 'samples', context)
        manifest.update(status=result['status'], attempted=result['attempted'], valid_samples=result['valid_samples'])
        require(result['status'] == 'samples_complete', result.get('error', 'sample execution failed'))
        require(load_builds(plan_path, bundle) == binding, 'build inputs changed during batch')
        for name, digest in context['runner_sources'].items():
            require(sha(ROOT / name) == digest, 'runner changed during batch')
        power = output / 'power-policy-after'
        power.mkdir()
        observed = capture(['powercfg', '/getactivescheme'], power, timeout=10)
        write_json(power / 'record.json', observed)
        require(observed['exit_code'] == 0 and (power / 'stdout.bin').read_bytes() ==
                (output / 'power-policy/stdout.bin').read_bytes(), 'power policy changed during batch')
        manifest['status'] = 'complete_diagnostic'
    except Exception as error:
        manifest.update(status='failed', error=f'{type(error).__name__}: {error}')
    except BaseException:
        manifest['status'] = 'interrupted'
        raise
    finally:
        samples_manifest = output / 'samples/manifest.json'
        if samples_manifest.exists():
            recorded = read(samples_manifest)
            manifest.update(attempted=recorded['attempted'], valid_samples=recorded['valid_samples'])
        manifest['finished_utc'] = utc()
        write_json(output / 'manifest.json', manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--build-bundle', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, choices=SEEDS, required=True)
    args = parser.parse_args()
    result = run(args.plan, args.build_bundle, args.environment, args.out, args.seed)
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result['status'] == 'complete_diagnostic' else 1)


if __name__ == '__main__':
    main()
