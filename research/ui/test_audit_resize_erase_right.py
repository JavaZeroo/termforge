"""Independent supplemental 85-byte-body EL0 guards using inert recordings and package files."""
from contextlib import contextmanager
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import audit_resize_erase_right as audit
from research.ui import resize_erase_reference as probe
from research.ui.test_export_resize_erase_reference import complete_fixture, fixture, marker_rows, save


def capture_fixture(root, case=audit.CASE):
    directory, data, plan = fixture(root, case)
    # Negative capture/CLI cases stop before provenance. Keep its required raw
    # inventory present; the complete export test supplies a real inert manifest.
    (directory / "files.json").write_text("{}")
    return directory, data, plan


def erased_suffix(directory, data):
    case = data[probe.RESULT_FILE]["case_id"]
    old = "L" + " " * 60 if case == audit.CASE else " " * 61
    added = "L" + " " * 59 if case == audit.CASE else " " * 60
    for observed in data[probe.RESULT_FILE]["observations"][3:]:
        terminal = observed["terminal"]
        terminal["lines"] = [added if line == old else line for line in terminal["lines"]]
        terminal["markers"] = marker_rows(terminal["lines"])
    save(directory, data)
    (directory / "mechanics-check.json").unlink()


@contextmanager
def preserved_fixture(root, case=audit.CASE):
    with complete_fixture(root, case) as values:
        directory, data, *_ = values
        erased_suffix(directory, data)
        yield values


