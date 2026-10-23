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
            (source / "winpty-agent.exe").write_bytes(b"synthetic helper")
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
            files = [source / "WindTerm.exe", source / "winpty-agent.exe", source / "qt.conf", source / "Qt5Widgets.dll"]
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
                sessions = json.loads((out / "profile/.wind/profiles/default.v10/terminal/user.sessions").read_text())
                self.assertEqual(len(sessions), 1)
                self.assertEqual(sessions[0]["session.protocol"], "Shell")
                self.assertEqual(sessions[0]["session.label"], "TF_UI_CMD")
                self.assertEqual(sessions[0]["process.arguments"], "/d /q")
                self.assertEqual(sessions[0]["process.workingDirectory"], (out / "host-home").as_posix())
                self.assertTrue(launch.check_profile_scope(out)["only_synthetic_session"])
                sessions.append({"session.protocol": "SSH", "session.label": "synthetic-unexpected"})
                session_file = out / "profile/.wind/profiles/default.v10/terminal/user.sessions"
                session_file.write_text(json.dumps(sessions))
                with self.assertRaisesRegex(RuntimeError, "unexpected session metadata"):
                    launch.check_profile_scope(out)
                self.assertNotIn("synthetic-unexpected", (out / "profile-check.json").read_text())
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

    def test_home_and_scale_are_isolated_without_mutating_host_environment(self):
        with tempfile.TemporaryDirectory() as folder, patch.dict(launch.os.environ, {
                "HOME": "host-home", "USERPROFILE": "host-profile", "APPDATA": "host-appdata",
                "LOCALAPPDATA": "host-local", "QT_FONT_DPI": "240", "QT_SCALE_FACTOR": "2"}):
            out = Path(folder)
            env = launch.isolated_environment(out, "1.5")
            self.assertEqual(env["HOME"], str(out / "host-home"))
            self.assertEqual(env["USERPROFILE"], env["HOME"])
            self.assertTrue(Path(env["APPDATA"]).is_relative_to(out))
            self.assertTrue(Path(env["LOCALAPPDATA"]).is_relative_to(out))
            self.assertEqual(env["QT_SCALE_FACTOR"], "1.5")
            self.assertNotIn("QT_FONT_DPI", env)
            self.assertEqual(launch.os.environ["HOME"], "host-home")
            self.assertEqual(list((out / "host-home/.ssh").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
