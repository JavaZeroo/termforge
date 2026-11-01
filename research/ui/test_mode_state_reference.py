"""Pure protocol/plan counterexamples; never execute a target or native runtime."""
from __future__ import annotations
import copy
from contextlib import contextmanager, ExitStack
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, Mock

from research.ui import mode_state_reference as probe


def naws(columns, rows):
    return bytes([255, 250, 31]) + columns.to_bytes(2, "big") + rows.to_bytes(2, "big") + bytes([255, 240])


class SyntheticConnection:
    """Deterministic recv fragments; this object never opens a socket."""
    def __init__(self, peer, mode):
        self.peer, self.mode = peer, mode
        self.initial, self.fragment, self.sent = True, None, []

    def settimeout(self, _):
        pass

    def close(self):
        pass

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _):
        if self.initial:
            self.initial = False
            return naws(91, 25)
        if self.mode == "transient-naws":
            return naws(92, 25) + naws(91, 25)
        row = self.peer.state["records"][-1]
        if row["cursor_reply"] is not None:
            (self.peer.directory / f'ack-{row["phase"]}').write_text("observed")
            import socket
            raise socket.timeout()
        if self.mode == "duplicate-dsr":
            return b"\x1b[3;7R\x1b[4;8R"
        if self.fragment is not None:
            value, self.fragment = self.fragment, None
            return value
        self.fragment = f'{row["phase"]+1};7R'.encode()
        return b"\x1b["


class SyntheticListener:
    def __init__(self, connection):
        self.connection = connection

    def accept(self):
        return self.connection, ("127.0.0.1", 12345)


def receive_fixture(mode, case=probe.CASES[0]):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class(case)
        peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary)
        (peer.directory / "fixture-ready").write_text("stable-NAWS-capacity")
        peer.state = {"connected": False, "columns": None, "rows": None, "phase": None, "records": [], "case": case}
        peer.stop = type("NeverStopped", (), {"is_set": lambda _: False})()
        connection = SyntheticConnection(peer, mode)
        peer.listener = SyntheticListener(connection)
        peer._serve()
        return peer.state, connection.sent


@contextmanager
def synthetic_run_plan(archive=False):
    """Exercise the actual run_plan/compiled-launcher seam with inert fake files."""
    with tempfile.TemporaryDirectory() as temporary, ExitStack() as patches:
        directory = Path(temporary).resolve()
        (directory / "local").mkdir()
        (directory / "source").mkdir()
        (directory / "source/WindTerm.exe").write_bytes(b"inert synthetic original")
        (directory / "probe.py").write_bytes(b"fixed synthetic source")
        nodes = [directory / f"node-{name}.exe" for name in ("a", "b")]
        for index, node in enumerate(nodes):
            node.write_bytes(f"inert synthetic Node {index}".encode())
        rea = directory / "tools/node_modules/.bin/rea.cmd"
        rea.parent.mkdir(parents=True)
        rea.write_bytes(b"inert synthetic REA shim")
        package = rea.parent.parent / "rea-agents"
        (package / "scripts").mkdir(parents=True)
        (package / "package.json").write_text(json.dumps({"name": "rea-agents", "version": "6.7.0", "bin": {"rea": "scripts/rea.mjs"}}))
        (package / "scripts/rea.mjs").write_bytes(b"inert synthetic REA entry")
        launcher = '''import shutil
def cli_command(rea):
    return [shutil.which("node"), str(rea.parent.parent / "rea-agents/scripts/rea.mjs")]
def run(source, out, rea, seconds, hosted, scenario):
    from research.ui import mode_state_reference as probe
    probe.capture_test_cli_command(lambda: cli_command(rea), out, seconds, hosted, scenario)
'''
        patches.enter_context(patch.object(probe, "ROOT", directory))
        patches.enter_context(patch.object(probe, "SOURCE_FILES", ("probe.py",)))
        patches.enter_context(patch.object(probe, "EXE_SHA256", probe.sha(directory / "source/WindTerm.exe")))
        patches.enter_context(patch.object(probe, "launcher_source", return_value=launcher))
        patches.enter_context(patch.object(probe, "host_identity", return_value={"python_sha256": "a"*64,
            "python_version": "synthetic", "windows_version": "synthetic"}))
        if archive:
            manifest = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": "b"*40,
                        "source_files_sha256": {"probe.py": probe.sha(directory / "probe.py")}}
            (directory / probe.ARCHIVE_MANIFEST).write_text(json.dumps(manifest), encoding="utf-8")
            patches.enter_context(patch.object(probe.subprocess, "check_output",
                                               side_effect=AssertionError("archive must not invoke Git")))
        else:
            patches.enter_context(patch.object(probe.subprocess, "check_output", return_value="b"*40))
        # Patch the shared stdlib selection seam used by both the original and fixed launcher.
        which = patches.enter_context(patch("shutil.which", return_value=str(nodes[0])))
        observed = []
        def record(command, out, *settings):
            observed.append((command(), settings))
            out.mkdir()
        capture = patches.enter_context(patch.object(probe, "capture_test_cli_command", create=True,
                                                    new=Mock(side_effect=record)))
        patches.enter_context(patch.object(probe, "audit_mechanics", return_value={"synthetic_only": True}))
        path, out = directory / "local/plan.json", directory / "local/out"
        plan = probe.prepare_plan(directory / "source", out, rea, path, probe.CASES[0], execution_authorized=True)
        yield path, out, plan, nodes, which, capture, observed


