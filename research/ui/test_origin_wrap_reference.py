"""Pure protocol/plan counterexamples; never execute a target or native runtime."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from research.ui import origin_wrap_reference as probe


class ProtocolTests(unittest.TestCase):
    def test_fixed_input_covers_fresh_and_explicit_origin_without_inserting_dsr(self):
        rows = probe.phases(25, 91)
        self.assertEqual(len(rows), 25)
        self.assertEqual(rows[0], ("fresh-session-cup", b"\x1b[2J\x1b[HFT"))
        self.assertNotIn(b"\x1b[?6", rows[0][1])
        self.assertEqual(rows[4], ("origin-set", b"\x1b[?6h"))
        self.assertEqual(rows[14], ("wrap-enabled-edge", b"\x1b[?7h\x1b[8;91HAB"))
        self.assertEqual(rows[15], ("wrap-disabled-edge", b"\x1b[?7l\x1b[10;91HCD"))
        self.assertEqual(rows[19][0], "main-margin-origin-before-alt")
        self.assertEqual(rows[20], ("alternate-enter", b"\x1b[?1049h"))
        self.assertEqual(rows[22], ("alternate-exit", b"\x1b[?1049l"))
        self.assertEqual(len({name for name, _ in rows}), len(rows))
        for _, content in rows:
            self.assertNotIn(b"\x1b[6n", content)

    def test_grid_limits_and_dynamic_edge_sources(self):
        for grid in ((19, 91), (61, 91), (25, 39), (25, 161), (True, 91)):
            with self.subTest(grid=grid), self.assertRaises(ValueError):
                probe.phases(*grid)
        cases = dict(probe.phases(30, 120))
        self.assertIn(b"\x1b[30;1HBT", cases["region-sentinels"])
        self.assertEqual(cases["wrap-enabled-edge"], b"\x1b[?7h\x1b[8;120HAB")

    def test_exact_reply_is_preserved_and_split_prefix_waits(self):
        for raw in (b"", b"\x1b", b"\x1b[", b"\x1b[?", b"\x1b[12;", b"\x1b[12;3"):
            with self.subTest(raw=raw):
                self.assertIsNone(probe.reply_observation(raw))
        self.assertEqual(probe.reply_observation(b"\x1b[3;7R"),
                         {"cursor_reply": [3, 7], "cursor_reply_private": False, "response_hex": "1b5b333b3752"})
        self.assertEqual(probe.reply_observation(b"\x1b[?0;5R")["cursor_reply"], [0, 5])
        self.assertTrue(probe.reply_observation(b"\x1b[?0;5R")["cursor_reply_private"])

    def test_duplicate_reply_or_unrelated_bytes_never_become_success(self):
        for raw in (b"\x1b[3;7R\x1b[3;7R", b"private-path", b"\x1b[3;7Rtail", b"\x1b[;7R", b"\x1b[3;7m"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                probe.reply_observation(raw)

    def test_naws_transient_change_is_retained_and_rejected(self):
        peer = probe.Peer.__new__(probe.Peer)
        peer.state = {"columns": 91, "rows": 25}
        peer._record = lambda: None
        peer._on_naws()
        peer.state["columns"] = 92
        peer._on_naws()
        peer.state["columns"] = 91
        peer._on_naws()
        self.assertEqual(peer.state["naws_history"], [[91, 25], [92, 25], [91, 25]])
        peer.state["frozen_grid"] = [91, 25]
        peer._on_naws()
        peer.state["columns"] = 92
        with self.assertRaisesRegex(RuntimeError, "NAWS changed"):
            peer._on_naws()
        self.assertEqual(peer.state["naws_history"][-1], [92, 25])

    def test_peer_namespace_does_not_mutate_older_cases(self):
        from research.ui import alternate_reference, alternate_style_reference
        self.assertIsNot(probe.Peer._serve.__globals__, alternate_reference.Peer._serve.__globals__)
        self.assertIs(probe.Peer._serve.__globals__["phases"], probe.phases)
        self.assertIs(alternate_style_reference.Peer._serve.__globals__["phases"], alternate_style_reference.phases)


class PlanTests(unittest.TestCase):
    def test_false_proposal_does_not_create_attempt_or_target_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "proposal.json"
            path.write_text(json.dumps({"issue": 151, "case_id": probe.CASE_ID,
                                        "maximum_original_runs": 1, "execution_authorized": False}))
            with self.assertRaisesRegex(ValueError, "not been authorized"):
                probe.run_plan(path)
            self.assertEqual([p.name for p in path.parent.iterdir()], ["proposal.json"])

    def test_plan_source_and_deadline_changes_are_rejected(self):
        plan = probe.fixed_contract()
        probe.validate_fixed_contract(plan)
        for key, value in (("phase_count", 26), ("rea_capture_seconds", 46), ("cli_wait_timeout_seconds", 61),
                           ("maximum_original_runs", True), ("whole_lifecycle_bound_seconds", 45)):
            changed = copy.deepcopy(plan)
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                probe.validate_fixed_contract(changed)
        changed = copy.deepcopy(plan)
        changed["example_phases"][0]["source_hex"] = "00"
        with self.assertRaises(ValueError):
            probe.validate_fixed_contract(changed)

    def test_launcher_and_input_identity_keep_protected_older_files(self):
        root = Path(__file__).resolve().parents[2]
        paths = [root / name for name in ("research/ui/alternate_reference.py", "research/ui/r1_launch.py",
                                         "research/ui/qt_r1_text.js")]
        before = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
        script = probe.launcher_source()
        self.assertIn("from research.ui.origin_wrap_reference import Peer", script)
        self.assertIn('with_name("origin_wrap_reference.js")', script)
        self.assertIn('job.assign(info.pid)', script)
        self.assertEqual([hashlib.sha256(path.read_bytes()).hexdigest() for path in paths], before)
        records = probe.phase_records(25, 91)
        self.assertEqual(records[0]["source_hex"], "1b5b324a1b5b484654")
        self.assertEqual(records[0]["dsr_suffix_hex"], "1b5b366e")


if __name__ == "__main__":
    unittest.main()
