"""Run the real Windows pseudo-console; no GUI or parser is involved."""
import copy
import os
import unittest

from experiments.conpty.run import binary_path, check_sample, execute


@unittest.skipUnless(os.name == "nt", "ConPTY is Windows-only")
class ConPtyLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = binary_path()
        if not cls.binary.is_file():
            raise RuntimeError(f"Build required probe: {cls.binary}")

    def test_normal_echo_resize_and_exit(self):
        result = execute(self.binary, "normal", repeats=3)
        broken = copy.deepcopy(result["samples"][0])
        broken["handles_after"] += 1
        with self.assertRaises(AssertionError):
            check_sample(broken)

    def test_large_ordered_payload(self):
        result = execute(self.binary, "large", repeats=2)
        broken = copy.deepcopy(result["samples"][0])
        broken["validated_records"] -= 1
        with self.assertRaises(AssertionError):
            check_sample(broken)

    def test_cancel_busy_tree(self):
        execute(self.binary, "cancel", repeats=3)

    def test_close_busy_tree(self):
        execute(self.binary, "close", repeats=3)

    def test_watchdog_kills_only_owned_job(self):
        execute(self.binary, "watchdog", timeout=8)


if __name__ == "__main__":
    unittest.main()
