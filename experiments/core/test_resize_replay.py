"""Resize-aware replay: one capture read across an explicit geometry timeline."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import binary


class ResizeReplayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        folder = Path(self.temp.name)
        self.input = folder / "input.bin"
        self.steps = folder / "steps.txt"

    def run_steps(self, data, steps, history=100):
        self.input.write_bytes(data)
        self.steps.write_text("".join(f"{rows} {columns} {count} {label}\n"
                                      for rows, columns, count, label in steps), encoding="utf-8")
        completed = subprocess.run(
            [str(binary("resize_replay")), str(self.input), str(self.steps), str(history)],
            capture_output=True, text=True, encoding="utf-8", timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return [json.loads(line) for line in completed.stdout.splitlines()]

    def test_resize_between_chunks_moves_the_viewport_and_grows_history(self):
        data = "".join(f"L{index:02d}\r\n" for index in range(1, 21)).encode()
        split = len("".join(f"L{index:02d}\r\n" for index in range(1, 13)).encode())
        rows = self.run_steps(data, [(3, 40, split, "small"), (5, 40, len(data) - split, "grown")])
        first, second = rows[0], rows[1]
        self.assertEqual((first["rows"], second["rows"]), (3, 5))
        self.assertTrue(first["following"] and second["following"])
        self.assertEqual(second["total_consumed"], len(data))
        self.assertGreater(second["history"], first["history"])
        # The viewport follows the bottom, so retained rows are history plus one page.
        self.assertEqual(second["retained"], second["history"] + 5)
        self.assertEqual(first["retained"], first["history"] + 3)

    def test_identical_bytes_scale_their_scrollback_with_page_height(self):
        # #194 measured this on a real capture: the same repaint bytes pushed 30
        # rows into scrollback behind a 7-row page and none behind a 24-row one.
        data = "".join(f"R{index:02d}\r\n" for index in range(1, 21)).encode()
        short, medium, tall = (self.run_steps(data, [(rows, 20, len(data), f"rows-{rows}")])[0]
                               for rows in (4, 10, 24))
        self.assertEqual((short["history"], medium["history"], tall["history"]), (17, 11, 0))
        for row in (short, medium, tall):
            self.assertEqual(row["retained"], row["history"] + row["rows"])

    def test_a_step_longer_than_the_capture_consumes_only_what_is_left(self):
        rows = self.run_steps(b"only\r\n", [(2, 20, 100, "overreach")])
        self.assertEqual(rows[0]["consumed"], len(b"only\r\n"))
        self.assertEqual(rows[0]["total_consumed"], len(b"only\r\n"))

    def test_malformed_steps_are_rejected_before_any_parsing(self):
        self.input.write_bytes(b"x")
        self.steps.write_text("not a step\n", encoding="utf-8")
        completed = subprocess.run(
            [str(binary("resize_replay")), str(self.input), str(self.steps), "10"],
            capture_output=True, text=True, encoding="utf-8", timeout=10)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("malformed step", completed.stderr)


if __name__ == "__main__":
    unittest.main()
