"""Host guard/report tests and real owned synthetic children; never run DISM."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ime import inventory_hosted as inventory


class InventoryReports(unittest.TestCase):
    def test_guard_rejects_without_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "refused"
            environment = dict(os.environ)
            for name in ("GITHUB_ACTIONS", "RUNNER_ENVIRONMENT", "GITHUB_REPOSITORY"):
                environment.pop(name, None)
            result = subprocess.run(
                [sys.executable, "-m", "research.ime.inventory_hosted", "--out", str(out)],
                env=environment, capture_output=True, timeout=10,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(out.exists())

    def test_guard_metadata_validation_is_pure(self):
        environment = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted",
                       "RUNNER_OS": "Windows", "GITHUB_REPOSITORY": "JavaZeroo/termforge",
                       "ImageOS": "win25-vs2026", "ImageVersion": "20261021.278.1"}
        inventory.require_hosted(environment)
        for key in environment:
            with self.subTest(key=key):
                changed = dict(environment, **{key: "invalid"})
                with self.assertRaises(ValueError):
                    inventory.require_hosted(changed)

    def test_claim_is_exclusive_and_preserves_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "run"
            def claim():
                try:
                    inventory.claim_directory(out)
                    return True
                except FileExistsError:
                    return False
            with ThreadPoolExecutor(max_workers=2) as pool:
                self.assertEqual(sorted(pool.map(lambda _: claim(), range(2))), [False, True])
            (out / "failure.json").write_text("old failure", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                inventory.claim_directory(out)
            self.assertEqual((out / "failure.json").read_text(), "old failure")

    def test_atomic_report_failure_keeps_prior_record(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            inventory.write_json(report, {"phase": "query", "state": "started"})
            with patch.object(inventory.os, "replace", side_effect=OSError("synthetic")):
                with self.assertRaises(OSError):
                    inventory.write_json(report, {"phase": "query", "state": "ended"})
            self.assertEqual(json.loads(report.read_text()), {"phase": "query", "state": "started"})

    def test_log_summary_has_no_raw_text_and_reads_bounded_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "dism.log"
            log.write_text("PRIVATE-PATH\\name secret=abc 0xDEADC0DE\nHRESULT = 0x800F0954 error\n" * 5000,
                           encoding="utf-8")
            result = inventory.summarize_dism(log)
            text = json.dumps(result)
            self.assertNotIn("PRIVATE", text)
            self.assertNotIn("secret", text)
            self.assertTrue(result["truncated"])
            self.assertLessEqual(result["examined_bytes"], 65536)
            self.assertIn("0x800F0954", result["hresults"])
            self.assertNotIn("0xDEADC0DE", result["hresults"])

    def test_workflow_defaults_to_inventory_and_keeps_verify_explicit(self):
        workflow = (inventory.ROOT / ".github/workflows/r1-ime.yml").read_text(encoding="utf-8")
        self.assertIn("default: inventory", workflow)
        self.assertIn("- inventory", workflow)
        self.assertIn("- verify", workflow)
        verify_steps = ("Configure probe", "Build native IME probe",
                        "Activate existing fixed Microsoft Pinyin in disposable hosted VM",
                        "Require real composition, candidate position, commit and cancel")
        for name in verify_steps:
            step = workflow.split("- name: " + name, 1)[1].split("- name:", 1)[0]
            self.assertIn("inputs.mode == 'verify'", step, name)
        self.assertNotIn("*.log", workflow)

    def test_missing_capability_is_observation_not_readiness(self):
        with tempfile.TemporaryDirectory() as directory:
            def observed(operation, *_):
                return {"phase": operation, "status": "complete", "data": {
                    "exposed": False, "capabilities": []}}
            with patch.object(inventory, "run_operation", side_effect=observed):
                result = inventory.collect_inventory(Path(directory), Path("unused"), 60,
                    {"ImageOS": "synthetic", "ImageVersion": "fixture"})
            self.assertEqual(result["status"], "inventory-complete-ime-unverified")
            self.assertFalse(result["real_ime_workflow_verified"])

    def test_operation_failure_stops_and_preserves_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(inventory, "run_operation", side_effect=[
                    {"phase": "os", "status": "complete", "data": {}},
                    OSError("synthetic PRIVATE PATH")]) as operation:
                result = inventory.collect_inventory(Path(directory), Path("unused"), 60,
                    {"ImageOS": "synthetic", "ImageVersion": "fixture"})
            self.assertEqual(operation.call_count, 2)
            self.assertEqual(result["status"], "inventory-failed")
            self.assertIsNotNone(result["ended_utc"])
            saved = (Path(directory) / "inventory.json").read_text()
            self.assertNotIn("PRIVATE", saved)
            self.assertEqual(json.loads(saved)["operations"][0]["phase"], "os")

    def test_reaped_query_failure_preserves_other_observations_without_passing(self):
        with tempfile.TemporaryDirectory() as directory:
            def observed(operation, *_):
                return {"phase": operation, "status": "failed" if operation == "dism" else "complete",
                        "data": {}, "process_reaped": True, "job_after": {"active": 0},
                        "cleanup_errors": []}
            with patch.object(inventory, "run_operation", side_effect=observed) as operation:
                result = inventory.collect_inventory(Path(directory), Path("unused"), 60,
                    {"ImageOS": "synthetic", "ImageVersion": "fixture"})
            self.assertEqual(operation.call_count, len(inventory.OPERATIONS))
            self.assertEqual([row["phase"] for row in result["operations"]], list(inventory.OPERATIONS))
            self.assertEqual(result["status"], "inventory-failed")
            self.assertFalse(result["real_ime_workflow_verified"])

    def test_unknown_or_incomplete_cleanup_stops_inventory(self):
        for cleanup in ({}, {"process_reaped": False, "job_after": {"active": 0}},
                        {"process_reaped": True, "job_after": {"active": 1}},
                        {"process_reaped": True, "job_after": {"active": 0}, "cleanup_errors": ["failure"]}):
            with self.subTest(cleanup=cleanup), tempfile.TemporaryDirectory() as directory:
                with patch.object(inventory, "run_operation", return_value={
                        "phase": "os", "status": "failed", "data": {}, **cleanup}) as operation:
                    result = inventory.collect_inventory(Path(directory), Path("unused"), 60,
                        {"ImageOS": "synthetic", "ImageVersion": "fixture"})
                self.assertEqual(operation.call_count, 1)
                self.assertEqual(result["status"], "inventory-failed")


@unittest.skipUnless(os.name == "nt", "owned Windows Job lifecycle")
class InventoryProcesses(unittest.TestCase):
    def test_private_evidence_failure_still_finishes_failed_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            source = ("import sys,json; sys.stdin.readline(); print(json.dumps({"
                      "'schema':'termforge-ime-inventory-worker-v1','operation':'synthetic',"
                      "'status':'complete','data':{}}))")
            write_bytes = Path.write_bytes
            def fail_private(path, contents):
                if path.name == "stdout.private":
                    raise PermissionError("synthetic private path")
                return write_bytes(path, contents)
            with patch.object(Path, "write_bytes", fail_private):
                result = inventory.run_operation("synthetic", Path(directory),
                    [sys.executable, "-c", source], 5)
            self.assertEqual(result["status"], "evidence-failed")
            self.assertEqual(result["job_after"]["active"], 0)
            saved = json.loads((Path(directory) / "synthetic/phase.json").read_text())
            self.assertEqual(saved["status"], "evidence-failed")
            self.assertIsNotNone(saved["ended_utc"])
            self.assertEqual(saved["evidence_errors"][0]["artifact"], "stdout.private")
            self.assertNotIn("synthetic private path", json.dumps(saved))

    def test_assignment_failure_never_releases_gate_and_reaps_host(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "wrongly-released"
            child = Path(directory) / "gate.py"
            child.write_text("import sys,pathlib\nassert sys.stdin.readline().strip() == 'GO'\n"
                             "pathlib.Path(sys.argv[1]).touch()\n", encoding="utf-8")
            with patch.object(inventory.OwnedJob, "assign", side_effect=OSError("synthetic")):
                result = inventory.run_operation("synthetic", Path(directory),
                    [sys.executable, str(child), str(marker)], 5)
            self.assertEqual(result["status"], "failed")
            self.assertFalse(marker.exists())
            self.assertTrue(result["process_reaped"])
            self.assertEqual(result["job_after"]["active"], 0)
            self.assertNotIn("releasing-query-gate", [item["phase"] for item in result["lifecycle"]])

    def test_success_and_failure_have_finished_phase_and_drained_job(self):
        for code in (0, 7):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as directory:
                source = ("import sys,json; assert sys.stdin.readline().strip()=='GO'; "
                          "print(json.dumps({'schema':'termforge-ime-inventory-worker-v1',"
                          "'operation':'synthetic','status':'complete','data':{'fixture':True}})); "
                          f"sys.exit({code})")
                result = inventory.run_operation("synthetic", Path(directory),
                    [sys.executable, "-c", source], 5)
                self.assertEqual(result["returncode"], code)
                self.assertEqual(result["status"], "complete" if code == 0 else "failed")
                self.assertTrue(result["process_reaped"])
                self.assertEqual(result["job_after"]["active"], 0)
                saved = json.loads((Path(directory) / "synthetic/phase.json").read_text())
                self.assertIsNotNone(saved["started_utc"])
                self.assertIsNotNone(saved["ended_utc"])
                self.assertGreater(saved["elapsed_seconds"], 0)

    def test_timeout_reaps_real_child_and_grandchild(self):
        with tempfile.TemporaryDirectory() as directory:
            child = Path(directory) / "synthetic_child.py"
            child.write_text(
                "import subprocess,sys,time\n"
                "assert sys.stdin.readline().strip() == 'GO'\n"
                "peer = subprocess.Popen([sys.executable, '-u', '-c', "
                "\"import time; print('grandchild-ready', flush=True); time.sleep(60)\"], "
                "stdout=subprocess.PIPE, text=True)\n"
                "assert peer.stdout.readline().strip() == 'grandchild-ready'\n"
                "print('both-ready', flush=True)\n"
                "time.sleep(60)\n", encoding="utf-8")
            result = inventory.run_operation("synthetic", Path(directory),
                [sys.executable, "-u", str(child)], 3)
            self.assertEqual(result["status"], "timed-out")
            self.assertEqual(result["error"]["kind"], "deadline-exceeded")
            self.assertGreaterEqual(result["job_before_cleanup"]["active"], 2)
            self.assertTrue(result["process_reaped"])
            self.assertEqual(result["job_after"]["active"], 0)
            self.assertLess(result["elapsed_seconds"], 10)
            self.assertEqual((Path(directory) / "synthetic/stdout.private").read_text().strip(), "both-ready")

    def test_invalid_json_remains_failed_after_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            result = inventory.run_operation("synthetic", Path(directory),
                [sys.executable, "-c", "import sys; sys.stdin.readline(); print('bad')"], 5)
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["error"]["kind"], "invalid-worker-report")
            self.assertEqual(result["job_after"]["active"], 0)


@unittest.skipUnless(os.name == "nt", "Windows PowerShell syntax and pure helper tests")
class WorkerContract(unittest.TestCase):
    @staticmethod
    def powershell():
        return Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"

    def pure_script(self, body):
        # Only selected AST definitions/blocks execute. No fake CI environment,
        # module import, system inventory or language operations are permitted.
        prefix = r"""
