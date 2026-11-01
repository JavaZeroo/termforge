"""Pure reentry input, private namespace and immutable-plan guards."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from research.ui import mode_reentry_reference as probe
from research.ui import mode_state_reference as original
from research.ui.test_mode_state_reference import SyntheticConnection, SyntheticListener


def receive_fixture(mode="normal"):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class(probe.CASE_ID)
        peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary)
        (peer.directory / "fixture-ready").write_text("stable-NAWS-capacity")
        peer.state = {"connected": False, "columns": None, "rows": None, "phase": None, "records": [], "case": probe.CASE_ID}
        peer.stop = type("NeverStopped", (), {"is_set": lambda _: False})()
        connection = SyntheticConnection(peer, mode)
        peer.listener = SyntheticListener(connection)
        peer._serve()
        return peer.state, connection.sent


class ReentryTests(unittest.TestCase):
    def test_literal_sequence_separates_duplicate_and_later_entry(self):
        rows = probe.phases(20, 91, probe.CASE_ID)
        self.assertEqual(len(rows), 14)
        self.assertEqual(rows[1][1], b"\x1b[?1049h\x1b[3;7r\x1b[?6h\x1b[2;10HA0")
        self.assertEqual(rows[2][1], b"\x1b[?1049h")
        self.assertEqual(rows[3][1], b"\x1b[3;7r\x1b[1;1HDS\x1b[99;1H")
        self.assertEqual(rows[6][1], b"\x1b[?6h\x1b[2;10HA1")
        self.assertEqual(rows[7][1], b"\x1b[?1049l")
        self.assertEqual(rows[8][1], b"\x1b[?1049h")
        self.assertEqual(rows[9][1], b"\x1b[3;7r\x1b[1;1HES\x1b[99;1H")
        for index, mode, marker in ((4, b"h", b"DI"), (5, b"l", b"DO"), (10, b"h", b"EI"), (11, b"l", b"EO")):
            self.assertEqual(rows[index][1], b"\x1b[?6"+mode+b"\x1b[3;7r\x1b[1;1H"+marker+b"\x1b[99;1H")
        self.assertEqual(len({name for name, _ in rows}), 14)
        self.assertTrue(all(b"\x1b[6n" not in data for _, data in rows))

    def test_receive_loop_has_exact_case_and_source_and_rejects_changed_grid_or_duplicate_reply(self):
        state, sent = receive_fixture()
        self.assertTrue(state["complete"])
        self.assertEqual(state["case"], probe.CASE_ID)
        self.assertEqual(len(state["records"]), 14)
        self.assertEqual(sent[1:], [content+b"\x1b[6n" for _, content in probe.phases(25, 91, probe.CASE_ID)])
        for mode in ("transient-naws", "duplicate-dsr"):
            state, _ = receive_fixture(mode)
            self.assertIn("error", state)
            self.assertFalse(state.get("complete", False))

    def test_new_namespace_does_not_mutate_executed_mode_cases(self):
        old = original.fixed_contract(original.CASES[0])
        fresh_peer = probe.peer_class(probe.CASE_ID)
        self.assertIsNot(fresh_peer._serve.__globals__, original.peer_class(original.CASES[0])._serve.__globals__)
        self.assertEqual(original.fixed_contract(original.CASES[0]), old)
        self.assertEqual(original.CASES, ("main-on-alt-off", "main-off-alt-on"))
        self.assertEqual(probe.SOURCE_FILES, ("research/ui/mode_reentry_reference.py", *original.SOURCE_FILES))

    def test_case_input_generated_driver_and_deadline_cannot_change(self):
        plan = probe.fixed_contract(probe.CASE_ID)
        probe.validate_fixed_contract(plan)
        self.assertEqual(plan["schema"], "termforge-mode-reentry-plan-v1")
        self.assertFalse(plan["semantic_acceptance"])
        self.assertEqual(plan["generated_driver_sha256"], hashlib.sha256(probe.javascript_source().encode()).hexdigest())
        for key, value in (("case_id", original.CASES[0]), ("phase_count", 15), ("maximum_original_runs", 2),
                           ("rea_capture_seconds", 46), ("cli_wait_timeout_seconds", 61), ("generated_driver_sha256", "0"*64)):
            changed = copy.deepcopy(plan)
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                probe.validate_fixed_contract(changed)
        changed = copy.deepcopy(plan)
        changed["example_phases"][3]["source_hex"] = "00"
        with self.assertRaises(ValueError): probe.validate_fixed_contract(changed)

    def test_generated_js_and_actual_launcher_bind_reentry_helpers(self):
        driver = probe.javascript_source()
        self.assertIn('const probeCase="alternate-origin-reentry"', driver)
        self.assertIn("'mode_reentry_phase'", driver)
        self.assertIn("'/mode-reentry-result.json'", driver)
        self.assertNotIn("main-on-alt-off", driver)
        self.assertNotIn("__MODE_STATE_CASE_JSON__", driver)
        source = '''from pathlib import Path
def run():
    if True:
        from research.ui.r1_vt_fixture import Peer
    script = Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")
    return Peer, script
'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "research/ui/r1_launch.py"
            target.parent.mkdir(parents=True)
            target.write_text(source)
            with patch.object(probe, "ROOT", root):
                generated = probe.launcher_source(probe.CASE_ID)
            namespace = {"__file__": str(target)}
            exec(compile(generated, str(target), "exec"), namespace)
            peer, actual_driver = namespace["run"]()
            self.assertEqual(peer.case_id, probe.CASE_ID)
            self.assertEqual(actual_driver, driver)

    def test_archive_preparation_uses_nine_bound_sources_and_false_plan_never_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "official"
            source.mkdir()
            (source / "WindTerm.exe").write_bytes(b"inert original")
            hashes = {}
            for name in probe.SOURCE_FILES:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes((probe.ROOT / name).read_bytes())
                hashes[name] = probe.sha(path)
            archive = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": "b"*40, "source_files_sha256": hashes}
            (root / probe.ARCHIVE_MANIFEST).write_text(json.dumps(archive))
            with patch.dict(probe._RUNTIME, ROOT=root, EXE_SHA256=probe.sha(source / "WindTerm.exe"),
                            rea_identity=lambda _: {"synthetic": True}, host_identity=lambda: {"synthetic_host": True}), \
                 patch.object(probe._RUNTIME["subprocess"], "check_output", side_effect=AssertionError("must not invoke Git")):
                plan_path = root / "local/plan.json"
                plan = probe.prepare_plan(source, root / "local/out", root / "inert-rea.cmd", plan_path, probe.CASE_ID)
                self.assertEqual(plan["source_files_sha256"], hashes)
                self.assertEqual(plan["source_identity"]["kind"], "source-archive")
                self.assertFalse(plan["execution_authorized"])
                with self.assertRaisesRegex(ValueError, "not been authorized"):
                    probe.run_plan(plan_path)
                self.assertFalse(plan_path.with_suffix(".attempted").exists())
                self.assertFalse((root / "local/out").exists())

    def test_grid_and_case_bounds_are_not_reduced_for_new_case(self):
        for rows, columns, case in ((19, 91, probe.CASE_ID), (25, 39, probe.CASE_ID), (25, 91, "other")):
            with self.assertRaises(ValueError): probe.phases(rows, columns, case)


if __name__ == "__main__":
    unittest.main()
