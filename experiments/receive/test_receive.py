"""Reject corrupt telemetry and verify native argument guards without UI."""
import json
from pathlib import Path
import subprocess
import unittest

from experiments.receive.run import check, ROOT


class ReceiveChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = ROOT / "build/receive/Release/receive.exe"
        if not cls.exe.exists():
            raise unittest.SkipTest("Build Release receive.exe first")

    def result(self):
        p = subprocess.run([str(self.exe), "512", "edge", "yield", "131101", "512"],
                           capture_output=True, text=True, timeout=30, check=True)
        return json.loads(p.stdout)

    def test_tight_queue_odd_eof(self):
        check(self.result())

    def test_corrupt_payload_count_rejected(self):
        row = self.result()
        row["consumed"] -= 1
        with self.assertRaises(AssertionError):
            check(row)

    def test_overflow_rejected(self):
        row = self.result()
        row["peak_queued_bytes"] = row["capacity"] + 1
        with self.assertRaises(AssertionError):
            check(row)

    def test_fake_notifications_rejected(self):
        row = self.result()
        row["notifications"] = row["reads"] + 1
        with self.assertRaises(AssertionError):
            check(row)

    def test_invalid_native_arguments(self):
        for args in [("512", "edge", "stream", "100", "511"),
                     ("512", "invalid", "stream", "100", "512"),
                     ("512", "edge", "stream", "0", "512")]:
            p = subprocess.run([str(self.exe), *args], capture_output=True, timeout=5)
            self.assertEqual(p.returncode, 2)


if __name__ == "__main__":
    unittest.main()
