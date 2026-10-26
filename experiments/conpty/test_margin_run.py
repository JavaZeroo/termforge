import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from experiments.build_paths import binary
from experiments.conpty.margin_run import (
    PACKAGE, SOURCE, SOURCE_SHA256, backend_path, margin_result, sha256, validate_lifecycle,
)
from experiments.conpty.run import OwnedJob
from experiments.conpty import margin_run as runner


class MarginCleanupFailureTests(unittest.TestCase):
    def mocks(self):
        process, job = mock.Mock(), mock.Mock()
        process.pid, process.returncode = 123, None
        process.poll.return_value = None
        job.drain.return_value = {"active": 0, "total": 1, "terminated": 0}
        return process, job

    def execute(self, root, process, job):
        with mock.patch.object(runner, "OwnedJob", return_value=job), \
             mock.patch.object(runner.subprocess, "Popen", return_value=process), \
             mock.patch.object(runner.subprocess, "CREATE_NO_WINDOW", 0, create=True), \
             mock.patch.object(runner, "inspect_case", return_value={"synthetic": True}):
            return runner.execute(Path(root) / "never-executed", "system", Path(root) / "unused-trace",
                                  Path(root) / "sample", "vt-on", 80, 24)

    def test_timeout_survives_independent_cleanup_failures_and_saves_unknowns(self):
        process, job = self.mocks()
        original = subprocess.TimeoutExpired("PRIVATE original", 15)
        process.communicate.side_effect = [original, OSError("PRIVATE pipe failure"),
                                           OSError("PRIVATE reap failure")]
        job.terminate.side_effect = OSError("PRIVATE terminate failure")
        job.drain.side_effect = OSError("PRIVATE accounting failure")
        job.close.side_effect = OSError("PRIVATE close failure")
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(subprocess.TimeoutExpired) as raised:
                self.execute(root, process, job)
            self.assertIs(raised.exception, original)
            row = json.loads((Path(root) / "sample/diagnostic.json").read_text())
            self.assertEqual(row["status"], "failed")
            self.assertIsNone(row["outer_job_after"])
            self.assertIsNone(row["process_reaped"])
            for stage in ("process_terminate", "process_communicate", "job_accounting", "job_close",
                          "process_reap_after_close"):
                self.assertIn(stage, row["cleanup_errors"])
            self.assertNotIn("PRIVATE", json.dumps(row))
        job.close.assert_called_once()

    def test_assignment_failure_never_sends_go_and_only_kills_owned_root(self):
        process, job = self.mocks()
        original = OSError("PRIVATE assignment failure")
        job.assign.side_effect = original
        process.poll.side_effect = [None, 99]
        process.communicate.return_value = (b"", b"")
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(OSError) as raised:
                self.execute(root, process, job)
            self.assertIs(raised.exception, original)
            process.kill.assert_called_once()
            row = json.loads((Path(root) / "sample/diagnostic.json").read_text())
            self.assertFalse(row["job_assigned"])
            self.assertTrue(row["process_reaped"])
            self.assertTrue(row["streams_drained"])
            self.assertIsNone(row["outer_job_after"])
            self.assertNotIn("PRIVATE", json.dumps(row))
        process.kill.assert_called_once()
        process.communicate.assert_called_once()
        self.assertEqual(process.communicate.call_args.args, ())
        job.terminate.assert_not_called()
        job.drain.assert_not_called()
        job.close.assert_called_once()

    def test_normal_body_cannot_pass_forced_or_unknown_cleanup(self):
        native = dict(waited=True, exit_code=37, read_error=109, capture_overflow=False,
                      forced_reader_cancel=False, forced_process_cancel=False,
                      cleanup_complete=True, active_children_after=0)
        for observation, forced in (({"active": 2, "total": 3, "terminated": 0}, True),
                                    (OSError("PRIVATE query"), False),
                                    ({"active": False, "total": 1, "terminated": 0}, False)):
            with self.subTest(observation=type(observation).__name__), tempfile.TemporaryDirectory() as root:
                process, job = self.mocks()
                process.returncode = 0
                process.poll.return_value = 0
                process.communicate.return_value = (json.dumps(native).encode(), b"")
                job.drain.side_effect = [observation, {"active": 0, "total": 3, "terminated": 2}]
                with self.assertRaises(ValueError):
                    self.execute(root, process, job)
                row = json.loads((Path(root) / "sample/diagnostic.json").read_text())
                self.assertEqual(row["status"], "failed")
                if forced:
                    self.assertEqual(row["outer_job_after"], observation)
                    self.assertEqual(row["outer_job_after_forced_cleanup"]["active"], 0)
                    self.assertTrue(row["forced_outer_cleanup"])
                else:
                    self.assertIsNone(row["outer_job_after"])
                    self.assertIn("job_accounting", row["cleanup_errors"])
                self.assertNotIn("PRIVATE", json.dumps(row))
                job.close.assert_called_once()


