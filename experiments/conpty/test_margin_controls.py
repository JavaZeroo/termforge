"""Independent literal captures and corruption gates for native margin controls."""
import copy
import unittest

from experiments.conpty.margin_controls import evaluate_native, matches_unpatched_prediction


def capture(lines, cursor, columns=12, rows=7):
    padded = lines + [""] * (rows - len(lines))
    return {"columns": columns, "rows": rows, "top": 0,
            "lines": padded, "cell_lines": padded.copy(),
            "cells": [[(ord(c), 7) for c in line.ljust(columns)] for line in padded],
            "cursor_row": cursor[0], "cursor_column": cursor[1]}


class NativeMarginControlTests(unittest.TestCase):
    def main_only(self):
        return {"before": capture(["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"], (0, 0)),
                "final": capture(["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "CTRL_DONE"], (6, 0))}

    def test_main_region_preserves_outside_and_rejects_corrupted_fields(self):
        original = self.main_only()
        self.assertTrue(evaluate_native(original, "main-only", 12, 7)["passed"])
        self.assertTrue(matches_unpatched_prediction(original, "main-only", 12, 7))
        for field in ("lines", "cell_lines", "cells", "cursor_row", "cursor_column", "top"):
            changed = copy.deepcopy(original)
            if field in ("lines", "cell_lines"):
                changed["final"][field][0] = "REGION_A"
            elif field == "cells":
                changed["final"][field][0][0] = (ord("T"), 4)
            else:
                changed["final"][field] += 1
            with self.subTest(field=field):
                self.assertFalse(evaluate_native(changed, "main-only", 12, 7)["passed"])
                self.assertFalse(matches_unpatched_prediction(changed, "main-only", 12, 7))

    def test_new_alternate_has_full_screen_margins_and_qualifies_specific_old_failure(self):
        sample = self.main_only()
        sample["alt"] = capture(["", "", "", "", "", "ALT_LAST", "ALT_DONE"], (6, 8))
        sample["restored"] = copy.deepcopy(sample["before"])
        self.assertTrue(evaluate_native(sample, "default-alt", 12, 7)["passed"])
        self.assertFalse(matches_unpatched_prediction(sample, "default-alt", 12, 7))
        old = copy.deepcopy(sample)
        old["alt"] = capture(["ALT_TOP", "", "", "", "", "", "ALT_DONE"], (6, 8))
        result = evaluate_native(old, "default-alt", 12, 7)
        self.assertFalse(result["passed"])
        self.assertEqual([name for name, stage in result["stages"].items() if not stage["passed"]], ["alt"])
        self.assertTrue(matches_unpatched_prediction(old, "default-alt", 12, 7))
        old["before"]["cursor_row"] = 1
        self.assertFalse(matches_unpatched_prediction(old, "default-alt", 12, 7))

    def test_repeated_alternate_and_main_requests_keep_the_hidden_main_region(self):
        sample = self.main_only()
        sample["alt1"] = capture(["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"], (4, 5))
        sample["alt2"] = capture(["", "", "", "", "", "ALT_LAST", "ALT_DONE"], (6, 8))
        sample["restored"] = copy.deepcopy(sample["before"])
        self.assertTrue(evaluate_native(sample, "repeat-alt", 12, 7)["passed"])
        self.assertFalse(matches_unpatched_prediction(sample, "repeat-alt", 12, 7))
        old = copy.deepcopy(sample)
        old["alt2"] = capture(["ALT_TOP", "", "", "", "", "", "ALT_DONE"], (6, 8))
        old["final"] = capture(["TOP_FIXED", "REGION_A", "REGION_B", "REGION_CIXED", "REGION_D", "CTRL_DONE"], (6, 0))
        result = evaluate_native(old, "repeat-alt", 12, 7)
        self.assertEqual([name for name, stage in result["stages"].items() if not stage["passed"]], ["alt2", "final"])
        self.assertTrue(matches_unpatched_prediction(old, "repeat-alt", 12, 7))
        old["restored"]["cells"][0][0] = (ord("T"), 4)
        self.assertFalse(matches_unpatched_prediction(old, "repeat-alt", 12, 7))

    def test_missing_stage_and_wrong_grid_are_rejected(self):
        with self.assertRaises(ValueError):
            evaluate_native({"before": self.main_only()["before"]}, "main-only", 12, 7)
        with self.assertRaises(ValueError):
            evaluate_native(self.main_only(), "main-only", 80, 24)
        with self.assertRaises(ValueError):
            evaluate_native(self.main_only(), "unknown", 12, 7)


if __name__ == "__main__":
    unittest.main()
