"""Synthetic publication guards for the separate reentry recording."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import export_mode_reentry_reference as exporter
from research.ui import mode_reentry_reference as probe
from research.ui.test_export_mode_state_reference import fixture as mode_fixture
from research.ui.test_export_mode_state_reference import complete_fixture
from research.ui import export_mode_state_reference as original_exporter
from research.ui import mode_state_reference as original_probe


def save(directory, data):
    result = data["mode-reentry-result.json"]
    payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "mode-reentry-v1", "case": probe.CASE_ID},
                {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                 "has_engine": True, "has_editor": False, "peer_ready": False}]
    payloads.extend({"kind": "mode_reentry_phase", "case": probe.CASE_ID, **copy.deepcopy(row)} for row in result["observations"])
    payloads.append({"kind": "mode_reentry_result", **copy.deepcopy(result)})
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
        {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def fixture(root):
    directory, data, _ = mode_fixture(root)
    plan = {**probe.fixed_contract(probe.CASE_ID), "execution_authorized": True,
            "out": str(directory), "source": str(root / "official")}
    data["mode-reentry-result.json"] = data.pop("mode-state-result.json")
    # Keep the old helper's original file separate; the new exporter must use its
    # own filename/schema, not accidentally accept this stale recording.
    result = data["mode-reentry-result.json"]
    result.update(case=probe.CASE_ID, status="observed-all-mode-reentry-phases")
    peer = data["peer.json"]
    peer["case"] = probe.CASE_ID
    for source, row, observation in zip(probe.phase_records(probe.CASE_ID), peer["records"], result["observations"]):
        row.update(source)
        observation.update(phase=source["phase"], label=source["label"])
    run = data["run.json"]
    template = (probe.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]+probe.javascript_source()
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(plan, run, probe.ROOT)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["source_sha256"] = run["script_sha256"]
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory, probe.CASE_ID)))
    return directory, data, plan


class ReentryExportTests(unittest.TestCase):
    def test_complete_reentry_archive_audit_and_export_source_identity(self):
        with tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as original:
            old_directory, _, old_plan, _, source_root, official = original
            with patch.object(probe, "EXE_SHA256", original_probe.EXE_SHA256), \
                 patch.object(probe, "MANIFEST_SHA256", original_probe.MANIFEST_SHA256), \
                 patch.dict(probe._RUNTIME, EXE_SHA256=original_probe.EXE_SHA256, MANIFEST_SHA256=original_probe.MANIFEST_SHA256), \
                 patch.dict(exporter._RUNTIME, FILE_COUNT=original_exporter.FILE_COUNT):
                root = Path(temporary) / "reentry"
                root.mkdir()
                directory, _, case_plan = fixture(root)
                plan = {**old_plan, **case_plan, "source": str(official)}
                (directory / "app").mkdir()
                for name in exporter.MODULES:
                    (directory / "app" / name).write_bytes((official / name).read_bytes())
                (directory / "files.json").write_bytes((old_directory / "files.json").read_bytes())
                profile = "profile/.wind/profiles/default.v10/terminal/user.sessions"
                (directory / profile).parent.mkdir(parents=True)
                (directory / profile).write_bytes((old_directory / profile).read_bytes())
                added = "research/ui/mode_reentry_reference.py"
                (source_root / added).write_bytes((probe.ROOT / added).read_bytes())
                archive = {"schema": probe.ARCHIVE_SCHEMA, "source_base_commit": "b"*40,
                           "source_files_sha256": {name: probe.sha(source_root / name) for name in probe.SOURCE_FILES}}
                (source_root / probe.ARCHIVE_MANIFEST).write_text(json.dumps(archive), encoding="utf-8")
                plan.update(probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES))
                plan["launcher_source_sha256"] = hashlib.sha256(probe.launcher_source(probe.CASE_ID).encode()).hexdigest()
                plan_path = root / "plan.json"
                plan_path.write_text(json.dumps(plan), encoding="utf-8")
                plan_path.with_suffix(".attempted").write_text("One original launch attempt; retain after failure and never retry this plan.\n")
                result = exporter.export(directory, plan_path, root / "public/result.json", exporter.sha(plan_path), source_root, official)
                self.assertEqual(result["schema"], "termforge-original-mode-reentry-reference-v1")
                self.assertEqual(len(result["provenance"]["execution_source_files_sha256"]), 9)
                self.assertEqual(set(result["offline_export_source_files_sha256"]),
                                 {"research/ui/export_mode_reentry_reference.py", "research/ui/export_mode_state_reference.py"})
                self.assertIsNone(result["origin_state_classification"])

    def test_valid_capture_uses_distinct_source_labels_and_markers(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, _, plan = fixture(Path(temporary))
            result = exporter.capture_facts(directory, plan)
            self.assertEqual(len(result["phases"]), 14)
            self.assertEqual(result["phases"][3]["label"], "duplicate-origin-sample")
            self.assertEqual(result["phases"][9]["label"], "later-origin-sample")
            self.assertNotIn(str(directory), json.dumps(result))

    def test_private_text_partial_and_old_case_are_rejected(self):
        for changed in ("private", "partial", "case", "old-label", "driver", "package"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary))
                result = data["mode-reentry-result.json"]
                if changed == "private": result["observations"][0]["terminal"]["lines"] = ["SECRET"]
                elif changed == "partial": result["observations"].pop()
                elif changed == "case": result["case"] = "main-on-alt-off"
                elif changed == "old-label": result["observations"][3]["label"] = "alternate-origin-sample"
                elif changed == "driver": plan["generated_driver_sha256"] = "0"*64
                elif changed == "package": data["run.json"]["sha256"] = "0"*64
                save(directory, data)
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_public_cli_rejects_private_text_normally_and_with_optimization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, data, plan = fixture(root)
            data["mode-reentry-result.json"]["observations"][0]["terminal"]["lines"] = ["SECRET"]
            save(directory, data)
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan))
            for optimize in (False, True):
                destination = root / ("optimized/out.json" if optimize else "normal/out.json")
                command = [sys.executable, "-B", *(["-O"] if optimize else []), "-X", "utf8", "-m",
                    "research.ui.export_mode_reentry_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed synthetic document text", completed.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
