import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from experiments import build_paths


class BinaryPathTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.filename = "replay.exe" if os.name == "nt" else "replay"

    def create(self, folder):
        path = self.root / folder / self.filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
        return path.resolve()

    def test_explicit_directory_overrides_environment(self):
        expected = self.create("chosen")
        with patch.dict(os.environ, {"TERMFORGE_BIN_DIR": str(self.root / "other")}):
            self.assertEqual(build_paths.binary("replay", expected.parent), expected)

    def test_environment_directory_used(self):
        expected = self.create("chosen")
        with patch.dict(os.environ, {"TERMFORGE_BIN_DIR": str(expected.parent)}):
            self.assertEqual(build_paths.binary("replay"), expected)

    def test_missing_override_does_not_use_old_binary(self):
        self.create("build/bin/Release")
        with patch.object(build_paths, "ROOT", self.root), self.assertRaises(FileNotFoundError):
            build_paths.binary("replay", self.root / "missing")

    def test_default_single_config_directory(self):
        expected = self.create("build/bin")
        with patch.object(build_paths, "ROOT", self.root), patch.dict(os.environ, {}, clear=True):
            self.assertEqual(build_paths.binary("replay"), expected)


if __name__ == "__main__":
    unittest.main()
