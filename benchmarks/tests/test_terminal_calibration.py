import copy
import sys
import unittest
from unittest.mock import Mock

from test_tools import load, TOOLS

sys.path.insert(0, str(TOOLS))
calibration = load("terminal_calibration")


def records():
    return [{"step": step, "marker_confirmed": True,
             "run": {"status": "producer_complete", "actual_settings": {"columns": 120, "rows": 40},
                     "producer": {"t0_counter_ns": 1, "producer_done_counter_ns": 111 if step["sampling"] else 101,
                                  "fully_written_chunk_bytes": 8388608, "payload_bytes": 8388608}}}
            for step in calibration.schedule()]


class CalibrationComparison(unittest.TestCase):
    def test_session_identity_ignores_wrapper_and_ancestors_above_terminal(self):
        wrapper, shell, target, unrelated = [Mock(pid=i) for i in range(4)]
        wrapper.name.return_value = "cmd.exe"
        shell.name.return_value = "pwsh.exe"
        target.name.return_value = "WindTerm.exe"
        unrelated.name.return_value = "powershell.exe"
        self.assertIs(calibration.session_shell([wrapper, shell, target, unrelated], target.pid), shell)
        with self.assertRaises(ValueError):
            calibration.session_shell([target, unrelated], target.pid)

    def test_balanced_alternating_pairs_and_warmup_exclusion(self):
        rows = records()
        self.assertEqual([r["step"]["sampling"] for r in rows[2:6]], [False, True, True, False])
        rows[0]["run"]["producer"]["producer_done_counter_ns"] = 999999999
        result = calibration.compare(rows)
        self.assertEqual(result["median_off_ns"], 100)
        self.assertEqual(result["median_on_ns"], 110)
        self.assertEqual(len(result["sampler_on_ns"]), 5)
        self.assertTrue(result["above_5_percent_trigger"])
        self.assertEqual(result["performance_gate"], "not_evaluated")

    def test_missing_failed_unconfirmed_and_resized_are_not_success(self):
        self.assertEqual(calibration.compare(records()[:-1])["status"], "incomplete")
        for change, expected in (
                (lambda r: r[3].update(marker_confirmed=False), "failed_samples_retained"),
                (lambda r: r[3]["run"].update(status="producer_timeout"), "failed_samples_retained"),
                (lambda r: r[3]["run"]["actual_settings"].update(rows=41), "inconsistent_or_unknown_terminal_size"),
                (lambda r: r[3]["run"]["actual_settings"].update(rows=None), "inconsistent_or_unknown_terminal_size"),
                (lambda r: r[3]["run"]["producer"].update(fully_written_chunk_bytes=2), "invalid_producer_record")):
            rows = copy.deepcopy(records())
            change(rows)
            result = calibration.compare(rows)
            self.assertEqual(result["status"], expected)
            self.assertNotIn("median_elapsed_change_percent", result)


if __name__ == "__main__":
    unittest.main()
