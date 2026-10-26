"""Reject falsely accepted UI paste evidence without launching a desktop."""
import copy
import unittest
from experiments.shell.paste_checks import validate_paste_fence


class PasteFenceEvidenceTests(unittest.TestCase):
    def test_requires_held_partial_progress_and_real_cancel_or_stop(self):
        row = dict(name="cancel", before_sent=17391, before_accepted=18414,
                   fence=dict(generation=1, held=True, owned_bytes=1023, wait_finished=True,
                              other_responsive=True, exit="cancelled", wrong_generation_rejected=True))
        validate_paste_fence(row, 1)
        for key, value in (("generation", 2), ("held", False), ("owned_bytes", 1024),
                           ("wait_finished", False), ("exit", "released"),
                           ("other_responsive", False), ("wrong_generation_rejected", False)):
            broken = copy.deepcopy(row)
            broken["fence"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_paste_fence(broken, 1)
        with self.assertRaises(ValueError):
            validate_paste_fence({k: v for k, v in row.items() if k != "fence"}, 1)
