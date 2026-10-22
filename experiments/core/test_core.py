"""Hand-authored screen expectations, chunk boundaries, bounds and pipe failure checks."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import binary


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "input.bin"

    def replay(self, data, rows=3, columns=5, history=2, chunk=1):
        self.path.write_bytes(data)
        return subprocess.run([str(binary("replay")), str(self.path), str(rows), str(columns), str(history), str(chunk)],
                              capture_output=True, text=True, encoding="utf-8", timeout=10)

    def expect(self, data, lines, row, column, wrap=False, history=(), discarded=0, limit=2):
        expected = dict(lines=lines, row=row, column=column, wrap_pending=wrap,
                        history=list(history), discarded_history_lines=discarded)
        for chunk in range(1, len(data) + 2):
            with self.subTest(chunk=chunk):
                result = self.replay(data, len(lines), len(lines[0]), limit, chunk)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout)["screen"], expected)

    def test_delayed_wrap(self):
        self.expect(b"ABCDE", ["ABCDE", "     "], 0, 4, True)
        self.expect(b"ABCDEF", ["ABCDE", "F    "], 1, 1)

    def test_cr_cancels_wrap_and_overwrites(self):
        self.expect(b"ABCDE\rZ", ["ZBCDE", "     "], 0, 1)

    def test_lf_preserves_column(self):
        self.expect(b"AB\nC", ["AB   ", "  C  "], 1, 3)

    def test_backspace_and_tab(self):
        self.expect(b"AB\bZ", ["AZ   "], 0, 2)
        self.expect(b"A\tZ", ["A       Z "], 0, 9)

    def test_scroll_bounded_history(self):
        self.expect(b"1\r\n2\r\n3\r\n4\r\n5\r\n", ["5  ", "   "], 1, 0,
                    history=["3  ", "4  "], discarded=2)

    def test_single_row_zero_history(self):
        self.expect(b"AB", ["B"], 0, 0, True, discarded=1, limit=0)

    def test_absolute_and_relative_cursor(self):
        self.expect(b"\x1b[2;3HX\x1b[1AY\x1b[2DZ", ["  ZY ", "  X  ", "     "], 0, 3)
        self.expect(b"\x1b[999;999HX", ["     ", "    X"], 1, 4, True)

    def test_default_zero_params(self):
        self.expect(b"AB\x1b[0DZ\x1b[;HX", ["XZ   "], 0, 1)

    def test_cursor_other_forms(self):
        self.expect(b"\x1b[2d\x1b[3GX\x1b[1;1fY\x1b[B\x1b[CZ", ["Y    ", "  Z  "], 1, 3)

    def test_erase_line_modes(self):
        for mode, expected in [(0, "AB   "), (1, "   DE"), (2, "     ")]:
            self.expect(b"ABCDE\x1b[3G\x1b[" + str(mode).encode() + b"K", [expected], 0, 2)

    def test_erase_display_modes(self):
        data = b"AAAAA\r\nBBBBB\r\nCCCCC\x1b[2;3H"
        for mode, lines in [(0, ["AAAAA", "BB   ", "     "]),
                            (1, ["     ", "   BB", "CCCCC"]),
                            (2, ["     ", "     ", "     "])]:
            self.expect(data + b"\x1b[" + str(mode).encode() + b"J", lines, 1, 2)

    def test_json_escaping(self):
        self.expect(b'"\\', ['"\\   '], 0, 2)

    def test_unsupported_and_truncated_fail(self):
        for data in [b"\x1b", b"\x1b[12;", b"\x1b[38;5;1m", b"\x1b[?1049h", b"\x1b]title\x07",
                     b"\0", b"\x1b[999999999A", b"\x1b[1;2;3H",
                     b"\x1b[" + b"0" * 33 + b"A"]:
            for chunk in (1, 65536):
                result = self.replay(data, chunk=chunk)
                self.assertEqual(result.returncode, 2, data)
                self.assertEqual(result.stdout, "")

    def test_dimensions_guard(self):
        self.assertEqual(self.replay(b"X", rows=0).returncode, 2)
        self.assertEqual(self.replay(b"X", columns=501).returncode, 2)
        self.assertEqual(self.replay(b"X", history=10001).returncode, 2)

    @unittest.skipUnless(os.name == "nt", "Windows anonymous pipe experiment")
    def test_pipe_parser_failure_drains_and_exits(self):
        data = b"\x1b[38;5;1m" + b"A" * 131101
        self.path.write_bytes(data)
        p = subprocess.run([str(binary("receive")), "512", "edge", "yield", str(len(data)), "512", str(self.path)],
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(p.returncode, 1)
        result = json.loads(p.stdout)
        self.assertFalse(result["valid"])
        self.assertEqual(result["consumed"], len(data))
        self.assertTrue(result["parser_error"])
        self.assertNotIn("screen", result)


if __name__ == "__main__":
    unittest.main()