class ProtocolTests(unittest.TestCase):
    def test_receive_loop_retains_split_replies_for_every_actual_source(self):
        state, sent = receive_fixture("normal")
        self.assertTrue(state["complete"])
        self.assertTrue(state["closed"])
        self.assertNotIn("error", state)
        self.assertEqual(state["naws_history"], [[91, 25]])
        self.assertEqual(state["freeze_event_count"], 1)
        self.assertEqual(state["frozen_grid"], [91, 25])
        self.assertEqual(len(state["records"]), 14)
        self.assertEqual([r["cursor_reply"] for r in state["records"]], [[n+1, 7] for n in range(14)])
        self.assertEqual(sent[0], bytes([255, 251, 1, 255, 251, 3, 255, 253, 31]))
        self.assertEqual(sent[1], probe.phases(25, 91, probe.CASES[0])[0][1]+b"\x1b[6n")
        self.assertEqual(len(sent), 15)

    def test_receive_loop_rejects_transient_naws_before_advancing(self):
        state, _ = receive_fixture("transient-naws")
        self.assertEqual(state["error"], "NAWS changed after fixture freeze")
        self.assertEqual(state["naws_history"], [[91, 25], [92, 25]])
        self.assertEqual(len(state["records"]), 1)
        self.assertFalse(state.get("complete", False))

    def test_receive_loop_keeps_duplicate_raw_and_fails_without_ack(self):
        state, _ = receive_fixture("duplicate-dsr")
        self.assertEqual(state["error"], "Unexpected DSR response bytes")
        self.assertEqual(state["records"][0]["response_hex"], b"\x1b[3;7R\x1b[4;8R".hex())
        self.assertEqual(state["received_hex"], b"\x1b[3;7R\x1b[4;8R".hex())
        self.assertFalse(state.get("complete", False))

    def test_case_bytes_are_asymmetric_and_samples_precede_controls(self):
        for case, main_mode, alt_mode in ((probe.CASES[0], b"h", b"l"), (probe.CASES[1], b"l", b"h")):
            rows = probe.phases(25, 91, case)
            self.assertEqual(len(rows), 14)
            self.assertEqual(rows[1][1], b"\x1b[2;5r\x1b[?6"+main_mode+b"\x1b[2;10HMP")
            self.assertEqual(rows[2][1], b"\x1b[?1049h")
            self.assertEqual(rows[3][1], b"\x1b[3;7r\x1b[1;1HAS\x1b[99;1H")
            self.assertEqual(rows[4][1], b"\x1b[?6h\x1b[3;7r\x1b[1;1HAI\x1b[99;1H")
            self.assertEqual(rows[5][1], b"\x1b[?6l\x1b[3;7r\x1b[1;1HAO\x1b[99;1H")
            self.assertEqual(rows[6][1], b"\x1b[?6"+alt_mode+b"\x1b[2;10HAP")
            self.assertEqual(rows[7][1], b"\x1b[?1049l")
            self.assertEqual(rows[8][1], b"\x1b[1;1HPS\x1b[99;1H")
            self.assertEqual(rows[12][1], b"\r\nNW")
            self.assertEqual(len({name for name, _ in rows}), len(rows))
            for _, content in rows:
                self.assertNotIn(b"\x1b[6n", content)

    def test_grid_limits_and_dynamic_edge_sources(self):
        for grid in ((19, 91), (61, 91), (25, 39), (25, 161), (True, 91)):
            with self.subTest(grid=grid), self.assertRaises(ValueError):
                probe.phases(*grid, probe.CASES[0])
        cases = dict(probe.phases(30, 120, probe.CASES[1]))
        self.assertIn(b"\x1b[30;1HSB", cases["primary-bottom-sentinels"])
        for invalid in (None, "", "main-on-alt-on", True):
            with self.subTest(case=invalid), self.assertRaises(ValueError):
                probe.phases(25, 91, invalid)

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
        peer_type = probe.peer_class(probe.CASES[0])
        peer = peer_type.__new__(peer_type)
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
        from research.ui import alternate_reference, origin_wrap_reference
        first, second = [probe.peer_class(case) for case in probe.CASES]
        self.assertIsNot(first._serve.__globals__, second._serve.__globals__)
        self.assertIsNot(first._serve.__globals__, alternate_reference.Peer._serve.__globals__)
        for case, bound in zip(probe.CASES, (first, second)):
            self.assertEqual(bound._serve.__globals__["phases"](25, 91), probe.phases(25, 91, case))
            state, sent = receive_fixture("normal", case)
            self.assertTrue(state["complete"])
            self.assertEqual(state["case"], case)
            self.assertEqual(sent[2], probe.phases(25, 91, case)[1][1]+b"\x1b[6n")
        self.assertIs(origin_wrap_reference.Peer._serve.__globals__["phases"], origin_wrap_reference.phases)


