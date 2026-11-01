"""Synthetic resize publication guards; never start WindTerm, REA or native UI."""
from __future__ import annotations
import copy
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import export_resize_state_reference as exporter
from research.ui import resize_state_reference as probe
from research.ui import mode_state_reference as original_probe
from research.ui import export_mode_state_reference as original_exporter
from research.ui.test_resize_state_reference import receive
from research.ui.test_export_mode_state_reference import fixture as mode_fixture, complete_fixture as mode_complete_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data["resize-state-result.json"]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "resize-state-v1"},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "resize_state_phase", **copy.deepcopy(row)} for row in result["observations"])
        payloads.append({"kind": "resize_state_result", **copy.deepcopy(result)})
        data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
            {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def fixture(root, logical_line=False):
    directory, data, _ = mode_fixture(root)
    data.pop("mode-state-result.json")
    state, _ = receive("batched-resize")
    data["peer.json"] = state
    result = {"status": "observed-three-resize-states", "error": None, "observations": [],
              "window_rect_format": "qt-qrect-inclusive-ltrb", "geometry_checks": [],
              "full_active_grid_verified": False, "local_shell_transport_verified": False, "presented_done": None}
    for record, window in zip(state["records"], probe.WINDOWS):
        lines = ["", "0123456789", "ANCHOR", "", " "*90+"v"]
        lines.extend([" "*90+"wx"] if logical_line else [" "*90+"w", "x"])
        markers, offset = [], 0
        for row, line in enumerate(lines):
            for label in exporter.MARKERS:
                column = line.find(label)
                while column >= 0:
                    markers.append({"label": label, "document_line": row, "column": column, "position": offset+column,
                                    "rect": {"x": column*8, "y": row*18, "width": 8, "height": 18}})
                    column = line.find(label, column+1)
            offset += len(line)+1
        terminal = {"connected": True, "protocol": "Telnet", "capacity": 20, "line_count": len(lines),
                    "lines": lines, "markers": markers, "top": None, "scroll": 0,
                    "caret_api": {"caretLine": 10, "caretColumn": 1, "caretPosition": offset}}
        rect = [-113, -27, window[0]-114, window[1]-28]
        observed = {key: record[key] for key in ("phase", "label", "frozen_grid", "cursor_reply", "cursor_reply_private", "response_hex")}
        result["observations"].append({**observed, "terminal": terminal, "window_rect": rect, "window_size": list(window)})
        result["geometry_checks"].append({"phase": record["phase"], "window_rect": rect,
                                          "window_size": list(window), "expected_size": list(window)})
    data["resize-state-result.json"] = result
    plan = {**probe.fixed_contract(), "execution_authorized": True, "out": str(directory), "source": str(root / "official")}
    run = data["run.json"]
    run.update(sha256=probe.EXE_SHA256, files_sha256=probe.MANIFEST_SHA256)
    template = (probe.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += (probe.ROOT / "research/ui/resize_state_reference.js").read_text(encoding="utf-8")
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(plan, run, probe.ROOT)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["source_sha256"] = run["script_sha256"]
    (directory / "fixture-ready").write_text(json.dumps({"grid": state["frozen_grids"][0], "event_count": state["freeze_event_counts"][0]}))
    for index in (1, 2):
        (directory / f"resize-request-{index}").write_text(json.dumps({"phase": index, "window": list(probe.WINDOWS[index])}))
        (directory / f"resize-ready-{index}").write_text(json.dumps({"phase": index,
            "grid": state["frozen_grids"][index], "event_count": state["freeze_event_counts"][index]}))
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory)))
    return directory, data, plan


