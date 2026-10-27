import unittest

from experiments.conpty.upstream.taef_result import METHOD, validate


def output(passed=0, failed=1, blocked=0, not_run=0, skipped=0):
    total = passed + failed + blocked + not_run + skipped
    assertion = "Error: Verify: AreEqual(til::inclusive_rect{}, _pDispatch->_scrollMargins)\n" if failed else ""
    return (f"StartGroup: Adapter::{METHOD}\n"
            f"{assertion}"
            f"Summary: Total={total}, Passed={passed}, Failed={failed}, "
            f"Blocked={blocked}, Not Run={not_run}, Skipped={skipped}\n")


class TaefResultTests(unittest.TestCase):
    def test_expected_assertion_failure_is_red(self):
        self.assertTrue(validate(output(), 1, "red")["accepted"])

    def test_process_failure_is_not_a_red_regression(self):
        for text in ("Error loading DLL", output().replace("Error: Verify", "Loader failure:"),
                     output().replace(METHOD, "UnrelatedTest"), output(blocked=1, failed=0)):
            with self.subTest(text=text), self.assertRaises(ValueError):
                validate(text, 1, "red")

    def test_no_tests_multiple_summaries_and_skips_are_rejected(self):
        for text in (output(failed=0), output() + output(), output(passed=1, failed=0, skipped=1)):
            with self.subTest(text=text), self.assertRaises(ValueError):
                validate(text, 0, "focused-green")

    def test_focused_and_full_success_remain_separate(self):
        self.assertTrue(validate(output(passed=1, failed=0), 0, "focused-green")["accepted"])
        self.assertTrue(validate(output(passed=5, failed=0), 0, "full-green")["accepted"])
        with self.assertRaises(ValueError):
            validate(output(passed=1, failed=0), 0, "full-green")
        with self.assertRaises(ValueError):
            validate(output(passed=5, failed=0), 0, "focused-green")

    def test_exit_code_and_counts_must_agree(self):
        for text, code, contract in ((output(), 0, "red"), (output(passed=1, failed=0), 1, "focused-green"),
                                     (output().replace("Total=1", "Total=2"), 1, "red")):
            with self.subTest(contract=contract), self.assertRaises(ValueError):
                validate(text, code, contract)

    def test_harness_error_bits_cannot_be_accepted_as_causal_red(self):
        for code in (2, 0x08000001, 0x02000001, 0x01000001, -1):
            with self.subTest(code=code), self.assertRaises(ValueError):
                validate(output(), code, "red")


if __name__ == "__main__":
    unittest.main()
