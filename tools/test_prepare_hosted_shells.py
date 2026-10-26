"""Pure guard/command/hash/oracle tests; never run WSL or impersonate hosted CI."""
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import prepare_hosted_shells as prep


class HostedShellPreparationTests(unittest.TestCase):
    def test_exact_hosted_guard_rejects_other_hosts_and_repositories(self):
        # A plain mapping is only input to the pure guard. It is never exported
        # into os.environ or used to call a worker or a subprocess.
        facts = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted", "RUNNER_OS": "Windows",
                 "GITHUB_REPOSITORY": "JavaZeroo/termforge", "ImageOS": "win25",
                 "ImageVersion": "20261024.1.0", "GITHUB_RUN_ID": "123456"}
        prep.require_hosted(facts)
        for key, value in (("GITHUB_ACTIONS", "false"), ("RUNNER_ENVIRONMENT", "self-hosted"),
                           ("RUNNER_OS", "Linux"), ("GITHUB_REPOSITORY", "another/repo"),
                           ("ImageOS", "win22"), ("ImageVersion", "unknown"), ("GITHUB_RUN_ID", "")):
            with self.subTest(key=key), self.assertRaises(prep.PreparationError):
                prep.require_hosted({**facts, key: value})

    def test_path_guard_rejects_workspace_escape_and_requires_local_ownership(self):
        self.assertEqual(prep.require_paths(prep.ROOT, prep.ROOT / "local/test-pure"),
                         prep.ROOT / "local/test-pure")
        for workspace, out in ((prep.ROOT, prep.ROOT), (prep.ROOT, prep.ROOT / "local"),
                               (prep.ROOT, prep.ROOT / "docs/preparation"),
                               (prep.ROOT, prep.ROOT / "local/../../outside"),
                               (prep.ROOT.parent, prep.ROOT / "local/test-pure")):
            with self.subTest(out=out), self.assertRaises(prep.PreparationError):
                prep.require_paths(workspace, out)

    def test_fixed_product_launch_and_import_have_no_global_mutation_flags(self):
        cwd = r"D:\a\termforge\local\launch cwd"
        command = prep.wsl_command("wsl.exe", cwd)
        self.assertEqual(command, ["wsl.exe", "--cd", cwd, "-d", "Ubuntu", "-e", "sh", "-c",
                                   "export PS1='TF> '; export LANG=C.UTF-8; exec sh"])
        imported = prep.import_command("wsl.exe", "fresh-owned-root", "verified-archive")
        self.assertEqual(imported, ["wsl.exe", "--import", "Ubuntu", "fresh-owned-root", "verified-archive", "--version", "1"])
        self.assertTrue({"--install", "--set-default", "--set-default-version", "--set-version", "--unregister",
                         "--terminate", "--shutdown", "--update"}.isdisjoint(command + imported))
        script = prep.wsl_input(cwd)
        self.assertIn(b'test "$WSL_DISTRO_NAME" = Ubuntu || exit 71', script)
        self.assertIn(b'test -x /bin/sh || exit 72', script)
        self.assertIn(b'test "$PWD" = \'/mnt/d/a/termforge/local/launch cwd\' || exit 73', script)
        self.assertTrue(script.endswith(b"exit 37\n"))
        self.assertEqual(prep.linux_path(r"D:\quoted'path\probe"), "/mnt/d/quoted'path/probe")
        for invalid in ("relative/path", r"\\server\share\probe"):
            with self.assertRaises(prep.PreparationError):
                prep.linux_path(invalid)

    def test_distribution_list_and_marker_oracles_reject_missing_or_wrong_identity(self):
        for raw in (b"Ubuntu\r\n", "Debian\r\nUbuntu\r\n".encode("utf-16")):
            self.assertTrue(prep.ubuntu_present(raw))
        self.assertFalse(prep.ubuntu_present(b"Debian\r\n"))
        self.assertFalse(prep.ubuntu_present(b"Ubuntu-24.04\r\n"))
        with self.assertRaises(prep.PreparationError):
            prep.ubuntu_present(b"x" * 65537)
        prep.validate_marker(b"TERMFORGE_HOSTED_WSL_READY\n", 37, "TERMFORGE_HOSTED_WSL_READY", 37)
        for output, code in ((b"", 37), (b"echo TERMFORGE_HOSTED_WSL_READY\n", 37),
                             (b"TERMFORGE_HOSTED_WSL_READY\n", 0),
                             (b"TERMFORGE_HOSTED_WSL_READY\nTERMFORGE_HOSTED_WSL_READY\n", 37)):
            with self.subTest(output=output, code=code), self.assertRaises(prep.PreparationError):
                prep.validate_marker(output, code, "TERMFORGE_HOSTED_WSL_READY", 37)

    def test_stream_download_requires_exact_hash_size_and_deadline(self):
        data = b"fixed synthetic rootfs bytes"
        digest = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            prep.copy_verified_rootfs(io.BytesIO(data), destination / "good", expected_hash=digest)
            self.assertEqual((destination / "good").read_bytes(), data)
            for name, body, kwargs in (("hash", data, {}), ("empty", b"", {}),
                                       ("size", data, {"limit": len(data) - 1}),
                                       ("deadline", data, {"clock": iter((0, 0, 120)).__next__})):
                with self.subTest(name=name), self.assertRaises(prep.PreparationError):
                    prep.copy_verified_rootfs(io.BytesIO(body), destination / name, **kwargs)

    def test_existing_unusable_ubuntu_is_never_replaced(self):
        steps = []
        def fake_step(name, out):
            steps.append(name)
            if name == "preflight":
                return {"status": "passed", "result": "ubuntu-existing"}
            return {"status": "failed", "failure_code": "shell_startup"}
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(prep, "run_step", side_effect=fake_step):
            report = prep.prepare(Path(temporary))
        self.assertEqual(steps, ["preflight", "check-before"])
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["failure_code"], "shell_startup")

    def test_absent_ubuntu_requires_verified_download_before_import_and_both_checks(self):
        steps = []
        def fake_step(name, out):
            steps.append(name)
            return {"status": "passed", "result": {"preflight": "ubuntu-absent", "download": "rootfs-verified",
                    "import": "ubuntu-imported-wsl1", "check-before": "ubuntu-sh-cwd-verified",
                    "check-after": "ubuntu-sh-cwd-verified"}[name]}
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(prep, "run_step", side_effect=fake_step):
            report = prep.prepare(Path(temporary))
        self.assertEqual(steps, ["preflight", "download", "import", "check-before", "check-after"])
        self.assertEqual(report["status"], "passed")
        steps.clear()
        def bad_download(name, out):
            steps.append(name)
            return ({"status": "passed", "result": "ubuntu-absent"} if name == "preflight" else
                    {"status": "failed", "failure_code": "download_hash"})
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(prep, "run_step", side_effect=bad_download):
            report = prep.prepare(Path(temporary))
        self.assertEqual(steps, ["preflight", "download"])
        self.assertEqual(report["status"], "failed")

    def test_empty_list_error_does_not_replace_a_registered_distribution(self):
        self.assertEqual(prep.classify_registration(1, b"no registered distributions", False), "ubuntu-absent")
        self.assertEqual(prep.classify_registration(0, b"Ubuntu\r\n", True), "ubuntu-existing")
        for code, output, registered in ((1, b"Ubuntu\r\n", True), (0, b"Debian\r\n", True),
                                         (0, b"Ubuntu\r\n", False)):
            with self.subTest(registered=registered), self.assertRaises(prep.PreparationError):
                prep.classify_registration(code, output, registered)

    def test_a_successful_download_cannot_stand_in_for_import_or_shell_checks(self):
        steps = []
        def wrong_step(name, out):
            steps.append(name)
            return {"status": "passed", "result": "ubuntu-absent" if name == "preflight" else "rootfs-verified"}
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(prep, "run_step", side_effect=wrong_step):
            report = prep.prepare(Path(temporary))
        self.assertEqual(steps, ["preflight", "download", "import"])
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["failure_code"], "worker_step")

    def test_local_runtime_is_rejected_before_any_subprocess_or_download(self):
        with mock.patch.object(prep, "require_runtime", side_effect=prep.PreparationError("hosted_guard")), \
             mock.patch.object(prep.subprocess, "Popen") as popen, \
             mock.patch.object(prep.urllib.request, "urlopen") as urlopen, \
             mock.patch.object(prep.sys, "argv", ["prepare_hosted_shells", "--out", "local/unused"]):
            self.assertEqual(prep.main(), 2)
        popen.assert_not_called()
        urlopen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
