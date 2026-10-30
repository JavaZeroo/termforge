"""Independent UTF-8, wide-cell and style expectations across every fixed chunk size."""
import json
import subprocess
import tempfile
from pathlib import Path
import unittest

from experiments.build_paths import binary


def style(fg=-1, bg=-1, bold=False):
    return dict(fg=fg, bg=bg, bold=bold)


def cell(row, col, text, width=1, **attributes):
    return dict(row=row, column=col, text=text, width=width, style=style(**attributes))


class UnicodeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "input.bin"

    def run_input(self, data, chunk, rows=2, cols=6):
        self.path.write_bytes(data)
        return subprocess.run([str(binary("replay")), str(self.path), str(rows), str(cols), "2", str(chunk)],
                              capture_output=True, text=True, encoding="utf-8", timeout=10)

    def expect(self, text, lines, col, row=0, wrap=False, details=(), hist=(), hdetails=(), current=None, cols=6):
        data = text.encode()
        expected = dict(lines=lines, row=row, column=col, wrap_pending=wrap,
                        history=list(hist), discarded_history_lines=0)
        if details: expected["cell_details"] = list(details)
        if hdetails: expected["history_cell_details"] = list(hdetails)
        if current: expected["current_style"] = current
        for chunk in range(1, len(data) + 2):
            with self.subTest(chunk=chunk):
                p = self.run_input(data, chunk, len(lines), cols)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertEqual(json.loads(p.stdout)["screen"], expected)

    def test_cjk_width_and_ascii(self):
        self.expect("中A", ["中A   ", "      "], 3,
                    details=[cell(0, 0, "中", 2), cell(0, 1, "", 0)])

    def test_four_byte_scalar(self):
        self.expect("😀", ["😀    "], 2, details=[cell(0, 0, "😀", 2), cell(0, 1, "", 0)])

    def test_ambiguous_is_narrow(self):
        self.expect("α", ["α     "], 1, details=[cell(0, 0, "α")])

    def test_combining_at_margin(self):
        self.expect("ABe\u0301X", ["ABe\u0301", "X  "], 1, row=1, cols=3,
                    details=[cell(0, 2, "e\u0301")])

    def test_wide_wrap_when_one_column_left(self):
        self.expect("ABCDE中", ["ABCDE ", "中    "], 2, row=1,
                    details=[cell(1, 0, "中", 2), cell(1, 1, "", 0)])

    def test_overwrite_wide_trailer(self):
        self.expect("中\x1b[2GX", [" X    "], 2)

    def test_overwrite_wide_lead(self):
        self.expect("中\rX", ["X     "], 1)

    def test_erase_wide_trailer(self):
        self.expect("中A\x1b[2G\x1b[K", ["      "], 1)

    def test_sgr_and_reset(self):
        self.expect("\x1b[1;31;44mA\x1b[0mB", ["AB    "], 2,
                    details=[cell(0, 0, "A", fg=1, bg=4, bold=True)])

    def test_bright_and_selective_reset(self):
        self.expect("\x1b[1;91;104mA\x1b[22;39;49mB", ["AB    "], 2,
                    details=[cell(0, 0, "A", fg=9, bg=12, bold=True)])

    def test_sgr_does_not_cancel_wrap(self):
        self.expect("ABC\x1b[31mX", ["ABC", "X  "], 1, row=1, cols=3,
                    details=[cell(1, 0, "X", fg=1)], current=style(fg=1))

    def test_styled_wide_history(self):
        self.expect("\x1b[31m中\x1b[0m\r\nX", ["X     "], 1,
                    hist=["中    "], hdetails=[cell(0, 0, "中", 2, fg=1), cell(0, 1, "", 0, fg=1)])

    def test_erase_uses_current_style(self):
        self.expect("AB\x1b[44m\x1b[2K", ["      "], 2,
                    details=[cell(0, i, " ", bg=4) for i in range(6)], current=style(bg=4))

    def test_invalid_utf8_and_unsupported_sequences(self):
        cases = [b"\x80", b"\xc0\xaf", b"\xe0\x80\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80",
                 b"\xf5\x80\x80\x80", b"\xe4A", b"\xe4\xb8", b"\xe4\x1b[0m",
                 "\u0301".encode(), "A\r\u0301".encode(), ("A" + "\u0301" * 17).encode(),
                 "👩\u200d💻".encode(), "❤️".encode(), "🇨🇳".encode(), "👍🏻".encode(),
                 b"\x1b[38;2;1;2;3m", b"\x1b[8m"]  # underline now works; hidden still does not
        for data in cases:
            for chunk in (1, 2, 3, 65536):
                p = self.run_input(data, chunk)
                self.assertEqual(p.returncode, 2, data)
                self.assertEqual(p.stdout, "")

    def test_wide_cannot_fit_single_column(self):
        self.assertEqual(self.run_input("中".encode(), 1, cols=1).returncode, 2)


if __name__ == "__main__":
    unittest.main()
