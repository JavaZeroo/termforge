"""Synthetic publication guards; no original, REA, Node or native execution."""
from __future__ import annotations
import copy
from contextlib import ExitStack, contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import export_mode_state_reference as exporter
from research.ui import mode_state_reference as probe
from research.ui.test_mode_state_reference import receive_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data["mode-state-result.json"]
        case = result["case"]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "mode-state-v1", "case": case},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "mode_state_phase", "case": case, **copy.deepcopy(row)} for row in result["observations"])
        payloads.append({"kind": "mode_state_result", **copy.deepcopy(result)})
        data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
            {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def fixture(root, case=probe.CASES[0]):
    directory = (root / "capture").resolve()
    directory.mkdir()
    plan = {**probe.fixed_contract(case), "execution_authorized": True, "out": str(directory), "source": str(root / "official")}
    peer, _ = receive_fixture("normal", case)
    observations = []
    for row in peer["records"]:
        terminal = {"connected": True, "protocol": "Telnet", "capacity": 25, "line_count": 1,
                    "lines": ["R01      MP"], "top": None, "scroll": 0, "is_alt": None, "is_main": None,
                    "caret_api": {"caretLine": 0, "caretColumn": 0, "caretPosition": 11},
                    "cell_styles": None, "current_style": None,
                    "markers": [{"label": label, "document_line": 0, "column": column, "position": column,
                                 "rect": {"x": column*8, "y": -18, "width": 8, "height": 18}}
                                for label, column in (("MP", 9), ("R01", 0))]}
        observation = {key: row[key] for key in ("phase", "label", "cursor_reply", "cursor_reply_private", "response_hex")}
        observations.append({**observation, "frozen_grid": [91, 25], "terminal": terminal})
    result = {"case": case, "status": "observed-all-mode-state-phases", "error": None, "observations": observations,
              "full_active_grid_verified": False, "terminal_style_values_verified": False,
              "local_shell_transport_verified": False, "presented_done": None}
    run = {"pid": 12, "tid": 34, "window_station": "TermForgeR1-1234", "sha256": probe.EXE_SHA256,
           "files_sha256": probe.MANIFEST_SHA256, "scenario": "menu-wrap", "profile": "synthetic-local-only",
           "clipboard": "owned-window-station", "fixture_sha256": probe.sha(exporter.ROOT / "research/ui/r1_vt_fixture.py")}
    base = (exporter.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    driver = (exporter.ROOT / "research/ui/mode_state_reference.js").read_text(encoding="utf-8")
    template = base+driver.replace("__MODE_STATE_CASE_JSON__", json.dumps(case))
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(plan, run, exporter.ROOT)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    normalized = {"messages_truncated": False, "cleanup_error": None, "source_sha256": run["script_sha256"],
                  "mode": "attach", "source_kind": "file", "state": "running", "pid": 12,
                  "source_path": str(directory / "probe.js"), "scripts": [{"script_id": "synthetic-script"}]}
    evidence = {"provider": {"id": "frida", "name": "Frida", "version": None}, "confidence": "observed",
                "operation": "instrument_with_frida", "subject": None, "environment": None, "raw_result": None,
                "evidence_id": "ev_"+"a"*64, "normalized_result": normalized}
    data = {"peer.json": peer, "mode-state-result.json": result, "run.json": run,
            "cleanup.json": {"active": 0, "total": 3, "terminated": 0},
            "profile-check.json": {"only_synthetic_session": True, "session_count": 1},
            "evidence.json": {"ok": True, "data": {"cleanup_error": None, "evidence": evidence}}}
    (directory / "stderr.txt").write_bytes(b"")
    (directory / "fixture-ready").write_text("stable-NAWS-capacity")
    for row in peer["records"]:
        (directory / f'ack-{row["phase"]}').write_text("observed")
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory, case)))
    return directory, data, plan


