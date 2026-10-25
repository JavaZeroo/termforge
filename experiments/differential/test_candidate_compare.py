"""Common TermForge/Alacritty traces; semantic differences are data, not a pass."""
import os
from pathlib import Path
import tempfile
import unittest

from cases import cases
import compare_candidate
from compare_candidate import run_case, run_suite
from run import ROOT, trace_binary


class CandidateComparisonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = trace_binary()
        try:
            cls.candidate = compare_candidate.candidate_trace_binary()
        except FileNotFoundError:
            raise unittest.SkipTest("optional Alacritty binary is not built; core-candidate workflow runs these checks")
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "candidate-comparison"
        base.mkdir(parents=True, exist_ok=True)
        cls.artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        cls.summary = run_suite(cls.core, cls.candidate, cls.artifacts / "suite")

    def test_common_matrix_executes_and_preserves_fragmentation(self):
        self.assertTrue(self.summary["execution_passed"], self.summary["counts"])
        self.assertEqual(self.summary["runs"], 95)
        self.assertEqual(self.summary["checkpoints"], 1011)
        self.assertEqual(self.summary["counts"], {"match": 95})
        self.assertFalse(self.summary["semantic_acceptance"])
        self.assertTrue(self.summary["differences_adjudicated"])
        for record in self.summary["records"]:
            self.assertFalse(record.get("core_fragmentation_difference"))
            self.assertFalse(record.get("candidate_fragmentation_difference"))
            folder = self.artifacts / "suite" / record["case"] / record["plan"].replace(":", "-")
            self.assertTrue((folder / "core.jsonl").is_file())
            self.assertTrue((folder / "candidate.jsonl").is_file())
            self.assertEqual((folder / "input.bin").read_bytes(),
                             next(case.data for case in cases() if case.name == record["case"]))

    def test_retired_erase_display_differences_stay_fixed(self):
        records = [record for record in self.summary["records"] if record["case"] == "erase_display"]
        self.assertEqual(len(records), 5)
        for record in records:
            self.assertEqual(record["status"], "match")
            self.assertIsNone(record["first_difference"])
            self.assertIn("neither is an oracle", record["difference_direction"])

    def test_changed_erase_display_input_also_matches(self):
        case = next(case for case in cases() if case.name == "erase_display")
        changed = type(case)(case.name, case.text + "\x1b[31m")
        report, _, _ = run_case(self.core, self.candidate, changed, "whole", self.artifacts / "changed-known")
        self.assertEqual(report["status"], "match")

    def test_missing_candidate_is_an_execution_failure(self):
        case = next(case for case in cases() if case.name == "ascii_wrap_scroll")
        missing = self.artifacts / "missing-candidate"
        report, _, _ = run_case(self.core, missing, case, "whole", self.artifacts / "missing")
        self.assertEqual(report["status"], "execution_failure")
        self.assertIsNone(report["candidate_returncode"])
        self.assertTrue((self.artifacts / "missing/candidate.stderr.txt").is_file())


if __name__ == "__main__":
    unittest.main()
