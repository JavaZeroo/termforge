"""Public exporter CLI regressions using only a generated synthetic fixture."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.alternate_reference import phases

EXPORTER = ROOT / "research/ui/export_alternate_reference.py"


def fixture(directory: Path):
    """Create value-consistent test records, without launching any application."""
    directory.mkdir()
    script = b"// Synthetic exporter test fixture; not original runtime evidence.\n"
    (directory / "probe.js").write_bytes(script)
    (directory / "stderr.txt").write_bytes(b"")
    records, observations = [], []
    for index, (label, source) in enumerate(phases(25)):
        records.append({"phase": index, "label": label, "source_hex": source.hex(),
                        "source_sha256": hashlib.sha256(source).hexdigest(), "cursor_reply": [1, 1]})
        observations.append({"phase": index, "label": label, "cursor_reply": [1, 1],
                             "terminal": {"protocol": "Telnet", "connected": True, "capacity": 25,
                                          "line_count": 1, "lines": ["MAIN_A"]}})
        (directory / f"ack-{index}").write_text("observed", encoding="utf-8")
    result = {"status": "observed-all-phases", "error": None, "observations": observations}
    normalized = {"source_sha256": hashlib.sha256(script).hexdigest(),
                  "messages_truncated": False, "cleanup_error": None}
    data = {
        "run.json": {"sha256": "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0",
                     "files_sha256": "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203",
                     "script_sha256": hashlib.sha256(script).hexdigest()},
        "peer.json": {"complete": True, "columns": 91, "rows": 25, "records": records,
                      "received_hex": (b"\x1b[1;1R" * 26).hex()},
        "probe-result.json": result,
        "evidence.json": {"ok": True, "data": {"cleanup_error": None, "evidence": {
            "normalized_result": normalized, "evidence_id": "synthetic-export-test-only",
            "provider": {"name": "synthetic-test"}, "confidence": "test-fixture",
            "subject": None, "environment": None, "limitations": ["Not runtime evidence."]}}},
        "cleanup.json": {"active": 0, "total": 0, "terminated": 0},
        "profile-check.json": {"only_synthetic_session": True, "session_count": 1},
    }
    synchronize_messages(data)
    write_records(directory, data)
    return data


def synchronize_messages(data):
    result = data["probe-result.json"]
    messages = [{"type": "send", "payload": {"kind": "alternate_phase", **copy.deepcopy(observation)}}
                for observation in result["observations"]]
    messages.append({"type": "send", "payload": {"kind": "alternate_reference_result", **copy.deepcopy(result)}})
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = messages


def write_records(directory, data):
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


class ExportCliTests(unittest.TestCase):
    def run_cli(self, directory, destination, optimized):
        return subprocess.run([sys.executable, *(["-O"] if optimized else []), str(EXPORTER),
                               "--directory", str(directory), "--destination", str(destination)],
                              cwd=ROOT, capture_output=True, text=True, timeout=10)

    def check_rejected(self, change, reason=None):
        for optimized in (False, True):
            with self.subTest(optimized=optimized), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory, destination = root / "fixture", root / "public/validation.json"
                data = fixture(directory)
                change(data, directory)
                write_records(directory, data)
                completed = self.run_cli(directory, destination, optimized)
                self.assertNotEqual(completed.returncode, 0, completed.stdout)
                self.assertFalse(destination.exists(), "A rejected record produced public output")
                self.assertFalse(destination.parent.exists(), "A failed gate created the public directory")
                if reason:
                    self.assertIn(reason, completed.stderr)

    def test_valid_fixture_exports_in_normal_and_optimized_python(self):
        for optimized in (False, True):
            with self.subTest(optimized=optimized), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                fixture(root / "fixture")
                destination = root / "public/validation.json"
                completed = self.run_cli(root / "fixture", destination, optimized)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                exported = json.loads(destination.read_text(encoding="utf-8"))
                self.assertEqual(exported["dsr_reply_count"], 26)
                self.assertEqual(exported["phases"][0]["terminal_document_projection"]["lines"], ["MAIN_A"])
                self.assertEqual(exported["rea_capture_seconds"], 45)
                self.assertEqual(exported["cli_wait_timeout_seconds"], 60)
                self.assertNotIn("maximum_cli_seconds", exported)

    def test_private_or_nonfixture_text_is_rejected_even_when_records_agree(self):
        for marker in ("PRIVATE_REVIEW_MARKER", "PRIVATE_REVIEW_MARKER/host/path", "C:\\host\\private"):
            with self.subTest(marker=marker):
                def change(data, directory):
                    data["probe-result.json"]["observations"][0]["terminal"]["lines"][0] = marker
                    synchronize_messages(data)
                self.check_rejected(change, "Only synthetic fixture text is publishable")

    def test_wrong_original_identity_is_rejected(self):
        self.check_rejected(lambda data, directory: data["run.json"].update(sha256="0" * 64))

    def test_changed_probe_script_is_rejected(self):
        self.check_rejected(lambda data, directory: (directory / "probe.js").write_text("changed script"))

    def test_changed_source_is_rejected(self):
        self.check_rejected(lambda data, directory: data["peer.json"]["records"][0].update(source_hex="00"))

    def test_missing_stage_acknowledgment_is_rejected(self):
        self.check_rejected(lambda data, directory: (directory / "ack-0").write_text("not observed"))

    def test_inconsistent_observation_is_rejected(self):
        self.check_rejected(lambda data, directory: data["probe-result.json"]["observations"][0].update(cursor_reply=[2, 2]))

    def test_unreclaimed_job_is_rejected(self):
        self.check_rejected(lambda data, directory: data["cleanup.json"].update(active=1))


if __name__ == "__main__":
    unittest.main()
