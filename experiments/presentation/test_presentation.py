"""Real owned window, synthetic screenshots, corrupted-gate and timeout checks."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

from experiments.build_paths import binary
from experiments.presentation.run import execute, validate


@unittest.skipUnless(os.name == "nt", "Owned HWND experiment is Windows-only")
class Presentation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("presentation_probe")  # Missing explicit binary must fail, never skip/fallback.

    def scenario(self, backend, dpi, **options):
        destination = os.environ.get("TERMFORGE_TEST_ARTIFACTS")
        if destination:
            return execute(self.exe, backend, dpi, Path(destination) / "presentation" / f"{backend}-{dpi}-{time.time_ns()}", **options)
        # Keep failed synthetic evidence locally; remove successful temporary data only.
        parent = Path("local/experiments/presentation-tests")
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent, ignore_cleanup_errors=True) as temporary:
            try:
                return execute(self.exe, backend, dpi, Path(temporary) / "case", **options)
            except Exception:
                import shutil
                shutil.copytree(temporary, parent / f"failure-{time.time_ns()}")
                raise

    def test_dwrite_window_font_cursor_dirty_and_scale(self):
        for dpi in (96, 144):
            with self.subTest(dpi=dpi):
                row = self.scenario("dwrite", dpi)["result"]
                for field, wrong in (("dwrite_missing_glyphs", 1), ("cursor_verified", False),
                                     ("snapshot_revision", 1), ("presented_done", 1), ("dirty_equals_full", False)):
                    broken = copy.deepcopy(row)
                    broken[field] = wrong
                    with self.assertRaises(ValueError):
                        validate(broken, "dwrite", dpi)

    def test_gdi_window_control(self):
        for dpi in (96, 144):
            with self.subTest(dpi=dpi):
                self.scenario("gdi", dpi)

    def test_timeout_removes_owned_window_and_desktop(self):
        self.scenario("gdi", 96, timeout=3, expect_timeout=True)

    def test_invalid_options_fail_before_window_creation(self):
        for backend, dpi in (("invalid", "96"), ("gdi", "96junk"), ("dwrite", "0")):
            run = subprocess.run([str(self.exe), backend, dpi, "unused.bmp"], capture_output=True, timeout=10,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(run.returncode, 2)
            self.assertIn(b"invalid arguments", run.stderr)


if __name__ == "__main__":
    unittest.main()