@contextmanager
def complete_fixture(root):
    with mode_complete_fixture(root) as original:
        old_directory, _, old_plan, _, source_root, official = original
        with patch.object(probe, "EXE_SHA256", original_probe.EXE_SHA256), \
             patch.object(probe, "MANIFEST_SHA256", original_probe.MANIFEST_SHA256), \
             patch.dict(exporter._PROVENANCE, FILE_COUNT=original_exporter.FILE_COUNT):
            new_root = root / "resize"
            new_root.mkdir()
            directory, data, case_plan = fixture(new_root)
            plan = {**old_plan, **case_plan, "source": str(official)}
            plan.pop("case_id")
            (directory / "app").mkdir()
            for name in original_exporter.MODULES:
                (directory / "app" / name).write_bytes((official / name).read_bytes())
            (directory / "files.json").write_bytes((old_directory / "files.json").read_bytes())
            profile = "profile/.wind/profiles/default.v10/terminal/user.sessions"
            (directory / profile).parent.mkdir(parents=True)
            (directory / profile).write_bytes((old_directory / profile).read_bytes())
            for name in probe.SOURCE_FILES:
                (source_root / name).parent.mkdir(parents=True, exist_ok=True)
                (source_root / name).write_bytes((probe.ROOT / name).read_bytes())
            archive = {"schema": probe.common.ARCHIVE_SCHEMA, "source_base_commit": "b"*40,
                       "source_files_sha256": {name: probe.sha(source_root / name) for name in probe.SOURCE_FILES}}
            (source_root / probe.common.ARCHIVE_MANIFEST).write_text(json.dumps(archive), encoding="utf-8")
            plan.update(probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES))
            plan["launcher_source_sha256"] = hashlib.sha256(probe.launcher_source().encode()).hexdigest()
            plan_path = new_root / "plan.json"
            plan_path.write_text(json.dumps(plan), encoding="utf-8")
            plan_path.with_suffix(".attempted").write_text("One original resize launch attempt; preserve failures and never retry this plan.\n")
            yield directory, data, plan, plan_path, source_root, official