class PlanTests(unittest.TestCase):
    def test_source_archive_helper_uses_explicit_probe_root_and_file_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "other-probe.py").write_bytes(b"independent source")
            manifest = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": "d"*40,
                        "source_files_sha256": {"other-probe.py": probe.sha(directory / "other-probe.py")}}
            (directory / probe.ARCHIVE_MANIFEST).write_text(json.dumps(manifest), encoding="utf-8")
            with patch.object(probe.subprocess, "check_output", side_effect=AssertionError("must not use Git")):
                identity = probe.source_identity(root=directory, source_files=("other-probe.py",))
            self.assertEqual(identity["source_base_commit"], "d"*40)
            self.assertEqual(identity["source_files_sha256"], manifest["source_files_sha256"])

    def test_archive_prepare_and_run_verify_real_file_hashes_without_git(self):
        with synthetic_run_plan(archive=True) as (path, out, plan, _, _, capture, _):
            self.assertEqual(plan["source_identity"]["kind"], "source-archive")
            self.assertEqual(plan["source_identity"]["manifest"], "reference-source.json")
            self.assertEqual(plan["source_identity"]["manifest_sha256"], probe.sha(probe.ROOT / probe.ARCHIVE_MANIFEST))
            self.assertEqual(plan["source_base_commit"], "b"*40)
            probe.run_plan(path)
            capture.assert_called_once()
            self.assertTrue((out / "mechanics-check.json").is_file())

    def test_archive_changed_bytes_or_manifest_are_rejected_before_attempt(self):
        for changed in ("source", "manifest", "extra-file", "commit"):
            with self.subTest(changed=changed), synthetic_run_plan(archive=True) as (path, out, _, _, _, capture, _):
                manifest_path = probe.ROOT / probe.ARCHIVE_MANIFEST
                if changed == "source":
                    (probe.ROOT / "probe.py").write_bytes(b"changed source")
                elif changed == "manifest":
                    manifest_path.write_text(manifest_path.read_text()+"\n", encoding="utf-8")
                else:
                    manifest = json.loads(manifest_path.read_text())
                    if changed == "extra-file":
                        manifest["source_files_sha256"]["unlisted.py"] = "a"*64
                    else:
                        manifest["source_base_commit"] = "c"*40
                    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(ValueError):
                    probe.run_plan(path)
                self.assertFalse(path.with_suffix(".attempted").exists())
                self.assertFalse(out.exists())
                capture.assert_not_called()

    def test_archive_cannot_be_substituted_for_prepared_checkout(self):
        with synthetic_run_plan() as (path, out, plan, _, _, capture, _):
            self.assertEqual(plan["source_identity"], {"kind": "git-checkout"})
            manifest = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": plan["source_base_commit"],
                        "source_files_sha256": plan["source_files_sha256"]}
            (probe.ROOT / probe.ARCHIVE_MANIFEST).write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Source identity changed"):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix(".attempted").exists())
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_invalid_archive_blocks_preparation_without_new_plan_or_output(self):
        with synthetic_run_plan(archive=True) as (_, _, plan, _, _, capture, _):
            (probe.ROOT / probe.ARCHIVE_MANIFEST).write_text("{}", encoding="utf-8")
            destination, out = probe.ROOT / "local/rejected.json", probe.ROOT / "local/rejected-out"
            with self.assertRaises(ValueError):
                probe.prepare_plan(Path(plan["source"]), out, Path(plan["rea"]), destination, probe.CASES[1])
            self.assertFalse(destination.exists())
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_archive_is_reverified_after_launcher_before_mechanics_success(self):
        with synthetic_run_plan(archive=True) as (path, out, _, _, _, capture, _):
            def mutate_manifest(command, destination, *settings):
                destination.mkdir()
                manifest = probe.ROOT / probe.ARCHIVE_MANIFEST
                manifest.write_text(manifest.read_text()+"\n", encoding="utf-8")
            capture.side_effect = mutate_manifest
            with self.assertRaisesRegex(ValueError, "Source identity changed"):
                probe.run_plan(path)
            self.assertTrue(path.with_suffix(".attempted").exists())
            self.assertFalse((out / "mechanics-check.json").exists())

    def test_archive_schema_file_set_hash_and_commit_counterexamples(self):
        with synthetic_run_plan(archive=True):
            path = probe.ROOT / probe.ARCHIVE_MANIFEST
            original = json.loads(path.read_text())
            mutations = [
                lambda p: p.update(schema="unknown"),
                lambda p: p.update(extra="not allowed"),
                lambda p: p.pop("source_base_commit"),
                lambda p: p.update(source_base_commit=True),
                lambda p: p.update(source_base_commit="b"*39),
                lambda p: p.update(source_files_sha256=[]),
                lambda p: p["source_files_sha256"].pop("probe.py"),
                lambda p: p["source_files_sha256"].update({"probe.py": "F"*64}),
                lambda p: p["source_files_sha256"].update({"probe.py": "a"*64}),
                lambda p: p["source_files_sha256"].update({"../private": "a"*64}),
            ]
            for index, mutate in enumerate(mutations):
                value = copy.deepcopy(original)
                mutate(value)
                path.write_text(json.dumps(value), encoding="utf-8")
                with self.subTest(index=index), self.assertRaises(ValueError):
                    probe.source_identity()
            encoded = json.dumps(original)
            for raw in (encoded.replace('{', '{"schema":"duplicate",', 1),
                        encoded.replace('"probe.py":', '"probe.py":"'+"a"*64+'", "probe.py":', 1),
                        " "*16385, "[]"):
                path.write_text(raw, encoding="utf-8")
                with self.subTest(raw=raw[:30]), self.assertRaises(ValueError):
                    probe.source_identity()

    def test_case_change_is_rejected_before_any_attempt(self):
        with synthetic_run_plan() as (path, out, plan, _, _, capture, _):
            plan["case_id"] = probe.CASES[1]
            path.write_text(json.dumps(plan), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Fixed source/bounds differ"):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix(".attempted").exists())
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_attempt_marker_prevents_reuse_before_launcher(self):
        with synthetic_run_plan() as (path, out, _, _, _, capture, _):
            path.with_suffix(".attempted").write_text("prior controlled attempt")
            with self.assertRaises(FileExistsError):
                probe.run_plan(path)
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_actual_case_launcher_compiles_and_binds_peer_and_injected_js(self):
        # The protected launcher substitutions execute against an inert shell;
        # no native imports, peer construction, socket or target are permitted.
        source = '''from pathlib import Path
def run():
    if True:
        from research.ui.r1_vt_fixture import Peer
    script = Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")
    return Peer, script
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            target = directory / "research/ui"
            target.mkdir(parents=True)
            launcher = target / "r1_launch.py"
            launcher.write_text(source)
            (target / "mode_state_reference.js").write_text("const probeCase=__MODE_STATE_CASE_JSON__;")
            for case in probe.CASES:
                with patch.object(probe, "ROOT", directory):
                    generated = probe.launcher_source(case)
                namespace = {"__file__": str(launcher)}
                exec(compile(generated, str(launcher), "exec"), namespace)
                peer, script = namespace["run"]()
                self.assertEqual(peer.case_id, case)
                self.assertEqual(peer._serve.__globals__["phases"](25, 91), probe.phases(25, 91, case))
                self.assertEqual(script, "const probeCase="+json.dumps(case)+";")

    def test_node_selection_path_drift_is_rejected_before_attempt(self):
        with synthetic_run_plan() as (path, out, _, nodes, which, capture, _):
            which.return_value = str(nodes[1])
            with self.assertRaisesRegex(ValueError, "REA entry changed"):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix(".attempted").exists())
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_selected_node_file_drift_is_rejected_before_attempt(self):
        with synthetic_run_plan() as (path, out, _, nodes, _, capture, _):
            nodes[0].write_bytes(b"changed inert Node bytes")
            with self.assertRaisesRegex(ValueError, "REA entry changed"):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix(".attempted").exists())
            self.assertFalse(out.exists())
            capture.assert_not_called()

    def test_actual_launcher_uses_recorded_executable_after_path_changes_late(self):
        with synthetic_run_plan() as (path, out, plan, nodes, which, capture, observed):
            def change_path_then_record(command, destination, *settings):
                which.return_value = str(nodes[1])
                observed.append((command(), settings))
                destination.mkdir()
            capture.side_effect = change_path_then_record
            probe.run_plan(path)
            self.assertEqual(observed, [([str(nodes[0]), str(Path(plan["rea"]).parent.parent / "rea-agents/scripts/rea.mjs")],
                                         (45, False, "menu-wrap"))])
            self.assertEqual(which.call_count, 2)  # preparation and pre-marker verification only
            self.assertTrue(path.with_suffix(".attempted").is_file())
            self.assertTrue((out / "mechanics-check.json").is_file())

    def test_mechanical_audit_refuses_partial_or_changed_observation_despite_cli_exit_zero(self):
        state, _ = receive_fixture("normal")
        observations = [{key: row[key] for key in ("phase", "label", "cursor_reply", "cursor_reply_private", "response_hex")}
                        for row in state["records"]]
        for observation in observations:
            observation.update(frozen_grid=[91, 25], terminal={"connected": True, "protocol": "Telnet", "capacity": 25,
                                                               "line_count": 1, "lines": ["synthetic"]})
        result = {"case": probe.CASES[0], "status": "observed-all-mode-state-phases", "error": None, "observations": observations,
                  "presented_done": None, "full_active_grid_verified": False, "terminal_style_values_verified": False,
                  "local_shell_transport_verified": False}
        data = {"peer.json": state, "mode-state-result.json": result, "cleanup.json": {"active": 0, "total": 3, "terminated": 0},
                "profile-check.json": {"only_synthetic_session": True, "session_count": 1}}
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for row in state["records"]:
                (directory / f'ack-{row["phase"]}').write_text("observed")
            (directory / "stderr.txt").write_bytes(b"")
            def save(values):
                for name, value in values.items():
                    (directory / name).write_text(json.dumps(value), encoding="utf-8")
            save(data)
            self.assertFalse(probe.audit_mechanics(directory, probe.CASES[0])["semantic_acceptance"])
            for key, field, value in (("mode-state-result.json", "status", "failed"),
                                      ("mode-state-result.json", "case", probe.CASES[1]),
                                      ("peer.json", "case", probe.CASES[1]),
                                      ("mode-state-result.json", "observations", observations[:-1]),
                                      ("peer.json", "received_hex", state["received_hex"] + "00"),
                                      ("cleanup.json", "active", 1),
                                      ("profile-check.json", "only_synthetic_session", False)):
                changed = copy.deepcopy(data)
                changed[key][field] = value
                save(changed)
                with self.subTest(field=field), self.assertRaises(ValueError):
                    probe.audit_mechanics(directory, probe.CASES[0])
            save(data)
            (directory / "stderr.txt").write_bytes(b"controlled error")
            with self.assertRaises(ValueError):
                probe.audit_mechanics(directory, probe.CASES[0])

    def test_false_proposal_does_not_create_attempt_or_target_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "proposal.json"
            path.write_text(json.dumps({"issue": 27, "case_id": probe.CASES[0],
                                        "maximum_original_runs": 1, "execution_authorized": False}))
            with self.assertRaisesRegex(ValueError, "not been authorized"):
                probe.run_plan(path)
            self.assertEqual([p.name for p in path.parent.iterdir()], ["proposal.json"])

    def test_plan_source_and_deadline_changes_are_rejected(self):
        plan = probe.fixed_contract(probe.CASES[0])
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
        script = probe.launcher_source(probe.CASES[0])
        self.assertIn("from research.ui.mode_state_reference import peer_class", script)
        self.assertIn('with_name("mode_state_reference.js")', script)
        self.assertIn('job.assign(info.pid)', script)
        self.assertEqual([hashlib.sha256(path.read_bytes()).hexdigest() for path in paths], before)
        records = probe.phase_records(probe.CASES[0], 25, 91)
        self.assertTrue(records[0]["source_hex"].startswith("1b5b3f366c"))
        self.assertEqual(records[0]["dsr_suffix_hex"], "1b5b366e")


if __name__ == "__main__":
    unittest.main()
