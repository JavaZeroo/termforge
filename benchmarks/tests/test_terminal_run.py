import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

from test_tools import load, TOOLS

sys.path.insert(0, str(TOOLS))
runner = load("terminal_run")
prepare = load("prepare_windterm")
generate = load("generate")


class TerminalRun(unittest.TestCase):
    def test_wrong_ancestor_and_redirected_stdout_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / "unrelated.exe"
            exe.write_bytes(b"not running")
            with self.assertRaises(ValueError):
                runner.find_target(exe)
            with patch.object(runner.sys, "stdout", io.StringIO()):
                with self.assertRaises(ValueError):
                    runner.run(temp, Path(temp) / "out", exe)
            self.assertFalse((Path(temp) / "out").exists())

    def test_wrong_archive_rejected_before_creating_lab(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "bad.zip"
            archive.write_bytes(b"not an official release")
            with self.assertRaises(ValueError):
                prepare.prepare(archive, Path(temp) / "lab")
            self.assertFalse((Path(temp) / "lab").exists())

    @unittest.skipUnless(os.name == "nt", "Windows runner")
    def test_timeout_retained_and_only_owned_producer_stopped(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            generate.generate(root / "payload", count=4)
            child = Mock()
            child.wait.side_effect = [subprocess.TimeoutExpired("test", 1), 0]
            child.poll.return_value = 0
            with patch.object(runner, "find_target", return_value=runner.psutil.Process()), \
                 patch.object(runner.sys.stdout, "isatty", return_value=True), \
                 patch.object(runner.subprocess, "Popen", return_value=child):
                report = runner.run(root / "payload", root / "result", runner.sys.executable, timeout=1)
            child.kill.assert_called_once()
            self.assertEqual(report["status"], "producer_timeout")
            self.assertNotIn("producer", report)
            self.assertEqual(json.loads((root / "result" / "run.json").read_text())["status"],
                             "producer_timeout")

    @unittest.skipUnless(os.name == "nt", "Windows runner")
    def test_controlled_sink_preserves_payload_and_unknown_endpoints(self):
        # Explicit test seam only: this tests orchestration against a byte file,
        # not a terminal and not performance. Production ancestry/TTY guards stay on.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            generate.generate(root / "payload", count=4)
            actual_popen = subprocess.Popen

            def sink(*args, **kwargs):
                kwargs["stdout"] = output
                kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
                return actual_popen(*args, **kwargs)

            with (root / "received.bin").open("wb") as output:
                with patch.object(runner, "find_target", return_value=runner.psutil.Process()), \
                     patch.object(runner.sys.stdout, "isatty", return_value=True), \
                     patch.object(runner.subprocess, "Popen", side_effect=sink):
                    report = runner.run(root / "payload", root / "result", runner.sys.executable, True)
            self.assertEqual(report["status"], "producer_complete")
            self.assertIsNone(report["presented_done"])
            self.assertEqual(report["performance_gate"], "not_evaluated")
            self.assertEqual((root / "received.bin").read_bytes(),
                             (root / "payload" / "payload.bin").read_bytes())
            resources = json.loads((root / "result" / "resource-summary.json").read_text())
            self.assertGreater(resources["sample_count"], 1)


if __name__ == "__main__":
    unittest.main()
