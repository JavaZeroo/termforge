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
        self.assertEqual(self.summary["counts"], {"difference": 5, "match": 90})
        self.assertFalse(self.summary["semantic_acceptance"])
        self.assertFalse(self.summary["differences_adjudicated"])
        for record in self.summary["records"]:
            self.assertFalse(record.get("core_fragmentation_difference"))
            self.assertFalse(record.get("candidate_fragmentation_difference"))
            folder = self.artifacts / "suite" / record["case"] / record["plan"].replace(":", "-")
            self.assertTrue((folder / "core.jsonl").is_file())
            self.assertTrue((folder / "candidate.jsonl").is_file())
            self.assertEqual((folder / "input.bin").read_bytes(),
                             next(case.data for case in cases() if case.name == record["case"]))

    def test_current_differences_keep_exact_case_offset_and_direction(self):
        differences = [record for record in self.summary["records"] if record["status"] == "difference"]
        self.assertEqual({(record["case"], record["plan"]) for record in differences},
                         {("erase_display", plan)
                          for plan in ("byte", "whole", "seed:42", "seed:20261022", "seed:8675309")})
        erase_expected = {
            "byte": (28, ".screen.cells[0][0].text", "", "o"),
            "whole": (32, ".screen.history.length", 0, 1),
            "seed:42": (30, ".screen.cells[0][0].text", "", "o"),
            "seed:20261022": (28, ".screen.cells[0][0].text", "", "o"),
            "seed:8675309": (32, ".screen.history.length", 0, 1),
        }
        for record in differences:
            difference = record["first_difference"]
            self.assertEqual((difference["offset"], difference["path"],
                              difference["expected"], difference["actual"]), erase_expected[record["plan"]])
            self.assertIn("neither is an oracle", record["difference_direction"])

    def test_missing_candidate_is_an_execution_failure(self):
        case = next(case for case in cases() if case.name == "ascii_wrap_scroll")
        missing = self.artifacts / "missing-candidate"
        report, _, _ = run_case(self.core, missing, case, "whole", self.artifacts / "missing")
        self.assertEqual(report["status"], "execution_failure")
        self.assertIsNone(report["candidate_returncode"])
        self.assertTrue((self.artifacts / "missing/candidate.stderr.txt").is_file())


if __name__ == "__main__":
    unittest.main()
