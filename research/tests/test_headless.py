"""Guard and failure-state tests; never launch Ghidra or the target."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from research import run_headless as runner


class HeadlessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.target = self.root / "target.exe"
        self.target.write_bytes(b"synthetic test fixture")
        self.ghidra = self.root / "ghidra"
        self.java = self.root / "jdk"
        for path in [self.ghidra / "support/analyzeHeadless.bat", self.java / "bin/java.exe"]:
            path.parent.mkdir(parents=True)
            path.touch()
        (self.ghidra / "Ghidra").mkdir()
        (self.ghidra / "Ghidra/application.properties").write_text("application.version=12.1.4\n")
        self.output = self.root / "local/run"
        self.patcher = patch.object(runner, "ROOT", self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def invoke(self, **kwargs):
        return runner.run(self.target, self.ghidra, self.java, self.output, **kwargs)

    def test_wrong_target_creates_no_output(self):
        with self.assertRaisesRegex(ValueError, "Target differs"):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_output_escape_rejected_before_target_read(self):
        self.output = self.root / "public"
        with self.assertRaisesRegex(ValueError, "inside repository local"):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_nonpositive_timeout_rejected(self):
        with self.assertRaisesRegex(ValueError, "positive"):
            self.invoke(timeout=0)

    def test_launcher_failure_is_persisted(self):
        with patch.object(runner, "TARGET_SHA256", runner.digest(self.target)), \
             patch.object(runner, "digest", return_value=runner.digest(self.target)), \
             patch.object(runner.subprocess, "run") as version, \
             patch.object(runner.subprocess, "Popen", side_effect=OSError("synthetic launch failure")):
            version.return_value.stderr = "test JDK"
            result = self.invoke()
        stored = json.loads((self.output / "run.json").read_text())
        self.assertEqual(result["status"], "failed")
        self.assertEqual(stored["status"], "failed")
        self.assertIn("synthetic launch failure", stored["error"])
        self.assertTrue(stored["target_unchanged"])


if __name__ == "__main__":
    unittest.main()
