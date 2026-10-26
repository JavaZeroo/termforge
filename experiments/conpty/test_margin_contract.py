import copy
import unittest

from experiments.conpty.margin_contract import evaluate_native, expected_native


def captures(columns, rows):
    result = {}
    for name, target in expected_native(columns, rows).items():
        result[name] = dict(columns=columns, rows=rows, top=0,
                            cursor_row=target["cursor"][0],
                            cursor_column=target["cursor"][1],
                            lines=list(target["lines"]), cell_lines=list(target["lines"]),
                            cells=[[(ord(c), 7) for c in line.ljust(columns)]
                                   for line in target["lines"]])
    return result


class NativeMarginContractTests(unittest.TestCase):
    def test_original_results_and_wrapped_marker_have_independent_literals(self):
        for columns, rows in ((80, 24), (12, 7)):
            result = evaluate_native(captures(columns, rows), columns, rows)
            self.assertTrue(result["all_native_stages_passed"])
            self.assertFalse(result["full_r2_contract_verified"])
        self.assertEqual(expected_native(12, 7)["final"]["lines"],
                         ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "",
                          "TF_MARGIN_DO", "NE"])

    def test_historical_margin_corruption_is_not_accepted(self):
        data = captures(80, 24)
        # Observed system/official native failure, not a different expected state.
        data["final"]["lines"][:5] = ["TOP_FIXED", "REGION_A", "REGION_B",
                                      "REGION_CIXED", "REGION_D"]
        result = evaluate_native(data, 80, 24)
        self.assertFalse(result["stages"]["final"]["text"])
        self.assertFalse(result["all_native_stages_passed"])

    def test_each_stage_rejects_attribute_cursor_and_secondary_read_corruption(self):
        for stage in ("before", "alt", "restored", "final"):
            for field in ("attribute", "cursor", "secondary"):
                with self.subTest(stage=stage, field=field):
                    data = copy.deepcopy(captures(80, 24))
                    if field == "attribute":
                        code, _ = data[stage]["cells"][2][2]
                        data[stage]["cells"][2][2] = (code, 4)
                    elif field == "cursor":
                        data[stage]["cursor_column"] += 1
                    else:
                        data[stage]["cell_lines"][0] += "X"
                    result = evaluate_native(data, 80, 24)
                    self.assertFalse(result["stages"][stage]["passed"])
                    self.assertFalse(result["all_native_stages_passed"])

    def test_missing_stage_or_changed_grid_is_rejected(self):
        for change in ("missing", "grid"):
            data = captures(80, 24)
            if change == "missing":
                del data["alt"]
            else:
                data["alt"]["rows"] = 23
            with self.assertRaises(ValueError):
                evaluate_native(data, 80, 24)


if __name__ == "__main__":
    unittest.main()
