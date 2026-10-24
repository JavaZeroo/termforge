"""Bounded fuzz, first-failure minimization and exact fixed regressions."""
import os
from pathlib import Path
import tempfile
import unittest

from compare_candidate import candidate_trace_binary
import fuzz_matrix
from run import ROOT, trace_binary


class FuzzMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = trace_binary()
        try:
            cls.candidate = candidate_trace_binary()
        except FileNotFoundError:
            raise unittest.SkipTest("optional Alacritty binary is not built; core-candidate workflow runs these checks")
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "candidate-fuzz"
        base.mkdir(parents=True, exist_ok=True)
        cls.artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        cls.summary = fuzz_matrix.run_suite(cls.core, cls.candidate, cls.artifacts / "suite")

    def test_bounded_fuzz_preserves_known_and_new_difference_boundaries(self):
        self.assertTrue(self.summary["execution_passed"], self.summary["counts"])
        self.assertEqual(self.summary["seeds"], 4)
        self.assertEqual(self.summary["runs"], 8)
        self.assertEqual(self.summary["counts"], {"known_failure": 2, "match": 2, "reference_difference": 4})
        self.assertFalse(self.summary["semantic_acceptance"])

    def test_original_and_minimized_regressions_match_exact_signatures(self):
        for record in fuzz_matrix.known_failures().values():
            data = bytes.fromhex(record["hex"])
            status, report, core, candidate = fuzz_matrix.evaluate(
                self.core, self.candidate, data, record["name"], self.artifacts / ("regression-" + record["name"]))
            self.assertEqual(status, "known_failure")
            self.assertEqual(fuzz_matrix.signature(report, core, candidate), record["signature"])
            self.assertEqual(report["input_sha256"], record["signature"]["input_sha256"])

    def test_minimizer_preserves_failure_path_and_fixed_minimum(self):
        original = bytes.fromhex("cc81e4b8ad081b5b323b3348")
        minimized = fuzz_matrix.minimize(self.core, self.candidate, original, ".screen.cells[0][0].text",
                                        self.artifacts / "minimize-check")
        self.assertEqual(minimized.hex(), "81e4b8ad1b5b48")
        status, _, _, _ = fuzz_matrix.evaluate(self.core, self.candidate, minimized, "minimum-check",
                                               self.artifacts / "minimum-check")
        self.assertEqual(status, "known_failure")

    def test_changed_input_cannot_reuse_fuzz_regression(self):
        original = bytes.fromhex("cc81e4b8ad081b5b323b3348")
        status, _, _, _ = fuzz_matrix.evaluate(self.core, self.candidate, original + b"A", "changed-fuzz",
                                               self.artifacts / "changed-fuzz")
        self.assertEqual(status, "difference")


if __name__ == "__main__":
    unittest.main()
