"""Dependency preparation CLI contracts; no GUI or personal desktop access."""
from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import prepare_qt_build


class PreparationCommandTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.out = self.root / "dependencies"
        self.summary = self.root / "summary.json"
        self.arguments = ["--cache", str(self.cache), "--out", str(self.out),
                          "--summary", str(self.summary)]

    def run_command(self, *extra):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            code = prepare_qt_build.main([*self.arguments, *extra])
        result = json.loads(stdout.getvalue())
        self.assertEqual(result, json.loads(self.summary.read_text(encoding="utf-8")))
        self.assertNotIn(str(self.root), stdout.getvalue())
        return code, result

    def test_offline_missing_archive_fails_without_creating_sdk(self):
        code, result = self.run_command("--offline")
        self.assertEqual(code, 1)
        self.assertEqual(result["error"], "archive_missing")
        self.assertFalse(self.out.exists())

    def test_corrupt_cache_is_rejected_and_never_replaced(self):
        archive_name = (prepare_qt_build.ROOT / "third_party/qt/manifest.json")
        archive_name = json.loads(archive_name.read_text(encoding="utf-8"))["official_binary_archive"]["archive"]
        archive = self.cache / archive_name
        archive.write_bytes(b"corrupt cached bytes")
        with patch("urllib.request.urlopen", side_effect=AssertionError("network must not be called")):
            code, result = self.run_command()
        self.assertEqual(code, 1)
        self.assertEqual(result["error"], "archive_identity")
        self.assertEqual(archive.read_bytes(), b"corrupt cached bytes")
        self.assertFalse(self.out.exists())

    def test_download_with_wrong_content_is_not_cached(self):
        with patch("urllib.request.urlopen", return_value=io.BytesIO(b"wrong response")):
            code, result = self.run_command()
        self.assertEqual(code, 1)
        self.assertEqual(result["error"], "archive_identity")
        self.assertEqual(list(self.cache.iterdir()), [])
        self.assertFalse(self.out.exists())

    def test_network_failure_does_not_publish_a_partial_archive(self):
        with patch("urllib.request.urlopen", side_effect=OSError("private network detail")):
            code, result = self.run_command()
        self.assertEqual(code, 1)
        self.assertEqual(result["error"], "download_failed")
        self.assertNotIn("private", json.dumps(result))
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_existing_output_is_not_overlaid_or_deleted(self):
        self.out.mkdir()
        marker = self.out / "keep.txt"
        marker.write_text("existing files", encoding="utf-8")
        code, result = self.run_command("--offline")
        self.assertEqual(code, 1)
        self.assertEqual(result["error"], "output_exists")
        self.assertEqual(marker.read_text(encoding="utf-8"), "existing files")


if __name__ == "__main__":
    unittest.main()