class ResizeExportTests(unittest.TestCase):
    def test_retained_soft_wrap_line_is_exactly_initial_columns_plus_one(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary), logical_line=True)
            result = exporter.capture_facts(directory, plan)
            for raw, phase in zip(data["resize-state-result.json"]["observations"], result["phases"]):
                self.assertEqual(len(raw["terminal"]["lines"][-1]), 92)
                edge = [marker for marker in phase["document_projection"]["markers"] if marker["label"] in ("w", "x")]
                self.assertEqual([(marker["label"], marker["column"]) for marker in edge], [("w", 90), ("x", 91)])
                self.assertNotIn("lines", phase["document_projection"])

    def test_only_the_exact_source_derived_logical_line_extension_is_allowed(self):
        invalid = [" "*91+"wx", " "*90+"wxx", " "*90+"wx ", " "*89+"wx", " "*90+"vx",
                   " "*90+"PRIVATE", " "*90+"wxSECRET", "0"*92, "0123456789 ", " ANCHOR"]
        for line in invalid:
            with self.subTest(line=line), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary), logical_line=True)
                data["resize-state-result.json"]["observations"][0]["terminal"]["lines"][-1] = line
                save(directory, data)
                with self.assertRaisesRegex(ValueError, "Only fixed synthetic document text"):
                    exporter.capture_facts(directory, plan)

    def test_capture_exports_three_geometries_but_no_document_text(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, _, plan = fixture(Path(temporary))
            result = exporter.capture_facts(directory, plan)
            self.assertEqual(result["freeze_event_counts"], [1, 4, 5])
            self.assertEqual(len(result["phases"]), 3)
            self.assertEqual(result["phases"][1]["source_hex"], "")
            self.assertEqual(result["phases"][2]["source_hex"], "")
            for phase, window in zip(result["phases"], probe.WINDOWS):
                self.assertEqual(phase["window_size"], list(window))
                self.assertNotIn("lines", phase["document_projection"])
            self.assertNotIn(str(directory), json.dumps(result))

    def test_private_text_marker_and_geometry_payloads_are_rejected(self):
        for changed in ("text", "marker", "rect", "caret", "window", "format", "partial", "source", "ready", "naws"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary))
                result = data["resize-state-result.json"]
                terminal = result["observations"][0]["terminal"]
                if changed == "text": terminal["lines"][1] = "SECRET"
                elif changed == "marker": terminal["markers"][0]["label"] = "SECRET"
                elif changed == "rect": terminal["markers"][0]["rect"]["x"] = "C:/private"
                elif changed == "caret": terminal["caret_api"]["caretPosition"] = "private"
                elif changed == "window": result["observations"][0]["window_rect"][2] += 1
                elif changed == "format": result["window_rect_format"] = "xywh"
                elif changed == "partial": result["observations"].clear()
                elif changed == "source": data["peer.json"]["records"][1]["source_hex"] = "1b5b48"
                elif changed == "ready": (directory / "resize-ready-1").write_text('{}')
                elif changed == "naws": data["peer.json"]["naws_history"].append([90, 20])
                save(directory, data)
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_wrong_identity_and_partial_provider_are_rejected(self):
        for changed in ("exe", "manifest", "template", "script", "evidence", "truncated", "cleanup", "source-path"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary))
                evidence = data["evidence.json"]["data"]["evidence"]
                if changed == "exe": data["run.json"]["sha256"] = "0"*64
                elif changed == "manifest": data["run.json"]["files_sha256"] = "0"*64
                elif changed == "template": data["run.json"]["template_sha256"] = "0"*64
                elif changed == "script": (directory / "probe.js").write_bytes(b"changed")
                elif changed == "evidence": evidence["evidence_id"] = "private"
                elif changed == "truncated": evidence["normalized_result"]["messages_truncated"] = True
                elif changed == "cleanup": data["cleanup.json"]["active"] = 1
                elif changed == "source-path": evidence["normalized_result"]["source_path"] = "C:/private/other.js"
                save(directory, data)
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_unlisted_metadata_is_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary))
            before = exporter.capture_facts(directory, plan)
            data["resize-state-result.json"]["observations"][0]["terminal"]["private_note"] = "SECRET"
            data["peer.json"]["resize_boundaries"][0]["private_path"] = "C:/private"
            save(directory, data)
            self.assertEqual(exporter.capture_facts(directory, plan), before)

    def test_complete_nine_source_archive_and_package_validation(self):
        with tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as values:
            directory, _, _, plan_path, source_root, official = values
            result = exporter.export(directory, plan_path, Path(temporary) / "public/result.json",
                                     exporter.sha(plan_path), source_root, official)
            self.assertEqual(result["schema"], "termforge-original-resize-state-reference-v1")
            self.assertEqual(len(result["provenance"]["execution_source_files_sha256"]), 9)
            self.assertEqual(set(result["offline_export_source_files_sha256"]), set(exporter.EXPORT_SOURCES))
            self.assertIsNone(result["resize_policy_classification"])
            self.assertFalse(result["semantic_acceptance"])

    def test_changed_package_source_and_attempt_are_rejected_by_full_export(self):
        for changed in ("module", "source", "manifest", "attempt", "plan"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary)) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = exporter.sha(plan_path)
                if changed == "module": (directory / "app/Qt5Core.dll").write_bytes(b"changed")
                elif changed == "source": (source_root / "research/ui/resize_state_reference.py").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text('{}')
                elif changed == "attempt": plan_path.with_suffix('.attempted').write_text('not resize')
                elif changed == "plan": plan_path.write_bytes(plan_path.read_bytes()+b' ')
                destination = Path(temporary) / "public/result.json"
                with self.assertRaises(ValueError): exporter.export(directory, plan_path, destination, expected, source_root, official)
                self.assertFalse(destination.parent.exists())

    def test_cli_refuses_private_text_normally_and_under_optimization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, data, plan = fixture(root)
            data["resize-state-result.json"]["observations"][0]["terminal"]["lines"][1] = "SECRET"
            save(directory, data)
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan))
            for optimize in (False, True):
                destination = root / ("optimized/out.json" if optimize else "normal/out.json")
                command = [sys.executable, "-B", *(["-O"] if optimize else []), "-X", "utf8", "-m",
                    "research.ui.export_resize_state_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed synthetic document text", completed.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
