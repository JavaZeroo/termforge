"""Independent supplemental hidden-primary guards using inert recordings and package files."""
from contextlib import contextmanager
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import audit_resize_frame_hidden as audit
from research.ui import resize_frame_reference as probe
from research.ui.test_export_resize_frame_reference import complete_fixture, fixture, marker_rows, save


def capture_fixture(root):
    directory, data, plan = fixture(root, audit.CASE)
    # Negative capture/CLI cases stop before provenance. Keep its required raw
    # inventory present; the complete export test supplies a real inert manifest.
    (directory / "files.json").write_text("{}")
    return directory, data, plan


def returned_fragment(directory, data):
    for observed in data[probe.RESULT_FILE]["observations"][3:]:
        terminal = observed["terminal"]
        terminal["lines"] = [" " * 61 + "x" if line == " " * 90 + "wx" else line for line in terminal["lines"]]
        terminal["markers"] = marker_rows(terminal["lines"])
    save(directory, data)
    (directory / "mechanics-check.json").unlink()


@contextmanager
def preserved_fixture(root):
    with complete_fixture(root, audit.CASE) as values:
        directory, data, *_ = values
        returned_fragment(directory, data)
        yield values


class HiddenSupplementTests(unittest.TestCase):
    def test_exact_candidate_is_new_only_from_return_and_old_audit_still_rejects_it(self):
        added = " " * 61 + "x"
        for phase in range(8):
            terminal = {"line_count": 1, "lines": [added]}
            with self.assertRaisesRegex(ValueError, audit.ORIGINAL_ERROR):
                probe.validate_document(audit.CASE, phase, terminal)
            if phase >= 3:
                audit.validate_document(audit.CASE, phase, terminal)
                self.assertEqual(audit.synthetic_lines(audit.CASE, phase) - probe.synthetic_lines(audit.CASE, phase), {added})
            else:
                with self.assertRaises(ValueError): audit.validate_document(audit.CASE, phase, terminal)

    def test_complete_export_preserves_failure_all_files_and_original_namespace(self):
        frozen = {name: audit.sha(probe.ROOT / name) for name in probe.SOURCE_FILES}
        original_document, original_mechanics = probe.validate_document, probe.audit_mechanics
        with tempfile.TemporaryDirectory() as temporary, preserved_fixture(Path(temporary).resolve()) as values:
            directory, _, _, plan_path, source_root, official = values
            before = {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()}
            destination = Path(temporary) / "public/result.json"
            result = audit.export(directory, plan_path, destination, audit.sha(plan_path), source_root, official)
            self.assertEqual(result["status"], "original-audit-failed-offline-supplement-passed")
            self.assertTrue(result["failure_preserved"])
            self.assertEqual(result["original_audit"]["message"], audit.ORIGINAL_ERROR)
            self.assertEqual(result["offline_supplemental_audit"]["additional_original_runs"], 0)
            self.assertEqual(len(result["capture"]["phases"]), 8)
            self.assertEqual(set(result["provenance"]["raw_files"]), set(audit.RAW_NAMES))
            self.assertNotIn("mechanics-check.json", result["provenance"]["raw_files"])
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()})
            self.assertNotIn(str(directory), destination.read_text())
            for phase in result["capture"]["phases"]: self.assertNotIn("lines", phase["document_projection"])
        self.assertIs(probe.validate_document, original_document)
        self.assertIs(probe.audit_mechanics, original_mechanics)
        self.assertEqual(frozen, {name: audit.sha(probe.ROOT / name) for name in probe.SOURCE_FILES})

    def test_unknown_text_overflow_and_repeated_source_markers_fail(self):
        for lines in (["PRIVATE"], [" " * 62 + "x"], [" " * 61 + "x "], ["Q"],
                      [" " * 61 + "x", " " * 61 + "x"], ["ANCHOR", "ANCHOR"], ["PO"]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                audit.validate_document(audit.CASE, 3, {"line_count": len(lines), "lines": lines})
        with self.assertRaises(ValueError): audit.synthetic_lines("alternate-active", 3)

    def test_plan_hash_is_checked_before_reading_or_auditing_recording(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); plan = root / "plan.json"; plan.write_text("{}")
            with patch.object(audit, "capture_facts", side_effect=AssertionError("Must not inspect recording")):
                with self.assertRaisesRegex(ValueError, "Authorized plan bytes differ"):
                    audit.export(root / "missing", plan, root / "public/result.json", "0" * 64)
            self.assertFalse((root / "public").exists())

    def test_other_mechanical_errors_before_and_after_old_text_failure_are_not_swallowed(self):
        for changed in ("early-naws", "late-geometry", "late-ack", "extra-reply", "private-text", "duplicate-marker",
                        "provider", "provider-observations", "script", "partial", "mechanics-present", "old-audit-passes"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); directory, data, plan = capture_fixture(root)
                original = copy.deepcopy(data)
                returned_fragment(directory, data)
                result, peer = data[probe.RESULT_FILE], data["peer.json"]
                if changed == "early-naws": peer["frozen_grids"][0] = [92, 20]
                elif changed == "late-geometry": result["observations"][7]["window_rect"][2] += 1
                elif changed == "late-ack": (directory / "ack-7").write_text("missing")
                elif changed == "extra-reply": peer["received_hex"] += "1b5b313b3152"
                elif changed == "private-text": result["observations"][7]["terminal"]["lines"][1] = "PRIVATE"
                elif changed == "duplicate-marker": result["observations"][3]["terminal"]["markers"].append(copy.deepcopy(result["observations"][3]["terminal"]["markers"][0]))
                elif changed == "provider": data["evidence.json"]["data"]["evidence"]["provider"]["id"] = "other"
                elif changed == "script": (directory / "probe.js").write_bytes(b"changed")
                elif changed == "partial": result["observations"].pop()
                elif changed == "mechanics-present": (directory / "mechanics-check.json").write_text("{}")
                elif changed == "old-audit-passes": data = original
                save(directory, data)
                if changed == "provider-observations":
                    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"][-1]["payload"]["error"] = "changed"
                    save(directory, data, synchronize=False)
                with self.assertRaises(ValueError): audit.capture_facts(directory, plan)

    def test_full_identity_profile_attempt_and_source_guards_still_apply(self):
        for changed in ("module", "source", "manifest", "profile", "attempt", "plan", "destination"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, preserved_fixture(Path(temporary).resolve()) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = audit.sha(plan_path); destination = Path(temporary) / "public/result.json"
                if changed == "module": (directory / "app/Qt5Core.dll").write_bytes(b"changed")
                elif changed == "source": (source_root / "research/ui/resize_frame_reference.py").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text("{}")
                elif changed == "profile": (directory / "profile/.wind/profiles/default.v10/terminal/user.sessions").write_text("[]")
                elif changed == "attempt": plan_path.with_suffix(".attempted").write_text("changed")
                elif changed == "plan": plan_path.write_bytes(plan_path.read_bytes() + b" ")
                elif changed == "destination": destination.parent.mkdir(); destination.write_text("preserved")
                with self.assertRaises(ValueError): audit.export(directory, plan_path, destination, expected, source_root, official)
                if changed == "destination": self.assertEqual(destination.read_text(), "preserved")
                else: self.assertFalse(destination.parent.exists())

    def test_duplicate_and_nonfinite_recordings_are_rejected(self):
        for content in ('{"active":0,"active":0}', '{"active":NaN}'):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = capture_fixture(Path(temporary).resolve())
                returned_fragment(directory, data); (directory / "cleanup.json").write_text(content)
                with self.assertRaises(ValueError): audit.capture_facts(directory, plan)

    def test_cli_rejects_private_text_normally_and_with_optimization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); directory, data, plan = capture_fixture(root)
            returned_fragment(directory, data)
            data[probe.RESULT_FILE]["observations"][3]["terminal"]["lines"][1] = "PRIVATE"
            save(directory, data); plan_path = root / "plan.json"; plan_path.write_text(json.dumps(plan))
            for optimize in (False, True):
                destination = root / ("optimized/result.json" if optimize else "normal/result.json")
                command = [sys.executable, "-B", *(["-O"] if optimize else []), "-X", "utf8", "-m",
                           "research.ui.audit_resize_frame_hidden", "--directory", str(directory), "--plan", str(plan_path),
                           "--expected-plan-sha256", audit.sha(plan_path), "--destination", str(destination)]
                result = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(audit.ORIGINAL_ERROR, result.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
