"""Offline guards for the fixed RIS source and response boundaries."""
import unittest

from research.ui import alternate_ris_reference as probe
from research.ui import export_alternate_ris_reference as exporter


class AlternateRisReferenceTests(unittest.TestCase):
    def test_only_two_phases_send_ris(self):
        records = probe.phase_records(probe.CASE_ID, 25, 91)
        self.assertEqual(len(records), 10)
        self.assertEqual([record["phase"] for record in records if record["source_hex"] == "1b63"], [2, 9])
        self.assertIn("1b5b32353b3148", records[0]["source_hex"])
        probe.validate_fixed_contract(probe.fixed_contract(probe.CASE_ID))
        self.assertIn("phaseCount=10", probe.javascript_source())

    def test_ris_pair_and_dsr_preserve_all_bytes_across_prefixes(self):
        response = b"\x13\x11\x1b[1;1R"
        for phase in (2, 9):
            for size in range(len(response)):
                self.assertIsNone(probe.observed_reply(response[:size], phase))
            reply = probe.observed_reply(response, phase)
            self.assertEqual(reply["cursor_reply"], [1, 1])
            self.assertEqual(reply["response_hex"], response.hex())
            self.assertEqual(reply["flow_control_hex"], "1311")
            self.assertNotIn("flow_control_hex", probe.observed_reply(response[2:], phase))

    def test_other_phase_prefixes_and_extra_bytes_are_rejected(self):
        for phase, response in ((1, b"\x13\x11\x1b[1;1R"), (2, b"\x13\x12\x1b[1;1R"),
                                (2, b"\x13\x11\x13\x11\x1b[1;1R"), (2, b"\x1b[1;1R\x13\x11")):
            with self.subTest(phase=phase, response=response), self.assertRaises(ValueError):
                probe.observed_reply(response, phase)

    def test_export_filters_to_fixed_synthetic_text(self):
        pattern = exporter._runtime["SYNTHETIC_LINE"]
        self.assertIsNotNone(pattern.fullmatch("   ALT"))
        self.assertIsNone(pattern.fullmatch("HIST unrelated"))


if __name__ == "__main__":
    unittest.main()
