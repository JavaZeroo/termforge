"""Pure fixture policy regressions plus one own-Job API check; never start WSL."""
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import prepare_hosted_shells as prep
from tools.owned_job_cleanup import JobMembers, helper_cleanup_accepted, image_role


def known_host():
    return {"member": 0, "role": "windows-wsl-host", "image_scope": "system32",
            "exact_job_membership_verified": True, "membership_query_error": 0,
            "image_query_error": 0, "exit_query_error": 0,
            "exited_after_cleanup": True, "exit_code_after_cleanup": 99}


class CleanupPolicyTests(unittest.TestCase):
    def test_image_query_failure_is_not_failure_to_reap_an_owned_fixture_tree(self):
        # Sanitized facts from CI 40265785235: these are policy inputs only.
        # No hosted environment is exported and no WSL command runs here.
        members = [{**known_host(), "member": index, "role": "unknown", "image_scope": "unverified",
                    "membership_query_error": 0, "image_query_error": 31,
                    "exit_query_error": 0} for index in range(2)]
        cleanup = {"root_natural_exit": True, "root_reaped": True, "root_exit_code": 0,
                   "root_direct_kill_requested": False, "membership_stable": True,
                   "job_before": {"active": 2, "total": 26, "terminated": 0},
                   "job_after": {"active": 0, "total": 26, "terminated": 0}, "job_after_pid_count": 0,
                   "job_termination_requests": 1, "member_cleanup_requested_count": 2, "members": members}
        self.assertTrue(helper_cleanup_accepted("check-before", True, cleanup))
        self.assertTrue(helper_cleanup_accepted("check-after", True, cleanup))
        self.assertEqual([member["role"] for member in cleanup["members"]], ["unknown", "unknown"])
        self.assertEqual([member["image_query_error"] for member in cleanup["members"]], [31, 31])
        for field, value in (("membership_query_error", 5), ("exit_query_error", 5),
                             ("exact_job_membership_verified", False), ("exited_after_cleanup", False)):
            with self.subTest(field=field):
                bad = copy.deepcopy(cleanup); bad["members"][0][field] = value
                self.assertFalse(helper_cleanup_accepted("check-before", True, bad))

    def test_image_name_alone_never_qualifies_an_unknown_helper(self):
        for image in (r"D:\synthetic\wslhost.exe", r"C:\Windows\System32\init",
                      r"C:\Windows\System32\wsl.exe", r"D:\synthetic\init"):
            self.assertNotEqual(image_role(image, r"C:\Windows\System32", r"C:\Program Files")[0],
                                "windows-wsl-host")
        self.assertEqual(image_role(r"C:\Windows\System32\wslhost.exe", r"C:\Windows\System32",
                                    r"C:\Program Files"), ("windows-wsl-host", "system32"))

    def test_only_owned_and_reaped_check_trees_can_use_fixture_cleanup_policy(self):
        good = {"root_natural_exit": True, "root_reaped": True, "root_exit_code": 0,
                "root_direct_kill_requested": False, "member_cleanup_requested_count": 1,
                "membership_stable": True, "job_before": {"active": 1, "total": 3, "terminated": 0},
                "job_after": {"active": 0, "total": 3, "terminated": 0}, "job_after_pid_count": 0,
                "job_termination_requests": 1, "members": [known_host()]}
        self.assertTrue(helper_cleanup_accepted("check-before", True, good))
        self.assertTrue(helper_cleanup_accepted("check-after", True, good))
        for field, value in (("root_natural_exit", False), ("root_reaped", False), ("root_exit_code", 99),
                             ("root_direct_kill_requested", True), ("member_cleanup_requested_count", 0),
                             ("membership_stable", False), ("job_after_pid_count", 1),
                             ("job_termination_requests", 2), ("members", []), ("job_after", {"active": 1})):
            with self.subTest(field=field):
                bad = copy.deepcopy(good); bad[field] = value
                self.assertFalse(helper_cleanup_accepted("check-before", True, bad))
        for field, value in (("exact_job_membership_verified", False), ("membership_query_error", 5),
                             ("exit_query_error", 5),
                             ("exited_after_cleanup", False), ("exit_code_after_cleanup", None)):
            with self.subTest(member_field=field):
                bad = copy.deepcopy(good); bad["members"][0][field] = value
                self.assertFalse(helper_cleanup_accepted("check-before", True, bad))
        self.assertFalse(helper_cleanup_accepted("check-before", False, good))
        for step in ("preflight", "download", "import"):
            self.assertFalse(helper_cleanup_accepted(step, True, good))

    def fixture_run(self, module=prep, role="windows-wsl-host", root_code=0):
        process = mock.Mock(pid=123, returncode=root_code)
        process.poll.return_value = root_code
        process.communicate.return_value = (b'{"result":"ubuntu-sh-cwd-verified"}', b"")
        job = mock.Mock()
        job.drain.side_effect = [{"active": 1, "total": 3, "terminated": 0},
                                 {"active": 0, "total": 3, "terminated": 0}]
        audit = mock.Mock(stable=True, members=[{**known_host(), "role": role}])
        audit.private = [{"member": 0, "pid": 456, "image": "synthetic-not-executed"}]
        audit.process_ids.return_value = []
        with tempfile.TemporaryDirectory() as temporary, \
             mock.patch.object(module, "OwnedJob", return_value=job), \
             mock.patch.object(module, "JobMembers", return_value=audit, create=True), \
             mock.patch.object(module.subprocess, "CREATE_NO_WINDOW", 0, create=True), \
             mock.patch.object(module.subprocess, "Popen", return_value=process):
            result = module.run_step("check-before", Path(temporary))
        return result, job

    def test_real_run_step_seam_distinguishes_worker_success_and_cleanup(self):
        result, job = self.fixture_run()
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["cleanup"]["root_exit_code"], 0)
        self.assertEqual(result["cleanup"]["job_before"]["active"], 1)
        self.assertEqual(result["cleanup"]["job_after"]["active"], 0)
        self.assertEqual(result["cleanup"]["helper_cleanup_policy"], "owned-disposable-wsl-check-tree-v1")
        job.terminate.assert_called_once()
        unknown, _ = self.fixture_run(role="unknown")
        self.assertEqual(unknown["status"], "passed")
        self.assertEqual(unknown["cleanup"]["members"][0]["role"], "unknown")
        failed_worker, _ = self.fixture_run(root_code=7)
        self.assertEqual(failed_worker["status"], "failed")


