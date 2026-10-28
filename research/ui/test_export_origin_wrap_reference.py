"""Synthetic publication counterexamples; no original/REA/Node/native execution."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from research.ui import export_origin_wrap_reference as exporter
from research.ui import origin_wrap_reference as probe
from research.ui.test_origin_wrap_reference import receive_fixture


def fixture(root):
    directory = (root / "capture").resolve()
    directory.mkdir()
    peer, _ = receive_fixture("normal")
    peer.update(naws_history=[[91, 24], [91, 25]], freeze_event_count=2)
    observations = []
    for row in peer["records"]:
        observation = {key: row[key] for key in ("phase", "label", "cursor_reply", "cursor_reply_private", "response_hex")}
        observation.update(frozen_grid=[91, 25], terminal={"connected": True, "protocol": "Telnet", "capacity": 25,
            "line_count": 1, "lines": ["FT"], "top": None, "scroll": 0,
            "caret_api": {"caretLine": 0, "caretColumn": 0, "caretPosition": 2}, "cell_styles": None, "current_style": None})
        observations.append(observation)
    result = {"status": "observed-all-origin-wrap-phases", "error": None, "observations": observations,
        "full_active_grid_verified": False, "terminal_style_values_verified": False,
        "local_shell_transport_verified": False, "presented_done": None}
    run = {"pid": 12, "tid": 34, "window_station": "TermForgeR1-1234", "sha256": probe.EXE_SHA256,
        "files_sha256": probe.MANIFEST_SHA256, "scenario": "menu-wrap", "profile": "synthetic-local-only",
        "clipboard": "owned-window-station"}
    template = (exporter.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += (exporter.ROOT / "research/ui/origin_wrap_reference.js").read_text(encoding="utf-8")
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(directory, run)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    normalized = {"messages_truncated": False, "cleanup_error": None, "source_sha256": run["script_sha256"],
        "mode": "attach", "source_kind": "file", "state": "running", "pid": 12,
        "source_path": str(directory / "probe.js"), "scripts": [{"script_id": "synthetic-script", "name": "synthetic"}]}
    evidence = {"provider": {"id": "frida", "name": "Frida", "version": None}, "confidence": "observed",
        "operation": "instrument_with_frida", "subject": None, "environment": None, "raw_result": None,
        "evidence_id": "ev_" + "a"*64, "normalized_result": normalized}
    data = {"peer.json": peer, "origin-wrap-result.json": result, "run.json": run,
        "cleanup.json": {"active": 0, "total": 3, "terminated": 0},
        "profile-check.json": {"only_synthetic_session": True, "session_count": 1},
        "evidence.json": {"ok": True, "data": {"cleanup_error": None, "evidence": evidence}}}
    (directory / "stderr.txt").write_bytes(b"")
    (directory / "fixture-ready").write_text("stable-NAWS-capacity")
    for row in peer["records"]:
        (directory / f'ack-{row["phase"]}').write_text("observed")
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory)))
    return directory, data


def save(directory, data, synchronize=True):
    if synchronize:
        normalized = data["evidence.json"]["data"]["evidence"]["normalized_result"]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": probe.CASE_ID}]
        payloads += [{"kind": "readiness", "phase": "ui", "ready": value, "has_tree": True,
                      "has_engine": True, "has_editor": False, "peer_ready": False} for value in (False, False, True)]
        payloads += [{"kind": "origin_wrap_phase", **copy.deepcopy(row)} for row in data["origin-wrap-result.json"]["observations"]]
        payloads += [{"kind": "origin_wrap_result", **copy.deepcopy(data["origin-wrap-result.json"])}]
        normalized["messages"] = [{"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


class ExportTests(unittest.TestCase):
    def test_existing_parent_alias_fixture_is_valid_for_direct_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            alias_component = root / "existing path spelling component"
            alias_component.mkdir()
            alias = alias_component / ".."
            self.assertNotEqual(alias, alias.resolve())
            directory, _ = fixture(alias)
            result = exporter.capture_facts(directory)
            self.assertEqual(len(result["phases"]), 25)
            self.assertEqual(directory, directory.resolve())

    def test_valid_capture_projects_safe_fields_without_paths_or_process_ids(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data = fixture(Path(temporary))
            result = exporter.capture_facts(directory)
            self.assertEqual(len(result["phases"]), 25)
            self.assertEqual(result["source_bytes"], 334)
            self.assertEqual(result["job_after"], {"active": 0, "total": 3, "terminated": 0})
            payload = json.dumps(result)
            for private in (str(directory), "TermForgeR1-1234", "source_path", '"pid"', '"tid"'):
                self.assertNotIn(private, payload)
            self.assertFalse(result["provider"]["messages_truncated"])

    def test_unknown_terminal_and_job_fields_are_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data = fixture(Path(temporary))
            before = exporter.capture_facts(directory)
            data["origin-wrap-result.json"]["observations"][0]["terminal"]["private_note"] = "DO_NOT_PUBLISH"
            data["cleanup.json"]["private_path"] = "C:/private/DO_NOT_PUBLISH"
            save(directory, data)
            self.assertEqual(exporter.capture_facts(directory), before)

    def test_private_text_and_string_caret_are_rejected_after_evidence_synchronization(self):
        for field in ("lines", "caret"):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                directory, data = fixture(Path(temporary))
                terminal = data["origin-wrap-result.json"]["observations"][0]["terminal"]
                if field == "lines":
                    terminal["lines"] = ["DO_NOT_PUBLISH_PRIVATE_TEXT"]
                else:
                    terminal["caret_api"]["caretLine"] = "C:/private/DO_NOT_PUBLISH"
                save(directory, data)
                with self.assertRaises(ValueError):
                    exporter.capture_facts(directory)

    def test_wrong_source_missing_stage_extra_reply_and_cleanup_are_refused(self):
        for case in ("source", "missing-stage", "extra-reply", "cleanup", "truncated", "script-error"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                directory, data = fixture(Path(temporary))
                normalized = data["evidence.json"]["data"]["evidence"]["normalized_result"]
                if case == "source":
                    data["run.json"]["sha256"] = "0"*64
                elif case == "missing-stage":
                    data["origin-wrap-result.json"]["observations"].pop()
                elif case == "extra-reply":
                    data["peer.json"]["received_hex"] += "1b5b313b3152"
                elif case == "cleanup":
                    data["cleanup.json"]["active"] = 1
                elif case == "truncated":
                    normalized["messages_truncated"] = True
                save(directory, data)
                if case == "script-error":
                    normalized["messages"][1]["type"] = "error"
                    save(directory, data, synchronize=False)
                with self.assertRaises(ValueError):
                    exporter.capture_facts(directory)

    def test_public_cli_refuses_private_text_in_normal_and_optimized_python_without_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data = fixture(Path(temporary))
            self.check_private_cli_refusals(directory, data, Path(temporary))

    def test_existing_parent_alias_private_text_reaches_public_cli_whitelist(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            alias_component = root / "existing path spelling component"
            alias_component.mkdir()
            alias = alias_component / ".."
            self.assertNotEqual(alias, alias.resolve())
            directory, data = fixture(alias)
            self.check_private_cli_refusals(directory, data, root)

    def check_private_cli_refusals(self, directory, data, root):
        data["origin-wrap-result.json"]["observations"][0]["terminal"]["lines"] = ["DO_NOT_PUBLISH"]
        save(directory, data)
        plan = root / "plan.json"
        plan.write_text("{}")
        for optimized in (False, True):
            with self.subTest(optimized=optimized):
                destination = root / ("optimized/out.json" if optimized else "normal/out.json")
                command = [sys.executable, "-B", *(["-O"] if optimized else []), "-X", "utf8", "-m",
                           "research.ui.export_origin_wrap_reference", "--directory", str(directory), "--plan", str(plan),
                           "--destination", str(destination)]
                result = subprocess.run(command, cwd=exporter.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Only fixed synthetic document text", result.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