class RightEraseSupplementTests(unittest.TestCase):
    def test_exact_candidate_is_new_only_after_EL0_and_old_audit_still_rejects_it(self):
        added = "L" + " " * 59
        for phase in range(5):
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
            self.assertEqual(len(result["capture"]["phases"]), 5)
            self.assertEqual(set(result["provenance"]["raw_files"]), set(audit.RAW_NAMES))
            self.assertNotIn("mechanics-check.json", result["provenance"]["raw_files"])
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()})
            self.assertNotIn(str(directory), destination.read_text())
            for phase in result["capture"]["phases"]: self.assertNotIn("lines", phase["document_projection"])
        self.assertIs(probe.validate_document, original_document)
        self.assertIs(probe.audit_mechanics, original_mechanics)
        self.assertEqual(frozen, {name: audit.sha(probe.ROOT / name) for name in probe.SOURCE_FILES})

    def test_EL2_has_its_own_single_candidate_and_cannot_use_EL0_addition(self):
        for case, added, other in ((audit.CASE, "L" + " " * 59, " " * 60),
                                   ("erase-whole-line", " " * 60, "L" + " " * 59)):
            for phase in range(5):
                with self.subTest(case=case, phase=phase):
                    expected = {added} if phase >= 3 else set()
                    self.assertEqual(audit.synthetic_lines(case, phase) - probe.synthetic_lines(case, phase), expected)
                    with self.assertRaises(ValueError): probe.validate_document(case, phase, {"line_count": 1, "lines": [added]})
                    with self.assertRaises(ValueError): audit.validate_document(case, phase, {"line_count": 1, "lines": [other]})
                    if phase >= 3: audit.validate_document(case, phase, {"line_count": 1, "lines": [added]})

    def test_EL2_complete_export_keeps_its_own_plan_input_and_original_failure(self):
        with tempfile.TemporaryDirectory() as temporary, preserved_fixture(Path(temporary).resolve(), "erase-whole-line") as values:
            directory, _, _, plan_path, source_root, official = values
            before = {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()}
            result = audit.export(directory, plan_path, Path(temporary) / "public/result.json", audit.sha(plan_path), source_root, official)
            self.assertEqual(result["case_id"], "erase-whole-line")
            self.assertEqual(result["original_audit"]["message"], audit.ORIGINAL_ERROR)
            self.assertTrue(result["failure_preserved"])
            self.assertEqual(result["offline_supplemental_audit"]["additional_candidate"], {"prefix": "", "trailing_spaces": 60, "length": 60})
            self.assertEqual(result["offline_supplemental_audit"]["additional_original_runs"], 0)
            self.assertEqual(result["capture"]["declared_actions_hex"], ["1b5b324b"])
            self.assertEqual(result["capture"]["initial_body_derivation"]["derived_source_bytes"], 85)
            self.assertEqual(len(result["capture"]["phases"]), 5)
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()})
            self.assertNotIn("mechanics-check.json", result["provenance"]["raw_files"])

    def test_EL2_cannot_swap_action_or_suppress_late_failure(self):
        for changed in ("action", "private-text", "preparation", "late-geometry"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = capture_fixture(Path(temporary).resolve(), "erase-whole-line")
                erased_suffix(directory, data)
                result = data[probe.RESULT_FILE]
                if changed == "action": data["peer.json"]["records"][3]["source_hex"] = "1b5b304b"
                elif changed == "private-text": result["observations"][4]["terminal"]["lines"][1] = "PRIVATE"
                elif changed == "preparation": result["observations"][2]["terminal"]["lines"][7] = "L"
                elif changed == "late-geometry": result["observations"][4]["window_rect"][2] += 1
                save(directory, data)
                with self.assertRaises(ValueError): audit.capture_facts(directory, plan)

    def test_unknown_text_overflow_and_repeated_source_markers_fail(self):
        for lines in (["PRIVATE"], ["L" + " " * 58], ["L" + " " * 92], ["Q"],
                      ["L" + " " * 59, "L" + " " * 59], ["ANCHOR", "ANCHOR"], ["PO"]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                audit.validate_document(audit.CASE, 3, {"line_count": len(lines), "lines": lines})
        with self.assertRaises(ValueError): audit.synthetic_lines("hidden-primary", 3)

    def test_plan_hash_is_checked_before_reading_or_auditing_recording(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); plan = root / "plan.json"; plan.write_text("{}")
            with patch.object(audit, "capture_facts", side_effect=AssertionError("Must not inspect recording")):
                with self.assertRaisesRegex(ValueError, "Authorized plan bytes differ"):
                    audit.export(root / "missing", plan, root / "public/result.json", "0" * 64)
            self.assertFalse((root / "public").exists())

    def test_new_85_byte_body_and_EL0_case_cannot_be_replaced_by_old_workload(self):
        for changed in ("length", "body", "insertion", "case"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                plan = {**probe.fixed_contract(audit.CASE), "execution_authorized": True}
                body = plan["initial_body_derivation"]
                self.assertEqual(body["derived_source_bytes"], 85)
                self.assertEqual(body["derived_source_sha256"], "a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3")
                if changed == "length": body["derived_source_bytes"] = 78
                elif changed == "body": body["derived_source_hex"] = probe.ORIGINAL_BODY.hex()
                elif changed == "insertion": body["inserted_source_hex"] = ""
                elif changed == "case": plan["case_id"] = "erase-whole-line"
                plan_path = root / "plan.json"; plan_path.write_text(json.dumps(plan))
                destination = root / "public/result.json"
                with self.assertRaisesRegex(ValueError, "Fixed 85-byte-body erase contract differs"):
                    audit.export(root / "missing", plan_path, destination, audit.sha(plan_path))
                self.assertFalse(destination.parent.exists())

    def test_other_mechanical_errors_before_and_after_old_text_failure_are_not_swallowed(self):
        for changed in ("early-naws", "late-geometry", "late-ack", "extra-reply", "private-text", "duplicate-marker",
                        "provider", "provider-observations", "script", "partial", "mechanics-present", "old-audit-passes", "preparation"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); directory, data, plan = capture_fixture(root)
                original = copy.deepcopy(data)
                erased_suffix(directory, data)
                result, peer = data[probe.RESULT_FILE], data["peer.json"]
                if changed == "early-naws": peer["frozen_grids"][0] = [92, 20]
                elif changed == "late-geometry": result["observations"][4]["window_rect"][2] += 1
                elif changed == "late-ack": (directory / "ack-4").write_text("missing")
                elif changed == "extra-reply": peer["received_hex"] += "1b5b313b3152"
                elif changed == "private-text": result["observations"][4]["terminal"]["lines"][1] = "PRIVATE"
                elif changed == "duplicate-marker": result["observations"][3]["terminal"]["markers"].append(copy.deepcopy(result["observations"][3]["terminal"]["markers"][0]))
                elif changed == "preparation":
                    terminal = result["observations"][2]["terminal"]
                    terminal["lines"][7] = "L"
                    terminal["markers"] = marker_rows(terminal["lines"])
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
                elif changed == "source": (source_root / "research/ui/resize_erase_reference.py").write_bytes(b"changed")
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
                erased_suffix(directory, data); (directory / "cleanup.json").write_text(content)
                with self.assertRaises(ValueError): audit.capture_facts(directory, plan)

    def test_cli_rejects_private_text_normally_and_with_optimization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); directory, data, plan = capture_fixture(root)
            erased_suffix(directory, data)
            data[probe.RESULT_FILE]["observations"][3]["terminal"]["lines"][1] = "PRIVATE"
            save(directory, data); plan_path = root / "plan.json"; plan_path.write_text(json.dumps(plan))
            for optimize in (False, True):
                destination = root / ("optimized/result.json" if optimize else "normal/result.json")
                command = [sys.executable, "-B", *(["-O"] if optimize else []), "-X", "utf8", "-m",
                           "research.ui.audit_resize_erase_right", "--directory", str(directory), "--plan", str(plan_path),
                           "--expected-plan-sha256", audit.sha(plan_path), "--destination", str(destination)]
                result = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(audit.ORIGINAL_ERROR, result.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