param([string]$Worker)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($Worker, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Worker syntax errors' }
"""
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "pure-fixture.ps1"
            script.write_text(prefix + body, encoding="utf-8")
            result = subprocess.run([str(self.powershell()), "-NoProfile", "-NonInteractive", "-NoLogo",
                "-ExecutionPolicy", "Bypass", "-File", str(script),
                "-Worker", str(Path(inventory.__file__).with_name("inventory_worker.ps1"))],
                capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
            return json.loads(result.stdout)

    def test_worker_syntax_and_parameter_sets_are_checked_without_union(self):
        result = self.pure_script(r"""
$definition = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-QueryContract'}, $true)
. ([scriptblock]::Create($definition.Extent.Text))
function ParameterSet($Name, $Names) {
    [pscustomobject]@{Name = $Name; Parameters = @($Names | ForEach-Object { [pscustomobject]@{Name = $_} })}
}
$good = Get-QueryContract ([pscustomobject]@{ParameterSets = @(
    (ParameterSet 'OnlineSet' @('Online', 'Name', 'LimitAccess', 'LogPath')))})
$split = Get-QueryContract ([pscustomobject]@{ParameterSets = @(
    (ParameterSet 'First' @('Online', 'Name', 'LimitAccess')),
    (ParameterSet 'Second' @('Online', 'Name', 'LogPath')))})
$offline = Get-QueryContract ([pscustomobject]@{ParameterSets = @(
    (ParameterSet 'Offline' @('Path', 'Name', 'LimitAccess', 'LogPath')))})
@{good=$good; split=$split; offline=$offline} | ConvertTo-Json -Depth 8 -Compress
""")
        self.assertEqual(result["good"]["online_parameter_set"], "OnlineSet")
        self.assertEqual(result["good"]["missing_parameters"], [])
        self.assertEqual(result["split"]["missing_parameters"], ["LogPath"])
        self.assertIsNone(result["offline"]["online_parameter_set"])
        self.assertIn("Online", result["offline"]["missing_parameters"])

    def test_failed_final_progress_cannot_return_complete(self):
        result = self.pure_script(r"""
$Operation = 'powershell'; $phase = $Operation
$report = [ordered]@{status = 'failed'; data = [ordered]@{}}
$script:states = @()
function Save-Progress([string]$Phase, [string]$State, $ErrorInfo = $null) {
    $script:states += $State
    if ($State -eq 'ended') { throw 'Synthetic final progress write failure' }
}
$body = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($body.Count -ne 1) { throw 'Expected one top-level worker operation boundary' }
. ([scriptblock]::Create($body[0].Extent.Text))
@{status=$report.status; error=$report.data.error; states=$script:states} | ConvertTo-Json -Depth 8 -Compress
""")
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["states"], ["started", "ended", "failed"])
        self.assertIn("kind", result["error"])

    def test_file_hash_uses_stream_without_optional_powershell_commands(self):
        result = self.pure_script(r"""
$definition = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-FileSha256'}, $true)
. ([scriptblock]::Create($definition.Extent.Text))
$path = [IO.Path]::Combine($PSScriptRoot, 'synthetic.bin')
[IO.File]::WriteAllBytes($path, [byte[]]@(97,98,99))
$hash = Get-FileSha256 $path
[IO.File]::Move($path, $path + '.moved')
@{hash=$hash; released=[IO.File]::Exists($path + '.moved')} | ConvertTo-Json -Compress
""")
        self.assertEqual(result["hash"], "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        self.assertTrue(result["released"])

    def test_real_progress_file_can_be_replaced_in_windows_powershell(self):
        result = self.pure_script(r"""
$definition = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Save-Progress'}, $true)
. ([scriptblock]::Create($definition.Extent.Text))
$progressPath = [IO.Path]::Combine($PSScriptRoot, 'progress.json')
$started = [Diagnostics.Stopwatch]::StartNew()
Save-Progress 'synthetic-file-only' 'started'
$first = Get-Content -LiteralPath $progressPath -Raw | ConvertFrom-Json
Save-Progress 'synthetic-file-only' 'ended'
$second = Get-Content -LiteralPath $progressPath -Raw | ConvertFrom-Json
@{first=$first; second=$second; temporary_exists=[IO.File]::Exists($progressPath + '.tmp');
  powershell_major=$PSVersionTable.PSVersion.Major} | ConvertTo-Json -Depth 8 -Compress
""")
        self.assertEqual(result["powershell_major"], 5)
        self.assertEqual(result["first"]["state"], "started")
        self.assertEqual(result["second"]["state"], "ended")
        self.assertEqual(result["second"]["phase"], "synthetic-file-only")
        self.assertFalse(result["temporary_exists"])

    def test_worker_rejects_eof_or_nonhosted_go_before_operations(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "must-not-exist"
            environment = dict(os.environ)
            environment.pop("GITHUB_ACTIONS", None)
            environment.pop("RUNNER_ENVIRONMENT", None)
            for message, code in ((b"", 90), (b"GO\n", 91)):
                with self.subTest(code=code):
                    result = subprocess.run([str(self.powershell()), "-NoProfile", "-NonInteractive", "-NoLogo",
                        "-ExecutionPolicy", "Bypass", "-File",
                        str(Path(inventory.__file__).with_name("inventory_worker.ps1")),
                        "-Operation", "capability", "-Out", str(out)],
                        env=environment, input=message, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, code)
                    self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
