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
    def test_public_provenance_is_guarded_and_contains_only_safe_runner_identity(self):
        facts = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted", "RUNNER_OS": "Windows",
                 "GITHUB_REPOSITORY": "JavaZeroo/termforge", "ImageOS": "win25",
                 "ImageVersion": "20261024.1.0", "GITHUB_RUN_ID": "123456",
                 "PRIVATE_TEST_TOKEN": "synthetic-never-publish", "PRIVATE_TEST_PATH": "synthetic-never-publish"}
        self.assertEqual(prep.public_hosted_provenance(facts),
                         {"image_os": "win25", "image_version": "20261024.1.0", "github_run_id": "123456"})
        with self.assertRaises(prep.PreparationError):
            prep.public_hosted_provenance({**facts, "RUNNER_ENVIRONMENT": "self-hosted"})

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

    def test_worker_job_guard_precedes_host_paths_or_commands(self):
        fake_stdin = mock.Mock(buffer=io.BytesIO(b"GO\n"))
        with mock.patch.object(prep.sys, "stdin", fake_stdin), \
             mock.patch.object(prep, "require_worker_job", side_effect=prep.PreparationError("worker_job_guard")), \
             mock.patch.object(prep, "require_runtime") as runtime, \
             mock.patch.object(prep.subprocess, "run") as command, \
             mock.patch.object(prep.urllib.request, "urlopen") as download:
            with self.assertRaisesRegex(prep.PreparationError, "worker_job_guard"):
                prep.worker("import", Path("local/not-created"))
        runtime.assert_not_called()
        command.assert_not_called()
        download.assert_not_called()

    def test_local_runtime_is_rejected_before_any_subprocess_or_download(self):
        with mock.patch.object(prep, "require_runtime", side_effect=prep.PreparationError("hosted_guard")), \
             mock.patch.object(prep.subprocess, "Popen") as popen, \
             mock.patch.object(prep.urllib.request, "urlopen") as urlopen, \
             mock.patch.object(prep.sys, "argv", ["prepare_hosted_shells", "--out", "local/unused"]):
            self.assertEqual(prep.main(), 2)
        popen.assert_not_called()
        urlopen.assert_not_called()