@contextmanager
def complete_fixture(root):
    """Inert files substitute only the published package pin, never validation logic."""
    official = root / "official"
    official.mkdir()
    for name in exporter.MODULES:
        (official / name).write_bytes(("inert synthetic "+name).encode())
    files = {name: exporter.sha(official / name) for name in exporter.MODULES}
    manifest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    with ExitStack() as patches:
        patches.enter_context(patch.object(probe, "EXE_SHA256", files["WindTerm.exe"]))
        patches.enter_context(patch.object(probe, "MANIFEST_SHA256", manifest))
        patches.enter_context(patch.object(exporter, "FILE_COUNT", len(files)))
        directory, data, plan = fixture(root)
        app = directory / "app"
        app.mkdir()
        for name in files:
            (app / name).write_bytes((official / name).read_bytes())
        (directory / "files.json").write_text(json.dumps(files))
        sessions = directory / "profile/.wind/profiles/default.v10/terminal/user.sessions"
        sessions.parent.mkdir(parents=True)
        sessions.write_text(json.dumps([{"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK",
            "session.protocol": "Telnet", "session.target": "127.0.0.1", "session.port": 12345,
            "session.uuid": "00000000-0000-4000-8000-000000000001"}]))
        source_root = root / "source-archive"
        hashes = {}
        for name in probe.SOURCE_FILES:
            path = source_root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((exporter.ROOT / name).read_bytes())
            hashes[name] = exporter.sha(path)
        archive = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": "b"*40, "source_files_sha256": hashes}
        archive_path = source_root / probe.ARCHIVE_MANIFEST
        archive_path.write_text(json.dumps(archive), encoding="utf-8")
        plan.update(probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES))
        plan.update(launcher_source_sha256=hashlib.sha256(probe.launcher_source(plan["case_id"]).encode()).hexdigest(),
                    windows_version="10.0.26300", python_version="3.12.10 (synthetic runtime)", python_sha256="c"*64,
                    rea_prepared_identity={"version": "6.7.0", "node_executable_sha256": "d"*64,
                        "shim_sha256": "e"*64, "package_json_sha256": "f"*64, "entry_sha256": "0"*64})
        plan_path = root / "executed.json"
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        plan_path.with_suffix(".attempted").write_text("One original launch attempt; retain after failure and never retry this plan.\n")
        yield directory, data, plan, plan_path, source_root, official


class CaptureTests(unittest.TestCase):
    def test_both_cases_publish_bounded_observations_without_classification(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                directory, _, plan = fixture(Path(temporary), case)
                result = exporter.capture_facts(directory, plan)
                self.assertEqual(len(result["phases"]), 14)
                self.assertEqual(result["dsr_request_bytes"], 56)
                payload = json.dumps(result)
                for private in (str(directory), "TermForgeR1-1234", '"pid"', '"tid"', "source_path", "session.uuid"):
                    self.assertNotIn(private, payload)
                self.assertIsNone(result["phases"][0]["document_projection"]["mode_getters"]["is_alt"])

    def test_private_text_and_marker_payload_are_rejected(self):
        for changed in ("text", "uppercase", "marker", "marker-binding", "rectangle", "caret", "getter"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary))
                terminal = data["mode-state-result.json"]["observations"][0]["terminal"]
                if changed in ("text", "uppercase"):
                    terminal["lines"] = ["C:/private/account" if changed == "text" else "SECRET"]
                elif changed == "marker":
                    terminal["markers"][0]["label"] = "PRIVATE"
                elif changed == "marker-binding":
                    terminal["markers"][0]["column"] = 1
                elif changed == "rectangle":
                    terminal["markers"][0]["rect"]["x"] = "C:/private"
                elif changed == "caret":
                    terminal["caret_api"]["caretPosition"] = "private"
                else:
                    terminal["is_alt"] = "private"
                save(directory, data)
                with self.assertRaises(ValueError):
                    exporter.capture_facts(directory, plan)

    def test_unlisted_metadata_is_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary))
            before = exporter.capture_facts(directory, plan)
            data["mode-state-result.json"]["observations"][0]["terminal"]["private_note"] = "SECRET"
            data["cleanup.json"]["private_path"] = "C:/private"
            save(directory, data)
            self.assertEqual(exporter.capture_facts(directory, plan), before)

    def test_wrong_identity_partial_case_and_provider_are_rejected(self):
        for changed in ("exe", "manifest", "partial", "case", "script", "template", "evidence", "truncated", "cleanup", "source-path"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary))
                evidence = data["evidence.json"]["data"]["evidence"]
                if changed == "exe": data["run.json"]["sha256"] = "0"*64
                elif changed == "manifest": data["run.json"]["files_sha256"] = "0"*64
                elif changed == "partial": data["mode-state-result.json"]["observations"].pop()
                elif changed == "case": data["mode-state-result.json"]["case"] = probe.CASES[1]
                elif changed == "script": (directory / "probe.js").write_bytes(b"changed")
                elif changed == "template": data["run.json"]["template_sha256"] = "0"*64
                elif changed == "evidence": evidence["evidence_id"] = "private"
                elif changed == "truncated": evidence["normalized_result"]["messages_truncated"] = True
                elif changed == "cleanup": data["cleanup.json"]["active"] = 1
                elif changed == "source-path": evidence["normalized_result"]["source_path"] = "C:/private/other.js"
                save(directory, data)
                with self.assertRaises(ValueError):
                    exporter.capture_facts(directory, plan)

    def test_cli_refuses_private_text_in_normal_and_optimized_python_without_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, data, plan = fixture(root)
            data["mode-state-result.json"]["observations"][0]["terminal"]["lines"] = ["SECRET"]
            save(directory, data)
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan))
            for optimized in (False, True):
                destination = root / ("optimized/out.json" if optimized else "normal/out.json")
                command = [sys.executable, "-B", *(["-O"] if optimized else []), "-X", "utf8", "-m",
                    "research.ui.export_mode_state_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=exporter.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed synthetic document text", completed.stderr)
                self.assertFalse(destination.parent.exists())


