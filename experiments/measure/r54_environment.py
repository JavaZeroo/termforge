"""Read-only Windows activity evidence, with a predeclared diagnostic host guard."""
import ctypes
from ctypes import wintypes
import math
import os
import platform
import time

import psutil

from experiments.measure.r54_batch import capture, utc, write_json

POLICY = dict(schema='termforge-r54-environment-guard-v1', idle_intervals=10,
              idle_interval_seconds=1, max_busy_percent=5.0,
              max_unattributed_busy_percent=5.0, product_performance_budget=False,
              measurement='GetSystemTimes; user + kernel - idle; all logical CPUs, at most 64',
              scope='guest-visible CPU activity only; physical host contention and frequency stability remain unknown')


def cpu_snapshot():
    if os.name != 'nt' or not 0 < (psutil.cpu_count() or 0) <= 64:
        raise ValueError('this host guard supports Windows with 1..64 logical CPUs only')
    idle, kernel, user = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
    function = ctypes.WinDLL('kernel32', use_last_error=True).GetSystemTimes
    function.argtypes = [ctypes.POINTER(wintypes.FILETIME)] * 3
    function.restype = wintypes.BOOL
    if not function(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
        raise ctypes.WinError(ctypes.get_last_error())
    seconds = lambda value: ((value.dwHighDateTime << 32) | value.dwLowDateTime) / 10_000_000
    return dict(user=seconds(user), kernel_including_idle=seconds(kernel), idle=seconds(idle),
                system=seconds(kernel) - seconds(idle), perf_counter_seconds=time.perf_counter(), utc=utc())


def cpu_delta(before, after):
    deltas = {name: after[name] - before[name] for name in ('user', 'system', 'idle')}
    if any(not math.isfinite(value) or value < 0 for value in deltas.values()) or sum(deltas.values()) <= 0:
        raise ValueError('invalid host CPU interval')
    total, busy = sum(deltas.values()), deltas['user'] + deltas['system']
    return dict(total_seconds=total, busy_seconds=busy, busy_percent=busy * 100 / total)


def qualify_idle(intervals):
    return (len(intervals) == POLICY['idle_intervals'] and
            all(math.isfinite(item['busy_percent']) and 0 <= item['busy_percent'] <= POLICY['max_busy_percent']
                for item in intervals))


def process_snapshot():
    """Private evidence contains names/CPU identities, never command lines or env."""
    records = []
    for process in psutil.process_iter(['pid', 'name', 'create_time', 'cpu_times'], ad_value=None):
        row = dict(process.info)
        if row['cpu_times'] is not None:
            row['cpu_times'] = row['cpu_times']._asdict()
        records.append(row)
    return records


def host_identity():
    import winreg
    def registry(key, names):
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as handle:
            return {name: winreg.QueryValueEx(handle, name)[0] for name in names}
    return dict(platform=platform.platform(), python=platform.python_version(), psutil=psutil.__version__,
                cpu_logical=psutil.cpu_count(), cpu_physical=psutil.cpu_count(logical=False),
                cpu=registry(r'HARDWARE\DESCRIPTION\System\CentralProcessor\0', ['ProcessorNameString', 'Identifier']),
                windows=registry(r'SOFTWARE\Microsoft\Windows NT\CurrentVersion', ['CurrentBuildNumber', 'UBR', 'DisplayVersion']))


def preflight(output, declaration):
    """One bounded idle observation; no waiting until green or modifying the host."""
    write_json(output / 'environment-policy.json', POLICY)
    write_json(output / 'environment-declaration.json', declaration)
    if (declaration.get('no_known_concurrent_build_or_test') is not True or
            not isinstance(declaration.get('known_concurrent_work'), list) or
            declaration.get('host_kind') not in ('shared_workstation', 'hosted_vm', 'dedicated_host')):
        raise ValueError('explicit host/concurrent-work declaration required')
    value = dict(status='observing', host=host_identity(), started_utc=utc(), intervals=[],
                 processes_before=process_snapshot(), declaration=declaration, policy=POLICY)
    write_json(output / 'environment.json', value)
    try:
        power = output / 'power-policy'
        power.mkdir()
        value['power_policy'] = capture(['powercfg', '/getactivescheme'], power, timeout=10)
        if value['power_policy']['exit_code'] != 0:
            raise ValueError('power policy observation failed')
        before = cpu_snapshot()
        for _ in range(POLICY['idle_intervals']):
            time.sleep(POLICY['idle_interval_seconds'])
            after = cpu_snapshot()
            value['intervals'].append(dict(cpu_delta(before, after), before=before, after=after))
            write_json(output / 'environment.json', value)
            before = after
        value['processes_after'] = process_snapshot()
        value['status'] = 'low_activity_observed' if qualify_idle(value['intervals']) else 'rejected_busy_host'
    except BaseException as error:
        value.update(status='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        value['finished_utc'] = utc()
        write_json(output / 'environment.json', value)
    return value