@unittest.skipUnless(os.name == "nt", "read-only owned Job identity API requires Windows")
class OwnJobApiTests(unittest.TestCase):
    def test_ordinary_owned_python_helper_is_audited_unknown_and_reaped(self):
        job = prep.OwnedJob()
        process = audit = None
        readiness = tempfile.TemporaryDirectory(prefix="termforge-job-ready-")
        try:
            script = ("import subprocess,sys,time\nfrom pathlib import Path\n"
                      "assert sys.stdin.buffer.readline()==b'GO\\n'\n"
                      "child = 'import sys,time; from pathlib import Path; '\n"
                      "child += 'Path(sys.argv[1]).write_bytes(bytes([82,69,65,68,89])); time.sleep(30)'\n"
                      "subprocess.Popen([sys.executable,'-c',child,sys.argv[1]],"
                      "stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\n"
                      "ready=Path(sys.argv[1]); deadline=time.monotonic()+4\n"
                      "while not ready.exists() and time.monotonic()<deadline: time.sleep(.01)\n"
                      "assert ready.read_bytes()==b'READY'\n")
            # Wait for the actual helper interpreter, including any venv
            # launcher descendants, before taking the stable Job snapshot.
            process = subprocess.Popen([sys.executable, "-c", script, str(Path(readiness.name) / "ready")], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            job.assign(process.pid)
            process.communicate(b"GO\n", timeout=5)
            self.assertEqual(process.returncode, 0)
            audit = JobMembers(job, process.pid)
            self.assertTrue(audit.stable)
            # A venv launcher can create more than one native Python process.
            self.assertGreaterEqual(len(audit.members), 1)
            self.assertTrue(all(member["role"] == "unknown" for member in audit.members))
            self.assertTrue(all(member["exact_job_membership_verified"] for member in audit.members))
            job.terminate()
            self.assertEqual(job.drain()["active"], 0)
            self.assertEqual(audit.process_ids(), [])
            audit.observe_exit()
            self.assertTrue(all(member["exited_after_cleanup"] for member in audit.members))
        finally:
            if audit is not None: audit.close()
            job.terminate(); job.drain(); job.close()
            if process is not None and process.poll() is None:
                process.kill(); process.communicate(timeout=5)
            readiness.cleanup()


if __name__ == "__main__": unittest.main()
