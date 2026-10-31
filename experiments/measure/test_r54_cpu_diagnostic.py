"""Process/artifact boundary tests; synthetic CPU work is not a core benchmark."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


class AccountingTests(unittest.TestCase):
    def test_own_interval_excluded_cpu_is_separated_and_negative_balance_is_retained(self):
        from experiments.measure.r54_cpu_diagnostic import partition_cpu
        record = dict(environment=dict(total_seconds=10, busy_seconds=3),
                      child_cpu=dict(total_seconds=2.25), runner_cpu_seconds=.125)
        value = partition_cpu(record, 2)
        self.assertEqual(value['inner_only_balance_seconds'], 1)
        self.assertEqual(value['child_outside_inner_seconds'], .25)
        self.assertEqual(value['unassigned_balance_seconds'], .625)
        self.assertEqual(value['unassigned_balance_percent'], 6.25)
        record['environment']['busy_seconds'] = 2.25
        self.assertEqual(partition_cpu(record, 2)['unassigned_balance_seconds'], -.125)
        with self.assertRaises(ValueError):
            partition_cpu(record, 3)
        for invalid in (float('nan'), float('inf'), -1):
            record['runner_cpu_seconds'] = invalid
            with self.assertRaises(ValueError):
                partition_cpu(record, 2)


def native_environment_supported():
    if os.name != 'nt' or sys.implementation.name != 'cpython' or sys.version_info[:2] != (3, 12):
        return False
    try:
        import psutil
    except ImportError:
        # The general regression job runs this module without the measurement
        # requirements; a missing observer means "not supported here", not a
        # collection error, so the decorator still skips instead of failing.
        return False
    return 0 < (psutil.cpu_count() or 0) <= 64


@unittest.skipUnless(native_environment_supported(), 'native diagnostic requires Windows CPython 3.12, 1..64 CPUs')
class ProcessAccountingTests(unittest.TestCase):
    def test_lifetime_cpu_includes_work_outside_child_reported_interval(self):
        from experiments.measure.r54_cpu_diagnostic import capture_cpu
        code = '''import json,os,time
def burn(seconds):
    start=time.process_time()
    while time.process_time()-start < seconds:
        pass
burn(.15)
start=time.process_time()
burn(.05)
inner=time.process_time()-start
burn(.15)
print(json.dumps({'inner_cpu':inner,'pid':os.getpid()}))
'''
        with tempfile.TemporaryDirectory() as folder:
            # A Windows venv executable is a redirector which starts another
            # process. Use the actual interpreter to test the direct-child seam.
            result = capture_cpu([sys._base_executable, '-c', code], Path(folder), timeout=5)
            self.assertEqual(result['exit_code'], 0, result)
            self.assertNotIn('error', result)
            child = json.loads((Path(folder) / 'stdout.bin').read_bytes())
            self.assertEqual(result['child_pid'], child['pid'])
            self.assertGreaterEqual(result['child_cpu']['total_seconds'] - child['inner_cpu'], .29)
            self.assertGreater(result['child_cpu']['exit_filetime'], result['child_cpu']['creation_filetime'])
            self.assertGreater(result['environment']['total_seconds'], 0)
            self.assertGreaterEqual(result['runner_cpu_seconds'], 0)

    def test_timeout_retains_partial_streams_and_exited_child_accounting(self):
        from experiments.measure.r54_cpu_diagnostic import capture_cpu
        code = "import os,time; os.write(1,b'partial\\xff'); os.write(2,b'error\\xfe'); time.sleep(10)"
        with tempfile.TemporaryDirectory() as folder:
            result = capture_cpu([sys._base_executable, '-c', code], Path(folder), timeout=1)
            self.assertTrue(result['timed_out'])
            self.assertIsNotNone(result['exit_code'])
            self.assertGreater(result['child_cpu']['exit_filetime'], 0)
            self.assertEqual((Path(folder) / 'stdout.bin').read_bytes(), b'partial\xff')
            self.assertEqual((Path(folder) / 'stderr.bin').read_bytes(), b'error\xfe')

    def test_bad_output_stops_diagnostic_without_overwrite_or_cost_summary(self):
        from experiments.measure.r54_cpu_diagnostic import run_observations
        command = [sys._base_executable, '-c', "import os; os.write(1,b'bad\\xff')"]
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'observations'
            context = {'evidence_kind': 'synthetic_process_fixture'}
            result = run_observations({name: command for name in ('baseline', 'candidate')}, output, context)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['attempted'], 1)
            self.assertEqual(result['accounted_observations'], 0)
            self.assertEqual((output / '000/stdout.bin').read_bytes(), b'bad\xff')
            self.assertFalse((output / '001').exists())
            self.assertFalse((output / 'summary.json').exists())
            self.assertEqual(result['performance_gate'], 'not_evaluated')
            original = (output / 'manifest.json').read_bytes()
            with self.assertRaises(FileExistsError):
                run_observations({name: command for name in ('baseline', 'candidate')}, output, context)
            self.assertEqual((output / 'manifest.json').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
