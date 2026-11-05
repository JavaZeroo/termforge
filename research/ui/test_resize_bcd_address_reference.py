"""Focused offline guards for the fixed BCD pre-write plan; no target or socket."""
import hashlib
import json
import unittest

from research.ui import resize_bcd_address_reference as probe
from research.ui import export_resize_bcd_address_reference as exporter


class BcdAddressContractTests(unittest.TestCase):
    def test_fixed_geometry_and_four_phases(self):
        self.assertEqual(probe.WINDOWS, ((1228, 869), (988, 509), (988, 509), (1228, 869)))
        self.assertEqual(probe.GRIDS, ((80, 24), (60, 12), (60, 12), (80, 24)))
        self.assertEqual(probe.RESIZE_PHASES, (1, 3))
        records = probe.phase_records("shrink-bcd-cup")
        self.assertEqual([r["label"] for r in records],
                         ["initial-bcd-body", "after-shrink-before-write",
                          "after-shrink-cup-write", "after-restore-after-write"])
        self.assertEqual([r["source_hex"] for r in records][1:],
                         ["", probe.CASES["shrink-bcd-cup"].hex(), ""])
        self.assertTrue(records[0]["source_hex"].endswith(b"\x1b[11;1HD".hex()))

    def test_fixed_contract_is_bounded_and_explicit(self):
        contract = probe.fixed_contract("shrink-bcd-cup")
        self.assertEqual(contract["maximum_original_runs"], 1)
        self.assertEqual(contract["actual_original_runs"], 0)
        self.assertEqual(contract["phase_count"], 4)
        self.assertEqual(contract["resize_phases"], [1, 3])
        self.assertEqual(contract["action_phase"], 2)
        self.assertFalse(contract["semantic_acceptance"])
        self.assertIn("not local-shell/native-grid proof", contract["scope"])

    def test_synthetic_allowlist_is_source_derived_and_bounded(self):
        allowed = probe.synthetic_lines("shrink-bcd-cup", 2)
        self.assertIn("", allowed)
        self.assertIn(" " * 79 + "B", allowed)
        self.assertIn("D", allowed)
        self.assertIn("RESIZED", allowed)
        self.assertIn("RESIZED" + " " * 72 + "B", allowed)
        self.assertNotIn("PRIVATE", allowed)
        self.assertNotIn("Q", allowed)
        terminal = {"line_count": 12,
                    "lines": ["", "", "", "", "", "", "", " " * 79 + "B", "", " " * 79 + "C", "D", "RESIZED"]}
        probe.validate_document("shrink-bcd-cup", 2, terminal)
        with self.assertRaises(ValueError):
            probe.validate_document("shrink-bcd-cup", 1, terminal)

    def test_protected_controller_anchor_and_fragment(self):
        source = (probe.ROOT / probe.CONTROLLER).read_text(encoding="utf-8")
        self.assertEqual(source.count(probe.CONTROLLER_ANCHOR), 1)
        fragment = probe.probe_fragment("shrink-bcd-cup")
        self.assertIn(json.dumps("shrink-bcd-cup"), fragment)
        self.assertNotIn(probe.CONTROLLER_ANCHOR, fragment)

    def test_source_archive_lists_the_new_tool_and_controller(self):
        for name in probe.SOURCE_FILES:
            self.assertTrue((probe.ROOT / name).is_file(), name)
        self.assertIn("research/ui/resize_bcd_address_reference.py", probe.SOURCE_FILES)
        self.assertIn(probe.CONTROLLER, probe.SOURCE_FILES)

    def test_exporter_template_is_deterministic_before_any_run(self):
        plan = {"case_id": "shrink-bcd-cup", "out": "C:/synthetic/out"}
        run = {"pid": 1, "tid": 2, "window_station": "TermForgeR1-1"}
        template = (probe.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split(
            "// Scenario driver", 1)[0]
        template += probe.probe_fragment(plan["case_id"])
        run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
        generated = exporter.expected_probe(plan, run, probe.ROOT).decode("utf-8")
        self.assertIn("shrink-bcd-cup", generated)
        self.assertIn("bcd-address-result.json", generated)
        self.assertNotIn("__RESIZE_BCD_CASE_JSON__", generated)


if __name__ == "__main__":
    unittest.main()
