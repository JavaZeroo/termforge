"""Exercise the actual replay executable's complete-line command grammar."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import binary


class CandidateResizeReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = binary('candidate_resize_replay')

    def replay(self, plan, payload=b'abc', options=()):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'transport.bin').write_bytes(payload)
            (root / 'plan.txt').write_text(plan, encoding='ascii')
            return subprocess.run([str(self.executable), str(root / 'transport.bin'), str(root / 'plan.txt'), *options],
                                  capture_output=True, timeout=10)

    def test_complete_lines_and_final_line_without_newline_are_valid(self):
        for plan in ('F 0 3\n', 'F 0 3', 'R 12 60\r\nF 0 3\r\n'):
            with self.subTest(plan=plan):
                result = self.replay(plan)
                self.assertEqual(result.returncode, 0, result.stderr)
                states = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(states[-1]['offset'], 3)

    def test_explicit_native_viewport_retains_history_outside_visible_coordinates(self):
        payload = b'H0\r\nH1\r\nTOP\r\nMID\r\nBOT'
        plan = f'R 3 8\nF 0 {len(payload)}\nR 6 8\n'
        for options, expected_top, history in [((), 'H0', []), (('--native-viewport',), 'TOP', ['H0', 'H1'])]:
            with self.subTest(options=options):
                result = self.replay(plan, payload, options)
                self.assertEqual(result.returncode, 0, result.stderr)
                final = json.loads(result.stdout.splitlines()[-1])['state']
                self.assertEqual(final['lines'][0].rstrip(), expected_top)
                self.assertEqual([line.rstrip() for line in final['history']], history)
        self.assertNotEqual(self.replay(plan, payload, ('--unknown',)).returncode, 0)
        self.assertNotEqual(self.replay(plan, payload, ('--native-viewport', 'extra')).returncode, 0)

    def test_trailing_partial_unknown_extra_and_overflow_records_are_rejected(self):
        cases = ('R 12', 'F 3', 'X', 'X 1 2', 'R 12 60 extra', 'F 3 0 extra',
                 'R 12\n60', 'R 12 999999999999999999999999999999999',
                 'F 999999999999999999999999999999999 1', 'R -1 80', 'R +1 80',
                 'R 1.0 80', 'R 0x10 80', 'F 3 1', 'F 0 1', '\n', '   ')
        for tail in cases:
            with self.subTest(tail=tail):
                result = self.replay('F 0 3\n' + tail)
                self.assertNotEqual(result.returncode, 0, 'accepted malformed trailing record')
                self.assertTrue(result.stderr)

    def test_feed_partition_must_cover_all_transport(self):
        for plan in ('', 'R 12 60\n', 'F 0 2\n', 'F 1 2\n', 'F 0 4\n'):
            with self.subTest(plan=plan):
                self.assertNotEqual(self.replay(plan).returncode, 0)


if __name__ == '__main__':
    unittest.main()
