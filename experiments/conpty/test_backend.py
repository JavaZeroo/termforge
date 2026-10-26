"""Owned, actual API-family/lifetime checks; no desktop or installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest import mock

from experiments.build_paths import ROOT, binary
from experiments.conpty.run import OwnedJob


@unittest.skipUnless(os.name == "nt", "Windows ConPTY backend")
class BackendTests(unittest.TestCase):
    def setUp(self):
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "backend"
        base.mkdir(parents=True, exist_ok=True)
        self.out = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        self.serial = 0

    def invoke(self, directory, mode="--host"):
        job = process = None
        stdout = stderr = b""
        result = {"status": "failed", "failure_code": None, "returncode": None,
                  "outer_job_after": None, "outer_job_after_forced_cleanup": None,
                  "forced_cleanup": False, "job_assigned": False,
                  "process_reaped": None, "streams_drained": False, "cleanup_errors": []}
        case = self.out / f"case-{self.serial}"
        self.serial += 1
        private = case / "_private"
        # Prepare storage before acquiring any process/Job resource.
        private.mkdir(parents=True)
        stage = "job_create"

        def cleanup_fault(code):
            result["cleanup_errors"].append(code)
            result["status"] = "failed"

        try:
            job = OwnedJob()
            stage = "process_start"
            process = subprocess.Popen([str(binary("conpty_backend_probe")), mode, str(directory)],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            stage = "job_assign"
            job.assign(process.pid)
            result["job_assigned"] = True
            stage = "process_communicate"
            stdout, stderr = process.communicate(b"GO\n", timeout=20)
            result["streams_drained"] = True
            stage = "stream_evidence"
            (private / "host.stdout.bin").write_bytes(stdout)
            (private / "host.stderr.bin").write_bytes(stderr)
            result["returncode"] = process.returncode
            stage = "native_report"
            result["native"] = json.loads(stdout) if stdout else None
            result["error"] = "identity_rejected" if stderr else None
            result["status"] = "completed"
        except BaseException:
            result["failure_code"] = stage
            raise
        finally:
            # One deadline covers all cleanup waits. Each operation may fail
            # independently without replacing the original body exception.
            deadline = time.monotonic() + 5

            def job_observation():
                observed = job.drain(timeout=max(0, deadline - time.monotonic()))
                if (type(observed) is not dict or set(observed) != {"active", "total", "terminated"}
                        or any(type(value) is not int or value < 0 for value in observed.values())):
                    raise ValueError("invalid_job_accounting")
                return observed

            try:
                if process is not None:
                    try:
                        running = process.poll() is None
                    except Exception:
                        running = True
                        cleanup_fault("process_poll")
                    if running:
                        result["forced_cleanup"] = True
                        result["status"] = "failed"
                        try:
                            if result["job_assigned"]:
                                job.terminate()
                            else:
                                process.kill()  # This owned root is still waiting for GO.
                        except Exception:
                            cleanup_fault("process_terminate")
                        try:
                            stdout, stderr = process.communicate(timeout=max(0, deadline - time.monotonic()))
                            result["streams_drained"] = True
                        except Exception:
                            cleanup_fault("process_communicate")
                if job is not None and result["job_assigned"]:
                    try:
                        result["outer_job_after"] = job_observation()
                    except Exception:
                        cleanup_fault("job_accounting")
                    if result["outer_job_after"] is not None and result["outer_job_after"]["active"]:
                        result["forced_cleanup"] = True
                        result["status"] = "failed"
                        try:
                            job.terminate()
                        except Exception:
                            cleanup_fault("job_terminate")
                        try:
                            result["outer_job_after_forced_cleanup"] = job_observation()
                            if result["outer_job_after_forced_cleanup"]["active"]:
                                cleanup_fault("job_not_empty")
                        except Exception:
                            cleanup_fault("job_accounting_after_forced_cleanup")
            finally:
                if job is not None:
                    try:
                        job.close()  # Kill-on-close remains the final containment fallback.
                    except Exception:
                        cleanup_fault("job_close")
            if process is not None:
                try:
                    if not result["streams_drained"]:
                        stdout, stderr = process.communicate(timeout=max(0, deadline - time.monotonic()))
                        result["streams_drained"] = True
                    result["process_reaped"] = process.poll() is not None
                    if not result["process_reaped"]:
                        cleanup_fault("process_not_reaped")
                except Exception:
                    cleanup_fault("process_reap_after_close")
            for name, contents in (("host.stdout.bin", stdout), ("host.stderr.bin", stderr)):
                try:
                    (private / name).write_bytes(contents)
                except Exception:
                    cleanup_fault("stream_evidence_write")
            if (result["cleanup_errors"] or result["forced_cleanup"] or result["outer_job_after"] is None
                    or result["process_reaped"] is not True or not result["streams_drained"]):
                result["status"] = "failed"
                if result["failure_code"] is None:
                    result["failure_code"] = "outer_cleanup_incomplete"
            try:
                (case / "summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            except Exception:
                cleanup_fault("summary_write")
        if result["status"] != "completed":
            raise ValueError("outer_cleanup_incomplete")
        return result

    def check_lifecycle(self, sample, side_by_side):
        self.assertEqual(sample["returncode"], 0)
        self.assertEqual(sample["native"], {
            "valid": True, "side_by_side": side_by_side, "creation_flags": 24 if side_by_side else 0,
            "loaded_during_session": side_by_side, "loaded_after_close": side_by_side,
            "module_unloaded_after_session": True, "exit_code": 37,
            "active_children_after": 0, "read_error": 109, "cleanup_complete": True,
            "forced_reader_cancel": False, "forced_process_cancel": False,
        })

    def selected_directory(self):
        if os.environ.get("TERMFORGE_EXPECT_CONPTY_BACKEND") != "reviewed-side-by-side-1.25.260930003":
            self.skipTest("explicit reviewed backend build not selected")
        return binary("conpty_backend_probe").parent / "conpty-backend"

    def test_system_create_resize_and_close(self):
        self.check_lifecycle(self.invoke("system"), False)

    def test_selected_module_survives_session_and_unloads_after_close(self):
        self.check_lifecycle(self.invoke(self.selected_directory()), True)

    def test_relative_directory_is_rejected(self):
        result = self.invoke("conpty-backend", "--identity")
        self.assertEqual(result["returncode"], 2)
        self.assertEqual(result["error"], "identity_rejected")

    def test_missing_pair_is_rejected(self):
        result = self.invoke(self.out, "--identity")
        self.assertEqual(result["returncode"], 2)

    def test_unreviewed_pair_is_rejected(self):
        (self.out / "conpty.dll").write_bytes(b"unreviewed dll")
        (self.out / "OpenConsole.exe").write_bytes(b"unreviewed host")
        result = self.invoke(self.out, "--identity")
        self.assertEqual(result["returncode"], 2)

    def test_either_selected_file_tamper_is_rejected(self):
        selected = self.selected_directory()
        for index, name in enumerate(("conpty.dll", "OpenConsole.exe")):
            directory = self.out / f"tamper-{index}"
            directory.mkdir()
            for member in ("conpty.dll", "OpenConsole.exe"):
                shutil.copyfile(selected / member, directory / member)
            payload = bytearray((directory / name).read_bytes())
            payload[-1] ^= 1
            (directory / name).write_bytes(payload)
            with self.subTest(file=name):
                result = self.invoke(directory, "--identity")
                self.assertEqual(result["returncode"], 2)


class BackendCleanupFailureTests(unittest.TestCase):
    """Pure supervision failures also run on non-Windows CI."""

    def mocks(self):
        process, job = mock.Mock(), mock.Mock()
        process.pid, process.returncode = 123, None
        process.poll.return_value = None
        job.drain.return_value = {"active": 0, "total": 1, "terminated": 0}
        return process, job

    def invoke(self, root, process, job):
        subject = BackendTests()
        subject.out, subject.serial = Path(root), 0
        with mock.patch(__name__ + ".OwnedJob", return_value=job), \
             mock.patch(__name__ + ".binary", return_value=Path(root) / "never-executed"), \
             mock.patch.object(subprocess, "Popen", return_value=process), \
             mock.patch.object(subprocess, "CREATE_NO_WINDOW", 0, create=True):
            return subject.invoke("system")

    def summary(self, root):
        row = json.loads((Path(root) / "case-0/summary.json").read_text("utf-8"))
        self.assertNotIn("PRIVATE", json.dumps(row))
        return row

    def normal_body(self, process):
        process.returncode = 0
        process.poll.return_value = 0
        process.communicate.return_value = (b"{}", b"")

    def test_timeout_survives_all_cleanup_failures_and_saves_unknowns(self):
        process, job = self.mocks()
        original = subprocess.TimeoutExpired("PRIVATE original", 20)
        process.communicate.side_effect = [original, OSError("PRIVATE pipe"), OSError("PRIVATE reap")]
        job.terminate.side_effect = OSError("PRIVATE terminate")
        job.drain.side_effect = OSError("PRIVATE accounting")
        job.close.side_effect = OSError("PRIVATE close")
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(subprocess.TimeoutExpired) as raised:
                self.invoke(root, process, job)
            self.assertIs(raised.exception, original)
            row = self.summary(root)
            self.assertEqual(row["status"], "failed")
            self.assertEqual(row["failure_code"], "process_communicate")
            self.assertIsNone(row["returncode"])
            self.assertIsNone(row["outer_job_after"])
            self.assertIsNone(row["process_reaped"])
            for code in ("process_terminate", "process_communicate", "job_accounting", "job_close",
                         "process_reap_after_close"):
                self.assertIn(code, row["cleanup_errors"])
        job.close.assert_called_once()

    def test_assignment_failure_never_sends_go_and_kills_only_owned_root(self):
        process, job = self.mocks()
        original = OSError("PRIVATE assignment")
        job.assign.side_effect = original
        process.poll.side_effect = [None, 99]
        process.communicate.return_value = (b"", b"")
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(OSError) as raised:
                self.invoke(root, process, job)
            self.assertIs(raised.exception, original)
            row = self.summary(root)
            self.assertFalse(row["job_assigned"])
            self.assertTrue(row["process_reaped"])
            self.assertTrue(row["streams_drained"])
            self.assertIsNone(row["outer_job_after"])
        process.kill.assert_called_once()
        process.communicate.assert_called_once()
        self.assertEqual(process.communicate.call_args.args, ())
        job.terminate.assert_not_called()
        job.drain.assert_not_called()
        job.close.assert_called_once()

    def test_first_nonzero_accounting_is_preserved_and_cannot_pass(self):
        process, job = self.mocks()
        self.normal_body(process)
        first = {"active": 2, "total": 3, "terminated": 0}
        after = {"active": 0, "total": 3, "terminated": 2}
        job.drain.side_effect = [first, after]
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "outer_cleanup_incomplete"):
                self.invoke(root, process, job)
            row = self.summary(root)
            self.assertEqual(row["outer_job_after"], first)
            self.assertEqual(row["outer_job_after_forced_cleanup"], after)
            self.assertTrue(row["forced_cleanup"])
            self.assertEqual(row["status"], "failed")
        job.close.assert_called_once()

    def test_invalid_or_unavailable_accounting_stays_unknown_and_cannot_pass(self):
        for observation in (OSError("PRIVATE query"), {"active": False, "total": 1, "terminated": 0},
                            {"active": 0, "total": 1}):
            with self.subTest(observation=type(observation).__name__), tempfile.TemporaryDirectory() as root:
                process, job = self.mocks()
                self.normal_body(process)
                job.drain.side_effect = [observation]
                with self.assertRaisesRegex(ValueError, "outer_cleanup_incomplete"):
                    self.invoke(root, process, job)
                row = self.summary(root)
                self.assertIsNone(row["outer_job_after"])
                self.assertEqual(row["status"], "failed")
                self.assertIn("job_accounting", row["cleanup_errors"])
                job.close.assert_called_once()

    def test_directory_preparation_does_not_acquire_job(self):
        _, job = self.mocks()
        original = OSError("PRIVATE directory")
        with tempfile.TemporaryDirectory() as root, mock.patch.object(Path, "mkdir", side_effect=original), \
             mock.patch(__name__ + ".OwnedJob", return_value=job) as acquire:
            subject = BackendTests()
            subject.out, subject.serial = Path(root), 0
            with self.assertRaises(OSError) as raised:
                subject.invoke("system")
            self.assertIs(raised.exception, original)
            acquire.assert_not_called()
        job.close.assert_not_called()

    def test_evidence_write_failure_still_closes_job_and_preserves_original(self):
        process, job = self.mocks()
        self.normal_body(process)
        original = OSError("PRIVATE evidence disk")
        with tempfile.TemporaryDirectory() as root, mock.patch.object(Path, "write_bytes", side_effect=original):
            with self.assertRaises(OSError) as raised:
                self.invoke(root, process, job)
            self.assertIs(raised.exception, original)
            row = self.summary(root)
            self.assertEqual(row["failure_code"], "stream_evidence")
            self.assertIn("stream_evidence_write", row["cleanup_errors"])
            self.assertTrue(row["process_reaped"])
            self.assertEqual(row["outer_job_after"]["active"], 0)
        job.close.assert_called_once()

    def test_summary_write_failure_cannot_pass_or_replace_original_timeout(self):
        for has_timeout in (False, True):
            with self.subTest(has_timeout=has_timeout), tempfile.TemporaryDirectory() as root:
                process, job = self.mocks()
                self.normal_body(process)
                original = subprocess.TimeoutExpired("PRIVATE original", 20)
                if has_timeout:
                    process.poll.side_effect = [None, 99]
                    process.communicate.side_effect = [original, (b"", b"")]
                with mock.patch.object(Path, "write_text", side_effect=OSError("PRIVATE summary disk")) as write:
                    with self.assertRaises(subprocess.TimeoutExpired if has_timeout else ValueError) as raised:
                        self.invoke(root, process, job)
                    if has_timeout:
                        self.assertIs(raised.exception, original)
                    write.assert_called_once()
                    self.assertNotIn("PRIVATE", write.call_args.args[0])
                job.close.assert_called_once()

    def test_cleanup_uses_one_five_second_deadline(self):
        process, job = self.mocks()
        self.normal_body(process)
        process.poll.side_effect = [None, 37]
        job.drain.side_effect = [{"active": 2, "total": 3, "terminated": 0},
                                 {"active": 0, "total": 3, "terminated": 2}]
        with tempfile.TemporaryDirectory() as root, \
             mock.patch(__name__ + ".time.monotonic", side_effect=[100, 101, 102, 103]):
            with self.assertRaisesRegex(ValueError, "outer_cleanup_incomplete"):
                self.invoke(root, process, job)
        self.assertEqual(process.communicate.call_args_list,
                         [mock.call(b"GO\n", timeout=20), mock.call(timeout=4)])
        self.assertEqual(job.drain.call_args_list, [mock.call(timeout=3), mock.call(timeout=2)])
        job.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
