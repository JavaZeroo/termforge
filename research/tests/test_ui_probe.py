"""Preparation guards; never execute a target or create a Windows desktop."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from research.ui import launch


class PreparationTests(unittest.TestCase):
    def test_wrong_binary_never_creates_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "WindTerm.exe").write_bytes(b"wrong executable")
            out = root / "new"
            with self.assertRaisesRegex(ValueError, "pinned official"):
                launch.prepare(root, out)
            self.assertFalse(out.exists())

    def test_subset_fresh_profile_and_output_guards(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "local" / "distribution"
            source.mkdir(parents=True)
            (source / "WindTerm.exe").write_bytes(b"synthetic fixture")
            (source / "qt.conf").write_text("synthetic")
            (source / "Qt5Widgets.dll").write_bytes(b"synthetic DLL")
            (source / "profiles.config").write_text("do not copy")
            (source / "private.dmp").write_bytes(b"do not copy")
            (source / "profiles").mkdir()
            (source / "profiles" / "secret").write_text("do not copy")
            for name in ("global", "plugins", "terminal"):
                (source / name).mkdir()
                (source / name / "fixture").write_text("synthetic")
            expected = hashlib.sha256(b"synthetic fixture").hexdigest()
            files = [source / "WindTerm.exe", source / "qt.conf", source / "Qt5Widgets.dll"]
            files += [source / name / "fixture" for name in ("global", "plugins", "terminal")]
            manifest_hash = launch.manifest_digest({p.relative_to(source).as_posix(): launch.digest(p) for p in files})
            out = root / "local" / "run"
            with patch.object(launch, "ROOT", root), patch.object(launch, "EXPECTED", expected), \
                    patch.object(launch, "EXPECTED_FILES", manifest_hash):
                with self.assertRaisesRegex(ValueError, "under local"):
                    launch.prepare(source, root / "outside")
                with self.assertRaisesRegex(ValueError, "overlap"):
                    launch.prepare(source, source / "nested")
                app = launch.prepare(source, out)
                self.assertFalse((app / "profiles").exists())
                self.assertFalse((app / "private.dmp").exists())
                self.assertEqual(json.loads((app / "profiles.config").read_text()),
                                 {"path": str(out.resolve() / "profile").replace("\\", "/")})
                self.assertEqual(list((out / "profile").iterdir()), [])
                manifest = json.loads((out / "files.json").read_text())
                self.assertEqual(manifest["WindTerm.exe"], expected)
                self.assertIn("global/fixture", manifest)
                with self.assertRaises(FileExistsError):
                    launch.prepare(source, out)
                (source / "Qt5Widgets.dll").write_bytes(b"changed DLL")
                with self.assertRaisesRegex(ValueError, "manifest differs"):
                    launch.prepare(source, root / "local" / "bad-dll")
                (source / "Qt5Widgets.dll").write_bytes(b"synthetic DLL")
                (source / "global" / "fixture").write_text("changed theme")
                with self.assertRaisesRegex(ValueError, "manifest differs"):
                    launch.prepare(source, root / "local" / "bad-theme")


if __name__ == "__main__":
    unittest.main()
