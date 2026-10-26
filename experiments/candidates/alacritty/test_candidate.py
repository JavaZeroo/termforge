import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.candidates.alacritty.run import ROOT, run_suite, trace_binary
from experiments.candidates.alacritty.grapheme_checks import run_fixed_samples


class AlacrittyCandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = trace_binary()
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts"))
        parent = parent / "candidate-alacritty"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        cls.summary = run_suite(cls.binary, cls.out / "suite")

    def test_real_candidate_checks_pass(self):
        self.assertTrue(self.summary["passed"], self.summary)
        self.assertEqual(len(self.summary["cases"]), 3)
        self.assertFalse(self.summary["performance_measured"])

    def test_fixed_streaming_grapheme_samples(self):
        summary = run_fixed_samples(self.binary, self.out / "fixed-graphemes")
        self.assertTrue(summary["passed"], summary)
        self.assertEqual(len(summary["cases"]), 4)
        self.assertFalse(summary["windterm_runtime_executed"])
        self.assertFalse(summary["performance_measured"])

    def test_identity_and_limitations_are_recorded(self):
        self.assertEqual(self.summary["version"], "0.26.1-dev+termforge.4")
        self.assertEqual(self.summary["license"], "Apache-2.0")
        self.assertEqual(self.summary["upstream_commit"], "542147067dd016fc5f49120ef0ba605a98a4e3a7")
        self.assertEqual(len(self.summary["trace_sha256"]), 64)
        self.assertGreaterEqual(len(self.summary["source_sha256"]), 4)
        self.assertIn("experiments/candidates/alacritty/vendor/alacritty_terminal/src/grid/row.rs",
                      self.summary["source_sha256"])
        self.assertTrue(self.summary["limitations"])

    def test_invalid_chunk_plan_is_an_execution_failure(self):
        out = self.out / "invalid-plan"
        out.mkdir()
        (out / "input.bin").write_bytes(b"A")
        (out / "chunks.txt").write_text("2\n", encoding="ascii")
        result = subprocess.run([str(self.binary), str(out / "input.bin"), "3", "8", "2",
                                 str(out / "chunks.txt")], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"chunk plan exceeds input", result.stderr)

    def test_missing_binary_is_not_a_pass(self):
        with self.assertRaises(FileNotFoundError):
            trace_binary(self.out / "missing-candidate")

    def test_summary_rejects_a_changed_real_result(self):
        changed = json.loads((self.out / "suite" / "summary.json").read_text(encoding="utf-8"))
        changed["cases"][0]["passed"] = False
        self.assertFalse(all(case["passed"] for case in changed["cases"]))


if __name__ == "__main__":
    unittest.main()
