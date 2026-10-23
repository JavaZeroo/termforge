"""Real shell messages/output/frame/lifecycle regression; all windows are owned."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.shell.run import execute, validate


@unittest.skipUnless(os.name == "nt", "ConPTY shell requires Windows")
class ShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "shell"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_commands_unicode_editing_interrupt_and_normal_exit(self):
        for repeat in range(3):
            result = execute(self.exe, "normal", self.out / f"normal-{repeat}")["result"]
            for field, wrong in (("ctrl_c_verified", False), ("active_children_after", 1),
                                 ("output_queue_high_water", 262145), ("input_rejected", 1),
                                 ("max_consumed_per_tick", 65537),
                                 ("presented_done", 1)):
                broken = copy.deepcopy(result)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "normal")

    def test_close_during_continuous_output(self):
        for repeat in range(3):
            execute(self.exe, "close", self.out / f"close-{repeat}")

    def test_resize_child_dimensions_and_interrupt(self):
        for repeat in range(3):
            row = execute(self.exe, "resize", self.out / f"resize-{repeat}")["result"]
            for field, wrong in (("resize_verified", 2), ("columns", 80), ("rows", 24)):
                broken = copy.deepcopy(row); broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "resize")

    def test_close_while_resizing_continuous_output(self):
        for repeat in range(3):
            execute(self.exe, "resize-close", self.out / f"resize-close-{repeat}")

    def test_watchdog_contains_real_shell_tree(self):
        execute(self.exe, "watchdog", self.out / "watchdog", timeout=3)

    def test_queues_apply_bounded_backpressure_and_reject_input(self):
        result = subprocess.run([str(binary("shell_queue_probe"))], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_arguments_create_no_session(self):
        result = subprocess.run([str(self.exe), "--test", "invalid", "unused.bmp"],
                                capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"invalid shell test mode", result.stderr)


if __name__ == "__main__":
    unittest.main()
