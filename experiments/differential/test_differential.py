"""Real compiled-core differential checks plus failure/report regression tests."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from cases import Case, cases
from run import ROOT, chunks, compare_traces, run_case, run_suite, trace_binary


class DifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = trace_binary()
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "differential"
        base.mkdir(parents=True, exist_ok=True)
        # Keep inputs/traces even on failures; CI uploads only this owned synthetic data.
        cls.artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=base))

    def test_common_subset_and_precise_known_differences(self):
        summary = run_suite(self.binary, self.artifacts / "suite")
        self.assertTrue(summary["passed"], summary["counts"])
        self.assertEqual(summary["counts"], {"match": 65, "expected_difference": 30})
        self.assertEqual(summary["runs"], 95)
        self.assertEqual(sum(record["plan"] == "byte" for record in summary["records"]), 19)
        for record in summary["records"]:
            self.assertFalse(record.get("core_fragmentation_difference"))
            self.assertFalse(record.get("reference_fragmentation_difference"))

    def test_intermediate_difference_is_found_when_final_state_recovers(self):
        case = Case("transient_cursor_difference", "ab\x1b[99;99HX\x1b[99A\x1b[99Dq\x1b[H!\x1b[;f?")
        folder = self.artifacts / "transient"
        report, core, reference = run_case(self.binary, case, "byte", folder)
        self.assertEqual(report["status"], "unexpected_difference")
        self.assertEqual(report["first_difference"], dict(checkpoint=16, offset=16,
                         path=".screen.wrap_pending", expected=True, actual=False))
        self.assertEqual(core[-1], reference[-1])
        self.assertEqual((folder / "input.bin").read_bytes(), case.data)
        self.assertEqual(json.loads((folder / "report.json").read_text(encoding="utf-8")), report)
        self.assertTrue((folder / "chunks.txt").is_file())

    def test_known_difference_does_not_allow_new_changes(self):
        case = next(case for case in cases() if case.name == "wide_lead_overwrite")
        changed = Case(case.name, case.text + "\x1b[31m", difference=case.difference)
        report, _, _ = run_case(self.binary, changed, "byte", self.artifacts / "changed-known")
        self.assertEqual(report["status"], "unexpected_difference")
        self.assertEqual(report["first_difference"]["offset"], 5)

    def test_execution_failure_never_counts_as_known_difference(self):
        case = next(case for case in cases() if case.name == "wide_lead_overwrite")
        broken = Case(case.name, case.text + "\x1b[", difference=case.difference)
        report, _, _ = run_case(self.binary, broken, "byte", self.artifacts / "truncated")
        self.assertEqual(report["status"], "execution_failure")
        self.assertEqual(report["returncode"], 2)
        self.assertIn("incomplete sequence", (self.artifacts / "truncated/core.stderr.txt").read_text())

    def test_comparator_reports_first_changed_checkpoint(self):
        expected = [dict(offset=i, screen=dict(row=0, column=i)) for i in range(4)]
        actual = copy.deepcopy(expected)
        actual[1]["screen"]["column"] = 9
        actual[2]["screen"]["row"] = 1
        self.assertEqual(compare_traces(expected, actual), dict(checkpoint=1, offset=1,
                         path=".screen.column", expected=1, actual=9))

    def test_rejects_malformed_or_incomplete_chunk_plans(self):
        folder = self.artifacts / "invalid-plans"
        folder.mkdir()
        (folder / "input.bin").write_bytes(b"abc")
        for index, text in enumerate(("0", "1", "4", "1 x", "-1")):
            plan = folder / f"plan-{index}.txt"
            plan.write_text(text)
            result = subprocess.run([str(self.binary), str(folder / "input.bin"), "3", "12", "3", str(plan)],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 2)

    def test_random_splits_are_fixed_nonuniform_and_cover_input(self):
        left = chunks(1024, "seed:42")
        self.assertEqual(left, chunks(1024, "seed:42"))
        self.assertNotEqual(left, chunks(1024, "seed:20261022"))
        self.assertEqual(sum(left), 1024)
        self.assertTrue(all(1 <= size <= 17 for size in left))
        self.assertGreater(len(set(left)), 1)


if __name__ == "__main__":
    unittest.main()
