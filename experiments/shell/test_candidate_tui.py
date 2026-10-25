import os
from pathlib import Path
import tempfile
import unittest

from experiments.build_paths import binary
from experiments.shell.run import execute


class CandidateTuiTests(unittest.TestCase):
    def setUp(self):
        root = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS",
                                  Path(__file__).resolve().parents[2] / "build/test-artifacts"))
        base = root / "candidate-shell"
        base.mkdir(parents=True, exist_ok=True)
        self.out = Path(tempfile.mkdtemp(prefix="run-", dir=base))

    def test_escape_after_resize(self):
        for repeat in range(2):
            out = self.out / f"candidate-escape-{repeat}"
            execute(binary("termforge_shell"), "candidate-input-escape", out, timeout=20)
            self.assertEqual((out / "candidate-escape.bin").read_bytes(), b"\x1b")

    def test_real_fullscreen_editor_and_cursor_reply(self):
        executable = binary("termforge_shell")
        for repeat in range(2):
            out = self.out / f"candidate-tui-{repeat}"
            result = execute(executable, "candidate-tui", out, timeout=40)["result"]
            self.assertTrue(result["candidate_tui_start"])
            self.assertTrue(result["candidate_tui"])
            self.assertTrue(result["candidate_replies"])
            self.assertTrue(result["candidate_resize"])
            self.assertIn("TF_CANDIDATE_TUI", (out / "candidate-tui.txt").read_text(encoding="utf-8"))
            script = (out / "candidate-input.bin").read_bytes()
            self.assertTrue(script.startswith(b"iTF_CANDIDATE_TUI"))
            self.assertTrue(script.endswith(b":wq\r"))
            output = (out / "candidate-output.bin").read_bytes()
            self.assertTrue(output)
            self.assertEqual((out / "candidate-startup-log.txt").read_bytes(), b"READY\n")
            self.assertEqual((out / "candidate-resize-log.txt").read_bytes(), b"60x12")
            self.assertEqual((out / "candidate-insert-log.txt").read_text(encoding="utf-8"),
                             "INSERT\n" + "\n".join("TF_CANDIDATE_TUI") + "\nLEAVE\nINSERT\nLEAVE\n")
            replies = (out / "candidate-replies.bin").read_bytes()
            self.assertRegex(replies, rb"^\x1b\[[0-9]+;[0-9]+R$", replies)
            self.assertIsNone(result["presented_done"])


if __name__ == "__main__":
    unittest.main()
