"""Real Windows shell/pager/editor traces; never substituted with synthetic TUI."""
import json
import copy
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from compare_candidate import candidate_trace_binary
import real_app_matrix
from run import ROOT, trace_binary
from experiments.conpty.run import OwnedJob


@unittest.skipUnless(os.name == "nt", "Windows-owned capture supervisor")
class CaptureSupervisorFailureTests(unittest.TestCase):
    def test_assignment_failure_never_releases_go_and_keeps_original_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "capture"
            process, job = mock.Mock(), mock.Mock()
            original = OSError("private assignment failure")
            process.poll.side_effect = [None, 0, 0]
            process.communicate.return_value = (b"", b"")
            job.assign.side_effect = original
            with mock.patch.object(real_app_matrix, "OwnedJob", return_value=job), \
                 mock.patch.object(real_app_matrix.subprocess, "Popen", return_value=process):
                with self.assertRaises(OSError) as raised:
                    real_app_matrix.run_capture(Path("never-executed.exe"), Path("never-executed-vim.exe"), output)
            self.assertIs(raised.exception, original)
            self.assertTrue(all(not call.args for call in process.communicate.call_args_list))
            self.assertFalse((output / "pager.bin").exists())
            process.kill.assert_called_once()
            job.close.assert_called_once()

    def test_timeout_and_termination_failure_keep_actual_prefix_and_close_owned_job(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "capture"
            prefix = (ROOT / "experiments/core/fixtures/more-pager-360.bin").read_bytes()
            process, job = mock.Mock(), mock.Mock()
            process.pid = 123
            process.poll.side_effect = [None, 0, 0]
            process.communicate.side_effect = [real_app_matrix.subprocess.TimeoutExpired("capture", 60),
                                               (b"captured partial result", b"private timeout detail")]
            job.terminate.side_effect = OSError("private terminate detail")
            job.drain.return_value = {"active": 0, "total": 4, "terminated": 0}
            def start(*args, **kwargs):
                output.mkdir()
                (output / "pager.bin").write_bytes(prefix)
                return process
            with mock.patch.object(real_app_matrix, "OwnedJob", return_value=job), \
                 mock.patch.object(real_app_matrix.subprocess, "Popen", side_effect=start):
                with self.assertRaises(RuntimeError):
                    real_app_matrix.run_capture(Path("never-executed.exe"), Path("never-executed-vim.exe"), output)
            self.assertEqual((output / "pager.bin").read_bytes(), prefix)
            supervision = json.loads((output / "supervision.json").read_text())
            self.assertTrue(supervision["timed_out"])
            self.assertIn("terminate_failed", supervision["cleanup_errors"])
            self.assertNotIn("private terminate", json.dumps(supervision))
            job.close.assert_called_once()


@unittest.skipUnless(os.name == "nt", "Windows native capture gate")
class CaptureNativeGateTests(unittest.TestCase):
    def test_bad_eof_and_oversized_go_leave_no_capture_or_pty_client(self):
        executable = real_app_matrix.capture_binary()
        with tempfile.TemporaryDirectory() as temporary:
            for number, gate in enumerate((b"", b"NOT_GO\n", b"G" * 64 + b"\n")):
                output = Path(temporary) / f"capture-{number}"
                job = OwnedJob()
                process = None
                try:
                    process = real_app_matrix.subprocess.Popen([str(executable), str(output), "never-executed-vim.exe"],
                        stdin=real_app_matrix.subprocess.PIPE, stdout=real_app_matrix.subprocess.PIPE,
                        stderr=real_app_matrix.subprocess.PIPE, creationflags=real_app_matrix.subprocess.CREATE_NO_WINDOW)
                    job.assign(process.pid)
                    process.communicate(gate, timeout=5)
                    self.assertEqual(process.returncode, 2)
                    self.assertFalse(output.exists())
                    accounting = job.drain()
                    self.assertEqual(accounting["active"], 0)
                    self.assertIn(accounting["total"], (1, 2))  # host's own headless console is permitted
                finally:
                    if process is not None and process.poll() is None:
                        job.terminate()
                        process.communicate(timeout=5)
                    job.close()


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
        lifecycle = json.loads((self.artifacts / "suite/capture/pager-lifecycle.json").read_text())
        self.assertTrue(lifecycle["valid"])
        self.assertTrue(lifecycle["output_eof"])
        self.assertEqual(lifecycle["read_error"], 109)
        self.assertEqual(lifecycle["q_bytes"], 1)
        self.assertEqual(lifecycle["active_children_after"], 0)
        supervision = json.loads((self.artifacts / "suite/capture/supervision.json").read_text())
        self.assertFalse(supervision["timed_out"])
        self.assertEqual(supervision["job_after"]["active"], 0)
        self.assertEqual(supervision["cleanup_errors"], [])

    def test_pager_eof_and_prompt_claims_cannot_hide_missing_or_partial_capture(self):
        capture = self.artifacts / "suite/capture"
        raw = (capture / "pager.bin").read_bytes()
        lifecycle = json.loads((capture / "pager-lifecycle.json").read_text())
        with tempfile.TemporaryDirectory(dir=self.artifacts) as temporary:
            out = Path(temporary)
            for field, wrong in (("valid", False), ("output_eof", False), ("read_error", 0),
                                 ("q_bytes", 0), ("forced_reader_cancel", True),
                                 ("output_bytes", len(raw) - 6), ("prompt_end_offset", 9)):
                bad = copy.deepcopy(lifecycle); bad[field] = wrong
                (out / "pager.bin").write_bytes(raw)
                (out / "pager-lifecycle.json").write_text(json.dumps(bad))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    real_app_matrix.validate_pager_capture(out)
            (out / "pager.bin").write_bytes(raw[:100])
            (out / "pager-lifecycle.json").write_text(json.dumps(lifecycle))
            with self.assertRaises(ValueError):
                real_app_matrix.validate_pager_capture(out)

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
