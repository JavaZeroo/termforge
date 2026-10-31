"""Separate whole-child CPU accounting from R54's unchanged v1 cost batches."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

from experiments.measure.r54_batch import capture, sha, utc, write_json
from experiments.measure.r54_contract import CPU_SECONDS, ROOT, SOURCES, validate_sample
from experiments.measure.r54_environment import cpu_delta, cpu_snapshot, host_identity
from experiments.measure.r54_inputs import IDENTITY, git, load_builds, read, require

PROTOCOL = dict(schema='termforge-r54-cpu-boundary-v1',
                order=['baseline', 'candidate', 'candidate', 'baseline', 'baseline', 'candidate'],
                route='interactive', chunk=1, observe=0, inner_cpu_seconds=CPU_SECONDS,
                process_timeout_seconds=90, cleanup_timeout_seconds=5,
                activity_acceptance_gate=False, performance_gate='not_evaluated',
                purpose='separate whole-child, inner interval and runner CPU; no cost ratios',
                residual_scope='other guest activity plus counter quantization and bracket mismatch; not exact attribution')


def partition_cpu(record, inner_cpu):
    """Expose balances without labelling the signed residual external activity.

    Counter updates and non-atomic bracket endpoints can leave a negative
    residual. Keep it visible; do not clamp, normalize or subtract it from cost.
    """
    total = record['environment']['total_seconds']
    busy = record['environment']['busy_seconds']
    child = record['child_cpu']['total_seconds']
    runner = record['runner_cpu_seconds']
    if (any(not math.isfinite(value) or value < 0 for value in (total, busy, child, runner, inner_cpu))
            or total <= 0 or child < inner_cpu):
        raise ValueError('inconsistent whole-child or CPU accounting')
    residual = busy - child - runner
    return dict(inner_only_balance_seconds=busy - inner_cpu,
                inner_only_balance_percent=(busy - inner_cpu) * 100 / total,
                child_outside_inner_seconds=child - inner_cpu,
                runner_cpu_seconds=runner, unassigned_balance_seconds=residual,
                unassigned_balance_percent=residual * 100 / total)


def exited_child_cpu(process):
    """Borrow CPython's retained Windows process handle after wait, before close.

    The handle belongs to Popen. Querying by PID after exit would lose the
    lifetime or risk a reused identity; retaining this handle avoids that race.
    This private CPython dependency is limited to the pinned diagnostic runtime.
    """
    if process.returncode is None:
        raise ValueError('child must have exited before lifetime accounting')
    creation, end, kernel, user = (wintypes.FILETIME() for _ in range(4))
    function = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    function.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    function.restype = wintypes.BOOL
    if not function(int(process._handle), ctypes.byref(creation), ctypes.byref(end),
                    ctypes.byref(kernel), ctypes.byref(user)):
        raise ctypes.WinError(ctypes.get_last_error())
    ticks = lambda value: (value.dwHighDateTime << 32) | value.dwLowDateTime
    return dict(creation_filetime=ticks(creation), exit_filetime=ticks(end),
                kernel_seconds=ticks(kernel) / 10_000_000,
                user_seconds=ticks(user) / 10_000_000,
                total_seconds=(ticks(kernel) + ticks(user)) / 10_000_000)


def capture_cpu(command, output, timeout=90):
    """Capture a direct child and its complete CPU lifetime, including exit.

    The fixed measurement executables do not spawn children. This helper does
    not claim descendant accounting or cleanup. Stream bytes are never decoded
    or rewritten here, including failed and timed-out executions.
    """
    if os.name != 'nt' or sys.implementation.name != 'cpython' or sys.version_info[:2] != (3, 12):
        raise ValueError('CPU diagnostic requires Windows CPython 3.12')
    output = Path(output)
    result = dict(command=command, started_utc=utc(), exit_code=None, timed_out=False,
                  child_cpu=None, clock='GetProcessTimes kernel plus user',
                  runtime=sys.version, accounting_scope='direct child only')
    with (output / 'stdout.bin').open('xb') as stdout, (output / 'stderr.bin').open('xb') as stderr:
        before = cpu_snapshot()
        runner_before = time.process_time()
        try:
            # Popen.__exit__ waits without a bound. Keep the object alive for
            # accounting, but use explicit bounded waits on failure instead.
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)
            result['child_pid'] = child.pid
            try:
                child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                result.update(timed_out=True, error=f'direct child timed out after {timeout}s')
                child.kill()
                child.wait(timeout=PROTOCOL['cleanup_timeout_seconds'])
            except BaseException:
                child.kill()
                child.wait(timeout=PROTOCOL['cleanup_timeout_seconds'])
                raise
            result['exit_code'] = child.returncode
            result['child_cpu'] = exited_child_cpu(child)
        except (OSError, subprocess.SubprocessError) as error:
            result['error'] = f'{type(error).__name__}: {error}'
        finally:
            runner_after = time.process_time()
            after = cpu_snapshot()
            result['runner_cpu_seconds'] = runner_after - runner_before
            result['runner_cpu_before_seconds'] = runner_before
            result['runner_cpu_after_seconds'] = runner_after
            result['environment'] = dict(cpu_delta(before, after), before=before, after=after)
    result['finished_utc'] = utc()
    result['streams'] = {name: dict(bytes=(output / name).stat().st_size, sha256=sha(output / name))
                         for name in ('stdout.bin', 'stderr.bin')}
    return result


def run_observations(commands, output, context):
    """Process/artifact seam for the fixed six-observation diagnostic only."""
    require(set(commands) == set(SOURCES), 'fixed source pair required')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / 'context.json', context)
    write_json(output / 'protocol.json', PROTOCOL)
    manifest = dict(schema='termforge-r54-cpu-observations-v1', status='running',
                    started_utc=utc(), attempted=0, accounted_observations=0,
                    context_sha256=sha(output / 'context.json'), protocol_sha256=sha(output / 'protocol.json'),
                    performance_gate='not_evaluated')
    try:
        for sequence, variant in enumerate(PROTOCOL['order']):
            item = dict(sequence=sequence, variant=variant, route=PROTOCOL['route'], chunk=PROTOCOL['chunk'])
            directory = output / f'{sequence:03d}'
            directory.mkdir()
            record = dict(item=item, status='started')
            manifest.update(attempted=sequence + 1, active_sequence=sequence)
            write_json(output / 'manifest.json', manifest)
            write_json(directory / 'record.json', record)
            try:
                command = list(commands[variant]) + [item['route'], str(item['chunk']), '0', str(CPU_SECONDS)]
                before_sha = sha(command[0])
                if 'binding' in context:
                    require(before_sha == context['binding']['builds'][variant]['binary']['sha256'],
                            'binary changed before observation')
                record.update(capture_cpu(command, directory, PROTOCOL['process_timeout_seconds']))
                require(not record['timed_out'] and record['exit_code'] == 0 and 'error' not in record,
                        record.get('error', 'nonzero child exit'))
                result = json.loads((directory / 'stdout.bin').read_bytes().decode('utf-8'))
                validate_sample(result, item)
                require(result['cpu_clock'] == 'GetProcessTimes kernel plus user', 'wrong child CPU clock')
                require(sha(command[0]) == before_sha, 'binary changed during observation')
                record['result'] = result
                record['accounting'] = partition_cpu(record, result['interval_cpu_seconds'])
                record['status'] = 'accounted'
            except Exception as error:
                record.update(status='failed', error=f'{type(error).__name__}: {error}')
            finally:
                write_json(directory / 'record.json', record)
            require(record['status'] == 'accounted', record.get('error', 'observation incomplete'))
            manifest['accounted_observations'] += 1
        manifest.update(status='accounting_complete', active_sequence=None)
    except Exception as error:
        manifest.update(status='failed', error=f'{type(error).__name__}: {error}')
    except BaseException:
        manifest['status'] = 'interrupted'
        raise
    finally:
        manifest['finished_utc'] = utc()
        write_json(output / 'manifest.json', manifest)
    return manifest


def execute(plan, bundle, declaration, output):
    """Audit retained builds; do not rebuild or repeat an old preflight/batch."""
    require(os.name == 'nt', 'Windows accounting required')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema='termforge-r54-cpu-diagnostic-v1', status='preparing', started_utc=utc(),
                    performance_gate='not_evaluated')
    try:
        runner = dict(commit=git(ROOT, 'rev-parse', 'HEAD'), worktree_status=git(ROOT, 'status', '--porcelain'),
                      sources={name: sha(ROOT / name) for name in (
                          'experiments/measure/r54_cpu_diagnostic.py', 'experiments/measure/r54_environment.py',
                          'experiments/measure/r54_batch.py', 'experiments/measure/r54_contract.py',
                          'experiments/measure/r54_inputs.py', 'experiments/measure/run.py')})
        write_json(output / 'runner.json', runner)
        require(not runner['worktree_status'], 'runner source must be committed and clean')
        environment = read(declaration)
        require(environment.get('host_kind') in ('shared_workstation', 'hosted_vm', 'dedicated_host') and
                isinstance(environment.get('known_concurrent_work'), list), 'explicit environment declaration required')
        write_json(output / 'environment.json', dict(host=host_identity(), declaration=environment,
                   scope='accounting diagnosis on declared host; no idle qualification or cost acceptance'))
        binding = load_builds(plan, bundle)
        write_json(output / 'binding.json', binding)
        (output / 'plan-v1.json').write_bytes(Path(plan).read_bytes())
        for variant, command in binding['commands'].items():
            directory = output / (variant + '-identity')
            directory.mkdir()
            value = capture(command + ['--identity'], directory, timeout=10)
            write_json(directory / 'record.json', value)
            require(value['exit_code'] == 0 and read(directory / 'stdout.bin') == IDENTITY, 'actual build identity mismatch')
        result = run_observations(binding['commands'], output / 'observations',
                                  dict(evidence_kind='cpu_boundary_diagnostic', binding=binding, runner=runner))
        manifest['observations'] = result
        require(result['status'] == 'accounting_complete', 'diagnostic stopped; no replacement observations')
        require(load_builds(plan, bundle) == binding, 'build binding changed during diagnosis')
        for name, digest in runner['sources'].items():
            require(sha(ROOT / name) == digest, 'runner changed during diagnosis')
        manifest['status'] = 'accounting_complete'
    except Exception as error:
        manifest.update(status='failed', error=f'{type(error).__name__}: {error}')
    except BaseException:
        manifest['status'] = 'interrupted'
        raise
    finally:
        manifest['finished_utc'] = utc()
        write_json(output / 'manifest.json', manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--build-bundle', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = execute(args.plan, args.build_bundle, args.environment, args.out)
    print(json.dumps(result))
    raise SystemExit(0 if result['status'] == 'accounting_complete' else 1)


if __name__ == '__main__':
    main()
