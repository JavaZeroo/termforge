"""Owned, actual API-family/lifetime checks; no desktop or installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.conpty.run import OwnedJob


@unittest.skipUnless(os.name == "nt", "Windows ConPTY backend")
class BackendTests(unittest.TestCase):
    def setUp(self):
        base = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "backend"
        base.mkdir(parents=True, exist_ok=True)
        self.out = Path(tempfile.mkdtemp(prefix="run-", dir=base))
        self.serial = 0

    def invoke(self, directory, mode="--host"):
        job, process = OwnedJob(), None
        result = {"returncode": None, "outer_job_after": None, "forced_cleanup": False}
        case = self.out / f"case-{self.serial}"
        self.serial += 1
        private = case / "_private"
        private.mkdir(parents=True)
        try:
            process = subprocess.Popen([str(binary("conpty_backend_probe")), mode, str(directory)],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            job.assign(process.pid)
            stdout, stderr = process.communicate(b"GO\n", timeout=20)
            (private / "host.stdout.bin").write_bytes(stdout)
            (private / "host.stderr.bin").write_bytes(stderr)
            result["returncode"] = process.returncode
            result["native"] = json.loads(stdout) if stdout else None
            result["error"] = "identity_rejected" if stderr else None
        finally:
            if process is not None and process.poll() is None:
                result["forced_cleanup"] = True
                job.terminate()
                process.communicate(timeout=5)
            try:
                result["outer_job_after"] = job.drain()
                if result["outer_job_after"]["active"]:
                    result["forced_cleanup"] = True
                    job.terminate()
                    result["outer_job_after"] = job.drain()
            finally:
                job.close()
                (case / "summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        self.assertFalse(result["forced_cleanup"])
        self.assertEqual(result["outer_job_after"]["active"], 0)
        return result

    def check_lifecycle(self, sample, side_by_side):
        self.assertEqual(sample["returncode"], 0)
        self.assertEqual(sample["native"], {
            "valid": True, "side_by_side": side_by_side, "creation_flags": 24 if side_by_side else 0,
            "loaded_during_session": side_by_side, "loaded_after_close": side_by_side,
            "module_unloaded_after_session": True, "exit_code": 37,
            "active_children_after": 0, "read_error": 109, "cleanup_complete": True,
            "forced_reader_cancel": False, "forced_process_cancel": False,
        })

    def selected_directory(self):
        if os.environ.get("TERMFORGE_EXPECT_CONPTY_BACKEND") != "reviewed-side-by-side-1.25.260930003":
            self.skipTest("explicit reviewed backend build not selected")
        return binary("conpty_backend_probe").parent / "conpty-backend"

    def test_system_create_resize_and_close(self):
        self.check_lifecycle(self.invoke("system"), False)

    def test_selected_module_survives_session_and_unloads_after_close(self):
        self.check_lifecycle(self.invoke(self.selected_directory()), True)

    def test_relative_directory_is_rejected(self):
        result = self.invoke("conpty-backend", "--identity")
        self.assertEqual(result["returncode"], 2)
        self.assertEqual(result["error"], "identity_rejected")

    def test_missing_pair_is_rejected(self):
        result = self.invoke(self.out, "--identity")
        self.assertEqual(result["returncode"], 2)

    def test_unreviewed_pair_is_rejected(self):
        (self.out / "conpty.dll").write_bytes(b"unreviewed dll")
        (self.out / "OpenConsole.exe").write_bytes(b"unreviewed host")
        result = self.invoke(self.out, "--identity")
        self.assertEqual(result["returncode"], 2)

    def test_either_selected_file_tamper_is_rejected(self):
        selected = self.selected_directory()
        for index, name in enumerate(("conpty.dll", "OpenConsole.exe")):
            directory = self.out / f"tamper-{index}"
            directory.mkdir()
            for member in ("conpty.dll", "OpenConsole.exe"):
                shutil.copyfile(selected / member, directory / member)
            payload = bytearray((directory / name).read_bytes())
            payload[-1] ^= 1
            (directory / name).write_bytes(payload)
            with self.subTest(file=name):
                result = self.invoke(directory, "--identity")
                self.assertEqual(result["returncode"], 2)


if __name__ == "__main__":
    unittest.main()
