"""Independent raw-output oracle and owned, unattended cmd interrupt gate."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from experiments.build_paths import ROOT, binary
from experiments.conpty.ctrl_c_run import COMPLETE_INPUT, execute, validate_sample, validate_trace
from experiments.conpty import ctrl_c_run as runner


class CtrlCOracleTests(unittest.TestCase):
    def test_command_echo_does_not_establish_marker_execution(self):
        for raw in (b"TF>echo TF_AFTER_IDLE\r\nTF>echo TF_AFTER_FLOOD\r\n",
                    b"TF_AFTER_IDLE\r\nTF_BUSY_42\r\necho TF_AFTER_FLOOD\r\n",
                    b"TF_AFTER_IDLE\r\nTF_BUSY_42\r\nTF_AFTER_FLOOD"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                validate_trace(raw, COMPLETE_INPUT)

    def test_marker_order_and_natural_completion_are_rejected(self):
        for raw in (b"TF_AFTER_IDLE\nTF_BUSY_1000000\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_AFTER_FLOOD\nTF_BUSY_3\n",
                    b"TF_BUSY_3\nTF_AFTER_IDLE\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_BUSY_3\nTF_AFTER_FLOOD\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_AFTER_FLOOD\n"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                validate_trace(raw, COMPLETE_INPUT)

    def test_full_output_lines_and_exact_input_are_required(self):
        raw = (b"TF>echo TF_AFTER_IDLE\r\n\x1b[0mTF_AFTER_IDLE\r\nTF_BUSY_123\r\n"
               b"echo TF_AFTER_FLOOD\r\n\x1b[?25hTF_AFTER_FLOOD\r\n")
        result = validate_trace(raw, COMPLETE_INPUT)
        self.assertEqual(result, dict(idle_marker_lines=1, after_marker_lines=1,
                                      highest_busy=123, busy_lines=1))
        for wrong in (COMPLETE_INPUT.replace(b"1000000", b"1000"),
                      COMPLETE_INPUT.replace(b"\x03echo TF_AFTER_FLOOD", b"\x03\x03echo TF_AFTER_FLOOD"),
                      COMPLETE_INPUT[:-1]):
            with self.assertRaises(ValueError):
                validate_trace(raw, wrong)


@unittest.skipUnless(os.name == "nt", "Windows runner cleanup is mocked without starting native processes")
class CtrlCCleanupFailureTests(unittest.TestCase):
    def mocked_run(self, temporary, process, job, images, *, gate=b"GO\n"):
        exe = Path(temporary) / "mock-probe.exe"
        exe.write_bytes(b"identity-only fixture; never executed")
        out = Path(temporary) / "sample"
        identity = {"cmd.exe": {"sha256": "a" * 64, "fixed_file_version": "1.2.3.4"},
                    "conhost.exe": {"sha256": "b" * 64, "fixed_file_version": "1.2.3.4"}}
        with mock.patch.object(runner, "system_directory", return_value=Path(temporary)), \
             mock.patch.object(runner, "system_identity", return_value=identity), \
             mock.patch.object(runner, "OwnedJob", return_value=job), \
             mock.patch.object(runner, "OwnedImages", return_value=images), \
             mock.patch.object(runner.subprocess, "Popen", return_value=process):
            return runner.execute(exe, out, gate=gate)

    def mocks(self):
        process, job, images = mock.Mock(), mock.Mock(), mock.Mock()
        process.pid, process.returncode = 123, None
        job.accounting.return_value = {"active": 1, "total": 1, "terminated": 0}
        job.drain.return_value = {"active": 0, "total": 1, "terminated": 0}
        images.observed, images.error = {"cmd.exe": False, "conhost.exe": False}, False
        return process, job, images

    def test_terminate_and_reap_failures_preserve_original_and_unknown_accounting(self):
        process, job, images = self.mocks()
        original = OSError("private original termination failure")
        process.poll.return_value = None
        process.communicate.side_effect = [runner.subprocess.TimeoutExpired("private-command", 1)] * 3
        job.terminate.side_effect = [original, OSError("private cleanup termination failure")]
        job.drain.side_effect = OSError("private accounting failure")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(OSError) as raised:
                self.mocked_run(temporary, process, job, images)
            self.assertIs(raised.exception, original)
            row = json.loads((Path(temporary) / "sample/sample.json").read_text())
            self.assertEqual(row["status"], "failed")
            self.assertEqual(row["failure_code"], "cleanup_failed")
            self.assertIsNone(row["job_after_cleanup"])
            self.assertIsNone(row["process_reaped"])
            self.assertIn("process_terminate", row["cleanup_errors"])
            self.assertIn("process_communicate", row["cleanup_errors"])
            self.assertIn("job_accounting", row["cleanup_errors"])
            self.assertNotIn("private original", json.dumps(row))
            self.assertIn("private original", (Path(temporary) / "sample/_private/exception.txt").read_text())
            with self.assertRaises(ValueError):
                validate_sample(row)
        images.finish.assert_called_once()
        job.close.assert_called_once()

    def test_sampler_failure_cannot_skip_job_close_or_public_evidence(self):
        process, job, images = self.mocks()
        original = ValueError("private original protocol failure")
        process.poll.side_effect = [None, 0, 0]
        process.communicate.side_effect = [original, runner.subprocess.TimeoutExpired("private-command", 1)]
        images.finish.side_effect = OSError("private sampler failure")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError) as raised:
                self.mocked_run(temporary, process, job, images)
            self.assertIs(raised.exception, original)
            row = json.loads((Path(temporary) / "sample/sample.json").read_text())
            self.assertIn("image_sampler_finish", row["cleanup_errors"])
            self.assertEqual(row["job_after_cleanup"]["active"], 0)
            self.assertTrue(row["process_reaped"])
            self.assertEqual(row["status"], "failed")
        job.close.assert_called_once()

    def test_cleanup_fault_after_normal_body_return_is_raised_and_never_accepted(self):
        process, job, images = self.mocks()
        process.returncode = 2
        process.poll.side_effect = [None, 0, 0]
        process.communicate.side_effect = [(b"guard rejected", b""),
                                           runner.subprocess.TimeoutExpired("private-command", 1)]
        job.terminate.side_effect = OSError("private cleanup termination failure")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "^cleanup_failed$"):
                self.mocked_run(temporary, process, job, images, gate=b"")
            row = json.loads((Path(temporary) / "sample/sample.json").read_text())
            self.assertEqual(row["failure_code"], "cleanup_failed")
            self.assertEqual(row["status"], "failed")
            self.assertEqual(row["cleanup_errors"], ["process_terminate", "process_communicate"])
            self.assertEqual(len(row["probe_sha256"]), 64)
            with self.assertRaises(ValueError):
                validate_sample(row)
        images.finish.assert_called()
        job.close.assert_called_once()


@unittest.skipUnless(os.name == "nt", "raw ConPTY requires Windows")
class CtrlCIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("conpty_ctrl_c_probe")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "conpty-ctrl-c"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_original_cmd_flood_interrupt_and_cleanup_three_fresh(self):
        for repeat in range(3):
            row = execute(self.exe, self.out / f"cmd-{repeat}")
            self.assertEqual(row["status"], "passed")
            self.assertFalse(row["handler_delivery_verified"])
            self.assertEqual(row["job_after_cleanup"]["active"], 0)
            for key, value in (("after_marker", False), ("highest_busy", 1000000),
                               ("busy_hold_ms", 999), ("flood_input_bytes", 20),
                               ("exit_code", 0), ("cleanup_complete", False),
                               ("forced_reader_cancel", True)):
                bad = copy.deepcopy(row)
                bad["native"][key] = value
                with self.subTest(field=key), self.assertRaises(ValueError):
                    validate_sample(bad)
            for key, value in (("image_query_error", True), ("owned_images", {"cmd.exe": True}),
                               ("job_after", {"active": 1}), ("timed_out", True)):
                bad = copy.deepcopy(row)
                bad[key] = value
                with self.subTest(field=key), self.assertRaises(ValueError):
                    validate_sample(bad)

    def test_eof_and_wrong_gate_cannot_start_conpty(self):
        for number, gate in enumerate((b"", b"NOT_GO\n", b"G" * 64 + b"\n")):
            out = self.out / f"guard-{number}"
            row = execute(self.exe, out, gate=gate)
            self.assertEqual(row["returncode"], 2)
            # CREATE_NO_WINDOW can create the console host's own headless
            # conhost. A PTY client or an additional ConPTY is not permitted.
            self.assertIn(row["job_after"]["total"], (1, 2))
            self.assertEqual(row["job_after_cleanup"]["active"], 0)
            self.assertFalse(row["owned_images"]["cmd.exe"])
            self.assertFalse((out / "_private/input.bin").exists())
            self.assertFalse((out / "_private/output.bin").exists())
            native = json.loads((out / "_private/stdout.json").read_bytes())
            self.assertEqual(native["status"], "guard_rejected")

    def test_withheld_gate_timeout_recovers_only_owned_host(self):
        row = execute(self.exe, self.out / "withheld-go", gate=None, timeout=0.3)
        self.assertTrue(row["timed_out"])
        self.assertIn(row["job_at_timeout"]["total"], (1, 2))
        self.assertEqual(row["job_at_timeout"]["active"], row["job_at_timeout"]["total"])
        self.assertEqual(row["job_at_timeout"]["terminated"], 0)
        self.assertFalse(row["owned_images"]["cmd.exe"])
        self.assertEqual(row["job_after_cleanup"]["active"], 0)
        self.assertFalse((self.out / "withheld-go/_private/input.bin").exists())


if __name__ == "__main__":
    unittest.main()