class CompleteExportTests(unittest.TestCase):
    def test_relocated_capture_checks_original_paths_without_rewriting_recordings(self):
        with tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as values:
            directory, _, _, plan_path, source_root, official = values
            source_before = {name: exporter.sha(source_root / name) for name in probe.SOURCE_FILES}
            raw_before = {name: exporter.sha(directory / name) for name in exporter.RAW_NAMES}
            moved = directory.with_name("transferred-capture")
            directory.rename(moved)
            destination = Path(temporary) / "public/moved.json"
            exporter.export(moved, plan_path, destination, exporter.sha(plan_path), source_root, official)
            self.assertEqual({name: exporter.sha(moved / name) for name in exporter.RAW_NAMES}, raw_before)
            self.assertEqual({name: exporter.sha(source_root / name) for name in probe.SOURCE_FILES}, source_before)

    def test_complete_archive_audit_exports_hashes_and_keeps_unknowns(self):
        with tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as values:
            directory, _, _, plan_path, source_root, official = values
            destination = Path(temporary) / "public/result.json"
            result = exporter.export(directory, plan_path, destination, exporter.sha(plan_path), source_root, official)
            self.assertFalse(result["semantic_acceptance"])
            self.assertIsNone(result["origin_state_classification"])
            self.assertIsNone(result["margin_restoration_classification"])
            self.assertEqual(result["provenance"]["source_identity"]["kind"], "source-archive")
            self.assertEqual(set(result["provenance"]["reference_identity"]["module_files_sha256"]), set(exporter.MODULES))
            payload = destination.read_text()
            self.assertNotIn(str(Path(temporary)), payload)
            self.assertNotIn('"pid"', payload)

    def test_plan_package_module_archive_or_profile_mutation_never_creates_output(self):
        for changed in ("plan-pin", "copied-module", "source-module", "manifest", "archive-source", "archive-manifest", "profile", "attempt"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = exporter.sha(plan_path)
                if changed == "plan-pin": expected = "0"*64
                elif changed == "copied-module": (directory / "app/Qt5Gui.dll").write_bytes(b"changed")
                elif changed == "source-module": (official / "Qt5Core.dll").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text("{}")
                elif changed == "archive-source": (source_root / "research/ui/mode_state_reference.py").write_bytes(b"changed")
                elif changed == "archive-manifest":
                    path = source_root / probe.ARCHIVE_MANIFEST
                    path.write_text(path.read_text()+"\n")
                elif changed == "profile":
                    (directory / "profile/.wind/profiles/default.v10/terminal/user.sessions").write_text("[]")
                elif changed == "attempt": plan_path.with_suffix(".attempted").write_text("changed")
                destination = Path(temporary) / "refused/result.json"
                with self.assertRaises(ValueError):
                    exporter.export(directory, plan_path, destination, expected, source_root, official)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
