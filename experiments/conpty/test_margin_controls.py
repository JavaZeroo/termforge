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


def styled_capture(columns=12, rows=7):
    def state(lines, cursor):
        return capture(lines, cursor, columns, rows)

    sample = {
        "before": state(["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"], (2, 5)),
        "alt": state(["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"], (4, 7)),
        "restored": state(["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"], (2, 5)),
        "after-write": state(["TOP_FIXED", "REGION_A", "REGIOM_B", "BOTTOM_FIXED"], (2, 6)),
        "final": state(["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "SGR_DONE"], (5, 8)),
    }
    # Independent literal Win32 color values: green/magenta, red/blue.
    for row in (3, 4):
        sample["alt"]["cells"][row] = [(character, 0x52) for character, _ in sample["alt"]["cells"][row]]
    sample["after-write"]["cells"][2][5] = (ord("M"), 0x14)
    for row in (1, 2):
        sample["final"]["cells"][row] = [(character, 0x14) for character, _ in sample["final"]["cells"][row]]
    for column, character in enumerate("SGR_DONE"):
        sample["final"]["cells"][5][column] = (ord(character), 0x14)
    return sample


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

    def test_styled_main_cursor_restores_and_scroll_retains_explicit_attributes(self):
        for columns, rows in ((12, 7), (80, 24)):
            sample = styled_capture(columns, rows)
            self.assertTrue(evaluate_native(sample, "styled-cursor", columns, rows)["passed"])
            for stage, row, column in (("after-write", 2, 5), ("alt", 3, columns - 1), ("final", 1, columns - 1)):
                changed = copy.deepcopy(sample)
                character, _ = changed[stage]["cells"][row][column]
                changed[stage]["cells"][row][column] = (character, 7)
                with self.subTest(stage=stage, columns=columns):
                    self.assertFalse(evaluate_native(changed, "styled-cursor", columns, rows)["passed"])
            changed = copy.deepcopy(sample)
            changed["restored"]["cursor_column"] = 0
            self.assertFalse(evaluate_native(changed, "styled-cursor", columns, rows)["passed"])

    def test_missing_stage_and_wrong_grid_are_rejected(self):
        with self.assertRaises(ValueError):
            evaluate_native({"before": self.main_only()["before"]}, "main-only", 12, 7)
        with self.assertRaises(ValueError):
            evaluate_native(self.main_only(), "main-only", 80, 24)
        with self.assertRaises(ValueError):
            evaluate_native(self.main_only(), "unknown", 12, 7)

    def test_styled_baseline_requires_the_specific_old_margin_cells_and_attributes(self):
        sample = styled_capture()
        sample["final"] = capture(["TOP_FIXED", "REGION_A", "REGIOM_B", "REGION_CIXED", "REGION_D", "SGR_DONE"], (5, 8))
        sample["final"]["cells"][2][5] = (ord("M"), 0x14)
        for row in (3, 4, 5):
            for column in range(8):
                character, _ = sample["final"]["cells"][row][column]
                sample["final"]["cells"][row][column] = (character, 0x14)
        result = evaluate_native(sample, "styled-cursor", 12, 7)
        self.assertEqual([name for name, stage in result["stages"].items() if not stage["passed"]], ["final"])
        self.assertTrue(matches_unpatched_prediction(sample, "styled-cursor", 12, 7))
        for stage, row, column in (("before", 0, 1), ("after-write", 2, 5), ("final", 3, 9)):
            changed = copy.deepcopy(sample)
            character, _ = changed[stage]["cells"][row][column]
            changed[stage]["cells"][row][column] = (character, 0x52)
            self.assertFalse(matches_unpatched_prediction(changed, "styled-cursor", 12, 7))

    def test_actual_terminal_final_requires_colors_cursor_and_restored_current_style(self):
        from experiments.conpty.margin_controls import evaluate_terminal

        lines = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "SGR_DONE", ""]
        default = {"fg": -1, "bg": -1, "bold": False}
        explicit = {"fg": 1, "bg": 4, "bold": False}
        # The trace represents untouched default blanks as empty/zero-width;
        # a blank with an explicit background is a space/one-width cell.
        cells = [[{"text": character, "width": 1, "style": default.copy()} for character in line]
                 + [{"text": "", "width": 0, "style": default.copy()} for _ in range(12 - len(line))]
                 for line in lines]
        for row in (1, 2):
            for cell in cells[row]:
                cell.update(text=cell["text"] or " ", width=1, style=explicit.copy())
        for cell in cells[5][:8]:
            cell["style"] = explicit.copy()
        record = {"mode_bits": 0, "screen": {"cells": cells, "row": 5, "column": 8, "current_style": explicit.copy()}}
        self.assertTrue(evaluate_terminal(record, "styled-cursor", 12, 7)["passed"])
        changes = (
            lambda row: row["screen"].update(column=0),
            lambda row: row.update(mode_bits=1 << 12),
            lambda row: row["screen"]["current_style"].update(fg=2),
            lambda row: row["screen"]["cells"][1][-1]["style"].update(fg=-1),
            lambda row: row["screen"]["cells"][1][-1]["style"].update(bg=-1),
            lambda row: row["screen"]["cells"][0][0].update(text="X"),
            lambda row: row["screen"]["cells"][2][0].update(width=2),
            lambda row: row["screen"]["cells"][4][0].update(text=" ", width=1),
        )
        for change in changes:
            changed = copy.deepcopy(record)
            change(changed)
            self.assertFalse(evaluate_terminal(changed, "styled-cursor", 12, 7)["passed"])


if __name__ == "__main__":
    unittest.main()
