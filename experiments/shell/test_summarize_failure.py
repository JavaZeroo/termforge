"""Contracts for the workspace failure dump summary (#73)."""
import unittest

from experiments.shell.summarize_failure import describe, parse

DUMP = """ValueError: shell probe failed: workspace-failure mode=ui-copy-system stage=7 what=selection scenario exceeded its bounded deadline
  tab bottom:0 input_high_water=26 input_rejected=0 output_bytes=688 read_error=0 consumed=688 stage=0 closing=false child_signaled=false exit_code=259
  tab main:0 input_high_water=2 input_rejected=0 output_bytes=577 read_error=0 consumed=577 stage=0 closing=false child_signaled=false exit_code=259
"""

OLD_STYLE = """workspace-failure mode=ui-selection what=selection scenario exceeded its bounded deadline
  tab bottom:0 input_high_water=3 input_rejected=0 output_bytes=637 read_error=0 consumed=637 stage=0 closing=false child_signaled=false exit_code=259
"""


class WorkspaceFailureDump(unittest.TestCase):
    def test_stage_and_both_tabs_are_read(self):
        failures = parse(DUMP)
        self.assertEqual(len(failures), 1)
        self.assertEqual(failures[0]["mode"], "ui-copy-system")
        self.assertEqual(failures[0]["stage"], 7)
        self.assertEqual([(tab["group"], tab["index"]) for tab in failures[0]["tabs"]],
                         [("bottom", 0), ("main", 0)])

    def test_missing_stage_still_parses(self):
        failures = parse(OLD_STYLE)
        self.assertEqual(len(failures), 1)
        self.assertIsNone(failures[0]["stage"])

    def test_drained_and_child_state_are_reported(self):
        verdicts = describe(parse(DUMP)[0])
        self.assertEqual(verdicts[0], "bottom:0 rejected=0 drained=yes child=259")
        self.assertEqual(verdicts[1], "main:0 rejected=0 drained=yes child=259")

    def test_partial_read_is_not_called_drained(self):
        failure = parse(DUMP)[0]
        failure["tabs"][0]["counters"]["consumed"] = "1"
        self.assertIn("drained=no", describe(failure)[0])


if __name__ == "__main__":
    unittest.main()
