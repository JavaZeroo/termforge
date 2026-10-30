"""Extended native-candidate capabilities and explicit unsupported-core gates."""
import os
import json
from pathlib import Path
import tempfile
import subprocess
import sys
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
        self.assertEqual(self.summary["runs"], 25)
        self.assertEqual(self.summary["counts"], {"core_unsupported": 0, "match": 16,
                                                  "reference_difference": 4, "resize_difference": 5})
        self.assertFalse(self.summary["semantic_acceptance"])
        self.assertTrue(self.summary["resize_covered"])
        self.assertFalse(self.summary["real_application_covered"])
        for record in self.records("scroll_region"):
            # DECSTBM is implemented, so these records carry a real comparison.
            self.assertTrue(record["expected_core_support"])
            self.assertNotEqual(record["status"], "core_unsupported")
        for record in self.records("cursor_query"):
            # The core answers the cursor report now, so this is a compared case.
            self.assertTrue(record["expected_core_support"])
            self.assertEqual(record["status"], "match")

    def test_alternate_query_and_region_have_real_candidate_results(self):
        for record in self.records("alternate_screen"):
            self.assertTrue(record["expected_core_support"])
            self.assertEqual(record["status"], "match" if record["plan"] == "whole" else "reference_difference")
            self.assertFalse(record.get("core_fragmentation_difference"))
            self.assertFalse(record.get("candidate_fragmentation_difference"))
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

    def test_public_cli_keeps_reviewed_and_unlisted_plan_boundaries(self):
        for plan,expected_code,expected_counts in ((None,0,{"match":1,"reference_difference":4}),
                                                   ("seed:99",1,{"difference":1})):
            output=self.artifacts/("cli-fixed" if plan is None else "cli-unlisted")
            command=[sys.executable,str(Path(capability_matrix.__file__).resolve()),"--trace",str(self.core),
                     "--candidate",str(self.candidate),"--out",str(output),"--case","alternate_screen"]
            if plan is not None:command.extend(["--plan",plan])
            completed=subprocess.run(command,capture_output=True,text=True,timeout=20)
            self.assertEqual(completed.returncode,expected_code,completed.stderr)
            summary=json.loads((output/"summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["counts"],expected_counts)
            self.assertFalse(summary["semantic_acceptance"])

    def test_resize_records_actual_dimensions_and_reflow_difference(self):
        for record in self.records("resize_stream"):
            self.assertEqual(record["status"], "resize_difference")
            self.assertEqual(record["core_arguments"], ["--resize=2x6"])
            self.assertEqual(record["candidate_arguments"], ["--resize=2x6"])
            self.assertEqual(record["core_final_dimensions"], [2, 6])
            self.assertEqual(record["candidate_final_dimensions"], [2, 6])
            self.assertEqual(record["candidate_final_rows"], ["123456", "7890"])
            self.assertEqual((record["first_difference"]["offset"], record["first_difference"]["path"],
                              record["first_difference"]["expected"], record["first_difference"]["actual"]),
                             (18, ".screen.column", 5, 4))


if __name__ == "__main__":
    unittest.main()