class MarginDiagnosticTests(unittest.TestCase):
    def test_original_workload_identity(self):
        import hashlib
        self.assertEqual(len(SOURCE), 175)
        self.assertEqual(hashlib.sha256(SOURCE).hexdigest(), SOURCE_SHA256)
        self.assertEqual(SOURCE.count(b"\x1b[2;3r"), 1)
        self.assertEqual(SOURCE.count(b"\x1b[4;5r"), 1)

    def test_exact_margin_result_and_wrapped_marker(self):
        for columns, rows in ((80, 24), (12, 7)):
            lines = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", ""]
            marker = "TF_MARGIN_DONE"
            lines += [marker[at:at+columns] for at in range(0, len(marker), columns)]
            lines += [""] * (rows - len(lines))
            self.assertTrue(margin_result(lines, columns, rows))
            wrong = lines.copy()
            wrong[3] = "REGION_CIXED"
            self.assertFalse(margin_result(wrong, columns, rows))
            wrong = lines.copy()
            wrong[5] = "TF_MARGIN"
            self.assertFalse(margin_result(wrong, columns, rows))
            self.assertFalse(margin_result(lines + [""], columns, rows))

    def test_lifecycle_requires_exact_observed_types_and_values(self):
        sample = dict(waited=True, exit_code=37, read_error=109, capture_overflow=False,
                      forced_reader_cancel=False, forced_process_cancel=False,
                      cleanup_complete=True, active_children_after=0, side_by_side=False, creation_flags=0)
        validate_lifecycle(sample)
        for key, value in sample.items():
            for replacement in (None, not value if type(value) is bool else value + 1,
                                int(value) if type(value) is bool else False):
                wrong = copy.deepcopy(sample)
                wrong[key] = replacement
                with self.subTest(key=key, replacement=replacement), self.assertRaises(ValueError):
                    validate_lifecycle(wrong)
            wrong = copy.deepcopy(sample)
            del wrong[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_lifecycle(wrong)

    def test_unreviewed_backend_pair_is_rejected(self):
        self.assertEqual(backend_path("system"), "system")
        with tempfile.TemporaryDirectory() as root:
            dll = Path(root) / "conpty.dll"
            dll.write_bytes(b"unreviewed")
            (dll.parent / "OpenConsole.exe").write_bytes(b"unreviewed")
            self.assertNotEqual(sha256(dll), PACKAGE["files"]["conpty.dll"])
            with self.assertRaisesRegex(ValueError, "identity"):
                backend_path(dll)

    @unittest.skipUnless(os.name == "nt", "Windows native diagnostic")
    def test_native_eof_and_invalid_gate_never_start_probe(self):
        executable = binary("conpty_margin_probe")
        with tempfile.TemporaryDirectory() as root:
            for index, gate in enumerate((b"", b"NO\n", b"GOXX")):
                out = Path(root) / str(index)
                result = subprocess.run([str(executable), "system", "vt-on", str(out), "80", "24"],
                                        input=gate, capture_output=True, timeout=5,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"GO required", result.stderr)
                self.assertFalse(out.exists())

    @unittest.skipUnless(os.name == "nt", "Windows native diagnostic")
    def test_gate_wait_is_bounded_by_owned_job(self):
        with tempfile.TemporaryDirectory() as root:
            out = Path(root) / "absent"
            job = OwnedJob()
            process = subprocess.Popen([str(binary("conpty_margin_probe")), "system", "vt-on", str(out), "80", "24"],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                job.assign(process.pid)
                with self.assertRaises(subprocess.TimeoutExpired):
                    process.wait(timeout=0.05)
                self.assertFalse(out.exists())
                job.terminate()
                process.communicate(timeout=5)
                self.assertEqual(job.drain()["active"], 0)
                self.assertFalse(out.exists())
            finally:
                if process.poll() is None:
                    job.terminate()
                    process.communicate(timeout=5)
                job.close()


if __name__ == "__main__":
    unittest.main()
