"""Real Windows shell/pager/editor traces; never substituted with synthetic TUI."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from compare_candidate import candidate_trace_binary
import real_app_matrix
from run import ROOT, trace_binary


class RealApplicationMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "nt":
            raise unittest.SkipTest("real application matrix currently requires Windows ConPTY")
        cls.core = trace_binary()
        try:
            cls.candidate = candidate_trace_binary()
            cls.capture = real_app_matrix.capture_binary()
        except FileNotFoundError:
            raise unittest.SkipTest("optional real application capture dependencies are not built")
        vim = real_app_matrix.vim_executable()
        if not vim or not Path("C:/Windows/System32/more.com").is_file():
            raise unittest.SkipTest("real application matrix requires more.com and Vim")
        cls.vim = Path(vim).resolve(strict=True)
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "real-applications"
        base.mkdir(parents=True, exist_ok=True)
        cls.artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        cls.summary = real_app_matrix.run_suite(cls.core, cls.candidate, cls.capture, cls.vim,
                                                cls.artifacts / "suite")

    def test_real_lifecycle_and_core_comparison(self):
        self.assertTrue(self.summary["execution_passed"], self.summary["counts"])
        self.assertEqual(self.summary["runs"], 3)
        self.assertEqual(self.summary["counts"], {"match": 3})
        self.assertTrue(self.summary["real_application_covered"])
        self.assertFalse(self.summary["semantic_acceptance"])
        self.assertIsNone(self.summary["presented_done"])
        manifest = self.summary["capture_manifest"]
        self.assertEqual((manifest["shell_exit"], manifest["pager_exit"], manifest["editor_exit"]), (0, 0, 0))
        self.assertTrue(manifest["editor_file_verified"])
        self.assertEqual(Path(manifest["editor_path"]), self.vim)
        self.assertTrue(manifest["startup_event_verified"])
        self.assertTrue(manifest["insert_event_verified"])
        self.assertEqual((self.artifacts / "suite/editor-startup.txt").read_bytes(), b"READY\n")
        self.assertEqual((self.artifacts / "suite/editor-insert.txt").read_bytes(), b"INSERT\n")

    def test_failure_preserves_editor_stage_trace_and_cleanup(self):
        executable = Path("C:/Windows/System32/cmd.exe")
        with tempfile.TemporaryDirectory(dir=self.artifacts) as temporary:
            output = Path(temporary) / "capture"
            with self.assertRaisesRegex(RuntimeError, "real application capture failed"):
                real_app_matrix.run_capture(self.capture, executable, output)
            diagnostic = json.loads((output / "editor-failure.json").read_text(encoding="utf-8"))
            self.assertEqual(diagnostic["stage"], "vim-enter")
            self.assertEqual(diagnostic["editor_path"], executable.as_posix())
            self.assertTrue(diagnostic["child_signaled"])
            self.assertEqual(diagnostic["active_children"], 0)
            self.assertTrue(diagnostic["cleanup_complete"])
            self.assertEqual((output / "editor.bin").stat().st_size, diagnostic["editor_bytes"])

    def test_pager_and_editor_match(self):
        for record in self.summary["records"]:
            if record["case"] == "shell":
                continue
            self.assertEqual(record["status"], "match")
            capture = self.artifacts / "suite/capture" / f"{record['case']}.bin"
            replay = self.artifacts / "suite" / record["case"] / "input.bin"
            self.assertEqual(replay.read_bytes(), capture.read_bytes())

    def test_shell_matches_without_history_migration(self):
        record = next(record for record in self.summary["records"] if record["case"] == "shell")
        self.assertEqual(record["status"], "match")
        self.assertIsNone(record["first_difference"])
        self.assertNotIn("decision", record)


if __name__ == "__main__":
    unittest.main()
