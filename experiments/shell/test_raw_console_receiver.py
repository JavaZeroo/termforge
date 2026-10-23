"""Real owned ConPTY stop tests; no IME, SendInput or language changes."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.conpty.run import OwnedJob


@unittest.skipUnless(os.name == "nt", "raw console lifecycle requires Windows ConPTY")
class RawConsoleReceiverTests(unittest.TestCase):
    def test_stop_policy_rejects_unsolicited_zero_and_unrelated_failures(self):
        result = subprocess.run([str(binary("raw_console_receiver_test")), "--policy"],
                                capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        self.assertEqual(result.stdout.strip(), b"raw receiver stop policy passed")

    def test_empty_unicode_and_repeated_stop_have_exact_bytes_and_owned_cleanup(self):
        executable = binary("raw_console_receiver_test")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "raw-console-receiver"
        parent.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        cases = directory / "cases"
        cases.mkdir()
        record = {"schema": "termforge-data-only-receiver-test-v1", "data_only": True,
                  "ime_or_global_input_used": False, "returncode": None,
                  "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                  "outer_job_after": None, "forced_cleanup": False, "timed_out": False,
                  "process_reaped": False, "error_kind": None, "status": "failed"}
        job = OwnedJob()
        process = None
        stdout = stderr = b""
        try:
            process = subprocess.Popen([str(executable), "--host", str(cases)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            job.assign(process.pid)
            stdout, stderr = process.communicate(b"GO\n", timeout=20)
            record["returncode"] = process.returncode
            record["outer_job_after"] = job.drain(timeout=3)
            self.assertEqual(process.returncode, 0, stderr.decode("utf-8", "replace"))
            self.assertEqual(stderr, b"")
            self.assertEqual(record["outer_job_after"]["active"], 0)
            data = json.loads(stdout)
            self.assertEqual(data["schema"], "termforge-raw-console-lifecycle-v1")
            self.assertIs(data["data_only"], True)
            self.assertIs(data["ime_or_global_input_used"], False)
            self.assertIs(data["passed"], True)
            self.assertEqual([row["name"] for row in data["cases"]], ["empty", "unicode", "repeat-stop"])
            for row in data["cases"]:
                with self.subTest(case=row["name"]):
                    expected = b"" if row["name"] == "empty" else "你好".encode("utf-8")
                    received = (cases / row["name"] / "capture.bin").read_bytes()
                    self.assertEqual(received, expected)
                    self.assertEqual(row["capture_hex"], expected.hex())
                    for flag in ("passed", "child_signaled", "stopped_event", "cleanup_complete"):
                        self.assertIs(row[flag], True)
                    self.assertEqual(row["exit_code"], 0)
                    self.assertEqual(row["inner_job_active_after"], 0)
                    observed = json.loads((cases / row["name"] / "capture.bin.receiver.json").read_bytes())
                    self.assertEqual(observed, row["receiver"])
                    self.assertEqual(observed["state"], "stopped")
                    self.assertIs(observed["stop_requested"], True)
                    self.assertIs(observed["worker_joined"], True)
                    self.assertEqual(observed["worker_error"], 0)
                    self.assertEqual(observed["failure_step"], "")
                    self.assertEqual(observed["unexpected_cancel_error"], 0)
                    self.assertEqual(observed["last_wait_result"], 0)
                    self.assertEqual(observed["last_wait_error"], 0)
                    self.assertEqual(observed["saved_bytes"], len(expected))
                    self.assertEqual(observed["received_units"], 0 if not expected else 2)
                    self.assertEqual(observed["reads_entered"], observed["reads_completed"])
                    self.assertGreaterEqual(observed["reads_entered"], 1 if not expected else 2)
                    self.assertGreaterEqual(observed["cancel_calls"], 1)
                    self.assertGreaterEqual(observed["cancel_successes"], 1)
                    self.assertIn(observed["last_cancel_error"], (0, 1168))
                    normal_zero = observed["last_read_success"] is True and observed["last_read_count"] == 0 and observed["last_read_error"] == 0
                    explicit_abort = observed["last_read_success"] is False and observed["last_read_error"] == 995
                    self.assertTrue(normal_zero or explicit_abort)
                    self.assertIs(observed["zero_read_accepted"], normal_zero)
            record["status"] = "passed"
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            record["error_kind"] = "deadline-exceeded"
            raise
        except BaseException as error:
            record["error_kind"] = type(error).__name__
            raise
        finally:
            if process and process.poll() is None:
                record["forced_cleanup"] = True
                job.terminate()
                process.kill()
                stdout, stderr = process.communicate(timeout=5)
            if process:
                record["returncode"] = process.poll()
                record["process_reaped"] = process.poll() is not None
            try:
                record["outer_job_after"] = job.drain(timeout=3)
            finally:
                job.close()
                (directory / "stdout.json").write_bytes(stdout)
                (directory / "stderr.txt").write_bytes(stderr)
                (directory / "validation.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