class CommandBoundaryTests(unittest.TestCase):
    @staticmethod
    def event(command="cmd", phase="invoking", elapsed=0, code=None):
        return {"command": command, "phase": phase, "deadline_ms": 10000,
                "elapsed_ms": elapsed, "returncode": code}

    def test_timeout_keeps_actual_command_boundary_without_private_arguments(self):
        secret = "synthetic-private-command-path"
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "preflight"
            directory.mkdir()
            failure = prep.subprocess.TimeoutExpired([secret], 10, output=b"synthetic-private-output")
            with mock.patch.object(prep.subprocess, "run", side_effect=failure) as command, \
                 mock.patch.object(prep.time, "monotonic_ns", side_effect=[0, 10000000000]), \
                 mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                with self.assertRaisesRegex(prep.PreparationError, "^command_deadline$"):
                    prep.run_command([secret], directory, "cmd", cwd=directory)
            command.assert_called_once()
            self.assertEqual(command.call_args.kwargs["timeout"], 10)
            data = json.loads((directory / "command-events.json").read_text(encoding="utf-8"))
            self.assertEqual(data, {"events": [
                {"command": "cmd", "phase": "invoking", "deadline_ms": 10000, "elapsed_ms": 0, "returncode": None},
                {"command": "cmd", "phase": "timed_out", "deadline_ms": 10000, "elapsed_ms": 10000, "returncode": None}]})
            self.assertNotIn(secret, json.dumps(data))
            self.assertNotIn("synthetic-private-output", json.dumps(data))

    def test_returned_command_and_partial_invocation_are_reported_independently(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "preflight"
            directory.mkdir()
            def returned(command, **kwargs):
                kwargs["stdout"].write(b"private-native-output")
                return prep.subprocess.CompletedProcess(command, 7)
            with mock.patch.object(prep.subprocess, "run", side_effect=returned), \
                 mock.patch.object(prep.time, "monotonic_ns", side_effect=[0, 123000000]), \
                 mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                self.assertEqual(prep.run_command(["private-command"], directory, "cmd", cwd=directory),
                                 (7, b"private-native-output"))
            self.assertEqual(prep.public_command_observation(directory, "preflight"),
                             {"status": "observed", "events": [self.event(), self.event(phase="returned", elapsed=123, code=7)]})
            prep.write_json(directory / "command-events.json", {"events": [self.event()]})
            self.assertEqual(prep.public_command_observation(directory, "preflight"),
                             {"status": "observed", "events": [self.event()]})

    def test_unknown_or_unordered_data_is_rejected_without_echoing_it(self):
        good = self.event()
        values = [
            {"events": [{**good, "command": "private-path"}]},
            {"events": [{**good, "argv": ["private-token"]}]},
            {"events": [self.event(phase="returned", code=0)]},
            {"events": [good, self.event(phase="returned", code=True)]},
            {"events": [good, self.event(phase="timed_out", code=91)]},
            {"events": [good, self.event(phase="returned", code=0), self.event(command="ps7")]},
            {"events": [{**good, "deadline_ms": 11000}]},
            {"events": [good, self.event(phase="returned", elapsed=-1, code=0)]},
            {"events": [good] * 9},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "preflight"
            directory.mkdir()
            for value in values:
                with self.subTest(value=value):
                    prep.write_json(directory / "command-events.json", value)
                    self.assertEqual(prep.public_command_observation(directory, "preflight"),
                                     {"status": "invalid", "events": []})
            (directory / "command-events.json").write_bytes(b"private-token" * 1000)
            self.assertEqual(prep.public_command_observation(directory, "preflight"),
                             {"status": "invalid", "events": []})
            (directory / "command-events.json").write_bytes(b"[" * 2000 + b"]" * 2000)
            self.assertEqual(prep.public_command_observation(directory, "preflight"),
                             {"status": "invalid", "events": []})

    def test_trace_write_failure_cannot_replace_observed_timeout(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "preflight"
            directory.mkdir()
            with mock.patch.object(prep.subprocess, "run", side_effect=prep.subprocess.TimeoutExpired(["synthetic"], 10)), \
                 mock.patch.object(prep, "write_command_event", side_effect=[None, prep.PreparationError("command_observation")]), \
                 mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                with self.assertRaisesRegex(prep.PreparationError, "^command_deadline$"):
                    prep.run_command(["synthetic"], directory, "cmd", cwd=directory)

    def test_trace_write_failure_preserves_command_and_marker_result(self):
        marker = "TERMFORGE_HOSTED_CMD_READY"
        output = (marker + "\r\n").encode("ascii")
        write_json = prep.write_json
        for phase in ("invoking", "returned"):
            for code in (0, 7):
                with self.subTest(phase=phase, code=code), tempfile.TemporaryDirectory() as temporary:
                    directory = Path(temporary) / "preflight"
                    directory.mkdir()
                    def failed_write(path, value):
                        if value["events"][-1]["phase"] == phase:
                            raise OSError("synthetic-private-storage-error")
                        write_json(path, value)
                    def returned(command, **kwargs):
                        kwargs["stdout"].write(output)
                        return prep.subprocess.CompletedProcess(command, code)
                    with mock.patch.object(prep, "write_json", side_effect=failed_write), \
                         mock.patch.object(prep.subprocess, "run", side_effect=returned) as command, \
                         mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                        result_code, result_output = prep.run_command(["synthetic"], directory, "cmd", cwd=directory)
                    command.assert_called_once()
                    self.assertEqual((result_code, result_output), (code, output))
                    if code:
                        with self.assertRaisesRegex(prep.PreparationError, "^shell_startup$"):
                            prep.validate_marker(result_output, result_code, marker)
                    else:
                        prep.validate_marker(result_output, result_code, marker)
                    observation = prep.public_command_observation(directory, "preflight")
                    self.assertEqual(observation, {"status": "not_observed", "events": []} if phase == "invoking" else
                                     {"status": "observed", "events": [self.event()]})

    def test_parent_retains_command_or_worker_deadline_after_cleanup(self):
        for worker_timeout in (False, True):
            with self.subTest(worker_timeout=worker_timeout), tempfile.TemporaryDirectory() as temporary:
                out = Path(temporary)
                events = [self.event(), self.event(phase="returned", elapsed=2, code=0), self.event(command="ps51")]
                if not worker_timeout:
                    events.append(self.event(command="ps51", phase="timed_out", elapsed=10000))
                process = mock.Mock(pid=123, returncode=None)
                process.poll.side_effect = lambda: process.returncode
                calls = []
                def communicate(*args, **kwargs):
                    calls.append(kwargs["timeout"])
                    prep.write_json(out / "_private/preflight/command-events.json", {"events": events})
                    if worker_timeout and len(calls) == 1:
                        raise prep.subprocess.TimeoutExpired(["synthetic-worker"], 45)
                    process.returncode = 99 if worker_timeout else 1
                    return (b"" if worker_timeout else b'{"failure_code":"command_deadline"}', b"private-worker-error")
                process.communicate.side_effect = communicate
                job = mock.Mock()
                job.drain.return_value = {"active": 0, "total": 2, "terminated": 0}
                audit = mock.Mock(stable=True, members=[], private=[])
                audit.process_ids.return_value = []
                with mock.patch.object(prep, "OwnedJob", return_value=job), \
                     mock.patch.object(prep, "JobMembers", return_value=audit), \
                     mock.patch.object(prep.subprocess, "Popen", return_value=process), \
                     mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                    result = prep.run_step("preflight", out)
                self.assertEqual(result["status"], "failed")
                self.assertEqual(result["failure_code"], "worker_deadline" if worker_timeout else "command_deadline")
                self.assertEqual(result["command_observation"], {"status": "observed", "events": events})
                self.assertTrue(result["cleanup"]["root_reaped"])
                self.assertEqual(result["cleanup"]["root_natural_exit"], not worker_timeout)
                self.assertEqual(result["cleanup"]["job_termination_requests"], int(worker_timeout))
                self.assertEqual(calls, [45, 5] if worker_timeout else [45])
                self.assertNotIn("private-worker-error", json.dumps(result))


class CommandOutputBoundaryTests(unittest.TestCase):
    def test_parent_reads_written_marker_after_command_timeout_and_verified_cleanup(self):
        marker = b"TERMFORGE_HOSTED_PS51_READY\r\n"
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            process = mock.Mock(pid=123, returncode=None)
            process.poll.side_effect = lambda: process.returncode

            def command_run(command, **kwargs):
                if command == ["synthetic-cmd"]:
                    kwargs["stdout"].write(b"TERMFORGE_HOSTED_CMD_READY\r\n")
                    return prep.subprocess.CompletedProcess(command, 0)
                kwargs["stdout"].write(marker)
                kwargs["stderr"].write(b"private-error-never-publish")
                raise prep.subprocess.TimeoutExpired(command, 10)

            def communicate(*args, **kwargs):
                private = out / "_private/preflight"
                try:
                    prep.run_command(["synthetic-cmd"], private, "cmd", cwd=private)
                    prep.run_command(["synthetic-ps51"], private, "ps51", cwd=private)
                except prep.PreparationError as error:
                    process.returncode = 1
                    return json.dumps({"failure_code": str(error)}).encode(), b""
                self.fail("Synthetic command timeout was lost")

            process.communicate.side_effect = communicate
            job = mock.Mock()
            job.drain.return_value = {"active": 0, "total": 6, "terminated": 0}
            audit = mock.Mock(stable=True, members=[], private=[])
            audit.process_ids.return_value = []
            with mock.patch.object(prep, "OwnedJob", return_value=job), \
                 mock.patch.object(prep, "JobMembers", return_value=audit), \
                 mock.patch.object(prep.subprocess, "Popen", return_value=process), \
                 mock.patch.object(prep.subprocess, "run", side_effect=command_run), \
                 mock.patch.object(prep.subprocess, "CREATE_NO_WINDOW", 0, create=True):
                result = prep.run_step("preflight", out)
            self.assertEqual((out / "_private/preflight/ps51.stdout").read_bytes(), marker)
            self.assertEqual(result["failure_code"], "command_deadline")
            self.assertEqual(result["status"], "failed")
            self.assertTrue(result["cleanup"]["root_reaped"])
            output = result["command_output_observation"]["commands"][1]
            self.assertEqual(output["command"], "ps51")
            self.assertEqual(output["stdout"], {"status": "observed", "bytes": len(marker),
                                             "marker_status": "observed", "marker_complete_line_seen": True})
            self.assertEqual(output["stderr"], {"status": "observed", "bytes": 26})
            self.assertNotIn("private-error", json.dumps(result))


if __name__ == "__main__":
    unittest.main()
