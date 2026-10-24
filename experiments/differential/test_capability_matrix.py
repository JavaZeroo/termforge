"""Extended native-candidate capabilities and explicit unsupported-core gates."""
import os
from pathlib import Path
import tempfile
import unittest

import capability_matrix
from compare_candidate import candidate_trace_binary
from run import ROOT, trace_binary


class CapabilityMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = trace_binary()
        try:
            cls.candidate = candidate_trace_binary()
        except FileNotFoundError:
            raise unittest.SkipTest("optional Alacritty binary is not built; core-candidate workflow runs these checks")
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "candidate-capabilities"
        base.mkdir(parents=True, exist_ok=True)
        cls.artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        cls.summary = capability_matrix.run_suite(cls.core, cls.candidate, cls.artifacts / "suite")

    def records(self, name):
        return [record for record in self.summary["records"] if record["capability"] == name]

    def test_extended_matrix_preserves_execution_and_support_boundaries(self):
        self.assertTrue(self.summary["execution_passed"], self.summary["counts"])
        self.assertEqual(self.summary["runs"], 20)
        self.assertEqual(self.summary["counts"], {"core_unsupported": 15, "match": 5})
        self.assertFalse(self.summary["semantic_acceptance"])
        self.assertFalse(self.summary["resize_covered"])
        self.assertFalse(self.summary["real_application_covered"])
        for name in ("alternate_screen", "scroll_region", "cursor_query"):
            for record in self.records(name):
                self.assertEqual(record["status"], "core_unsupported")
                self.assertFalse(record["expected_core_support"])
                self.assertIn("trace failed after checkpoint",
                              (self.artifacts / "suite" / name / record["plan"].replace(":", "-") /
                               "core.stderr.txt").read_text())

    def test_alternate_query_and_region_have_real_candidate_results(self):
        for record in self.records("alternate_screen"):
            self.assertEqual(record["candidate_final_rows"], ["A", "BZ", ""])
        for record in self.records("cursor_query"):
            self.assertEqual(record["replies"], ["\x1b[1;2R"])
        for record in self.records("scroll_region"):
            self.assertEqual(record["candidate_final_rows"], ["B", "D", "X"])

    def test_recovery_matches_and_preserves_fragmentation(self):
        for record in self.records("recovery_stream"):
            self.assertEqual(record["status"], "match")
            self.assertEqual(record["core_arguments"], ["--recovery"])
            self.assertEqual(record["candidate_final_rows"], ["ABC�D", "", ""])
            self.assertFalse(record.get("core_fragmentation_difference"))
            self.assertFalse(record.get("candidate_fragmentation_difference"))
            folder = self.artifacts / "suite" / "recovery_stream" / record["plan"].replace(":", "-")
            self.assertIn(b"\xff", (folder / "input.bin").read_bytes())


if __name__ == "__main__":
    unittest.main()
