"""Host guard/report tests and real owned synthetic children; never run DISM."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from research.ime import inventory_hosted as inventory


FIXTURE_CHECKPOINT_STAGES = frozenset((
    "fixture-ready", "go-received", "ast-started", "ast-ended", "worker-entered",
    "progress-started", "progress-ended", "progress-failed", "worker-returned",
))


def fixture_checkpoints(directory):
    """Read only bounded, fixed-stage telemetry after the owned host is reaped."""
    observation = {"status": "missing", "events": [], "error": None}
    try:
        with (directory / "fixture-progress.tsv").open("rb") as stream:
            contents = stream.read(4097)
        if len(contents) > 4096 or not contents.endswith(b"\n"):
            raise ValueError("incomplete or oversized fixture checkpoint record")
        rows = contents.decode("ascii").splitlines()
        if not 1 <= len(rows) <= 32:
            raise ValueError("invalid checkpoint count")
        events = []
        previous = -1
        for row in rows:
            milliseconds, stage = row.split("\t")
            if (not milliseconds.isdecimal() or len(milliseconds) > 10
                    or stage not in FIXTURE_CHECKPOINT_STAGES):
                raise ValueError("invalid fixture checkpoint")
            elapsed = int(milliseconds)
            if elapsed < previous:
                raise ValueError("nonmonotonic fixture checkpoint")
            events.append({"elapsed_milliseconds": elapsed, "stage": stage})
            previous = elapsed
        observation.update(status="observed", events=events)
    except FileNotFoundError:
        pass
    except (OSError, UnicodeError, ValueError) as error:
        observation.update(status="unavailable", error=inventory.error_identity(error))
    return observation


class FixtureCheckpoints(unittest.TestCase):
    def test_only_fixed_complete_monotonic_bounded_records_are_public(self):
        for contents in (b"", b"0\tgo-received", b"0\tPRIVATE-PATH\n",
                         b"1\tgo-received\n0\tast-started\n", b"x\tast-started\n",
                         b"0\tast-started\textra\n", b"\xff\tast-started\n",
                         b"0\tast-started\n" * 33, b"X" * 4097):
            with self.subTest(contents=contents[:30]), tempfile.TemporaryDirectory() as directory:
                (Path(directory) / "fixture-progress.tsv").write_bytes(contents)
                result = fixture_checkpoints(Path(directory))
                self.assertEqual(result["status"], "unavailable")
                self.assertEqual(result["events"], [])
                self.assertNotIn("PRIVATE", json.dumps(result))

    def test_absent_and_unreadable_records_remain_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(fixture_checkpoints(Path(directory)),
                             {"status": "missing", "events": [], "error": None})
            with patch.object(Path, "open", side_effect=PermissionError("PRIVATE")):
                result = fixture_checkpoints(Path(directory))
            self.assertEqual(result["status"], "unavailable")
            self.assertEqual(result["events"], [])
            self.assertNotIn("PRIVATE", json.dumps(result))


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

    def pure_script(self, body, lifecycle=False):
        # Only selected AST definitions/blocks execute. No fake CI environment,
        # module import, system inventory or language operations are permitted.
        prefix = r"""
param([string]$Worker)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
"""
        if lifecycle:
            prefix += r"""
$fixtureStopwatch = [Diagnostics.Stopwatch]::StartNew()
$fixtureProgressPath = [IO.Path]::Combine($PSScriptRoot, 'fixture-progress.tsv')
function Save-FixtureCheckpoint([string]$Stage) {
    # CLR-only fixed-stage metadata survives a timeout before final JSON output.
    # Do not load a module or query the environment to produce test telemetry.
    $line = $fixtureStopwatch.ElapsedMilliseconds.ToString([Globalization.CultureInfo]::InvariantCulture) + "`t" + $Stage + "`n"
    [IO.File]::AppendAllText($fixtureProgressPath, $line, [Text.Encoding]::ASCII)
}
Save-FixtureCheckpoint 'fixture-ready'
[IO.File]::WriteAllText([IO.Path]::Combine($PSScriptRoot, 'fixture-ready'), 'ready')
if ([Console]::ReadLine() -cne 'GO') { throw 'Missing fixture GO' }
Save-FixtureCheckpoint 'go-received'
$fixtureAstStarted = $fixtureStopwatch.ElapsedMilliseconds
Save-FixtureCheckpoint 'ast-started'
"""
        prefix += r"""
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($Worker, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Worker syntax errors' }
"""
        if lifecycle:
            prefix += "\n$fixtureAstParseMilliseconds = $fixtureStopwatch.ElapsedMilliseconds - $fixtureAstStarted\nSave-FixtureCheckpoint 'ast-ended'\n"
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "pure-fixture.ps1"
            script.write_text(prefix + body, encoding="utf-8")
            command = [str(self.powershell()), "-NoProfile", "-NonInteractive", "-NoLogo",
                "-ExecutionPolicy", "Bypass", "-File", str(script),
                "-Worker", str(Path(inventory.__file__).with_name("inventory_worker.ps1"))]
            if lifecycle:
                return self.run_lifecycle_fixture(command, Path(directory))
            result = subprocess.run(command, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
            return json.loads(result.stdout)

    def run_lifecycle_fixture(self, command, directory):
        timeout = 10
        started = time.monotonic()
        job = process = None
        stdout = stderr = b""
        record = {"timeout_seconds": timeout, "elapsed_seconds": None, "ready_seconds": None,
                  "returncode": None, "reaped": False, "streams_drained": False, "timed_out": False,
                  "job_before_cleanup": None, "job_after": None, "cleanup_errors": [], "error": None,
                  "outcome": None, "checkpoint_observation": None}
        try:
            job = inventory.OwnedJob()
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
            job.assign(process.pid)
            deadline = started + timeout
            ready = directory / "fixture-ready"
            while not ready.exists() and process.poll() is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, timeout)
                time.sleep(min(0.01, remaining))
            record["ready_seconds"] = time.monotonic() - started
            if process.poll() is not None and not ready.exists():
                raise RuntimeError("PowerShell fixture did not reach its gate")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(command, timeout)
            stdout, stderr = process.communicate(b"GO\n", timeout=remaining)
            record["returncode"] = process.returncode
            self.assertEqual(process.returncode, 0, stderr.decode("utf-8", errors="replace"))
            result = json.loads(stdout)
            result["process"] = record
            record["outcome"] = {key: result[key] for key in
                ("status", "states", "state_timeline", "ast_parse_milliseconds")}
            return result
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            raise
        except Exception as error:
            record["error"] = inventory.error_identity(error)
            raise
        finally:
            if job is not None:
                try:
                    record["job_before_cleanup"] = job.accounting()
                    job.terminate()
                except Exception as error:
                    record["cleanup_errors"].append(inventory.error_identity(error))
            if process is not None:
                try:
                    if process.poll() is None:
                        process.kill()
                    stdout, stderr = process.communicate(timeout=5)
                    record["returncode"] = process.returncode
                    record["reaped"] = process.poll() is not None
                    record["streams_drained"] = True
                except Exception as error:
                    record["cleanup_errors"].append(inventory.error_identity(error))
            if job is not None:
                try:
                    record["job_after"] = job.drain(timeout=5)
                    if record["job_after"]["active"]:
                        record["cleanup_errors"].append({"kind": "owned-job-not-empty", "code": None})
                except Exception as error:
                    record["cleanup_errors"].append(inventory.error_identity(error))
                finally:
                    job.close()
            record["elapsed_seconds"] = time.monotonic() - started
            record["checkpoint_observation"] = fixture_checkpoints(directory)
            print(json.dumps({"fixture": "powershell-final-progress", "lifecycle": record}), flush=True)

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

    @staticmethod
    def failed_progress_body(pause_final=False):
        body = r"""
$Operation = 'powershell'; $phase = $Operation
$report = [ordered]@{status = 'failed'; data = [ordered]@{}}
$script:states = @()
$script:stateTimeline = @()
function Save-Progress([string]$Phase, [string]$State, $ErrorInfo = $null) {
    Save-FixtureCheckpoint ('progress-' + $State)
    $script:states += $State
    $script:stateTimeline += [pscustomobject]@{state = $State; elapsed_milliseconds = $fixtureStopwatch.ElapsedMilliseconds}
    if ($State -eq 'ended') { __PAUSE_FINAL__ throw 'Synthetic final progress write failure' }
}
$body = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($body.Count -ne 1) { throw 'Expected one top-level worker operation boundary' }
Save-FixtureCheckpoint 'worker-entered'
. ([scriptblock]::Create($body[0].Extent.Text))
Save-FixtureCheckpoint 'worker-returned'
@{status=$report.status; error=$report.data.error; states=$script:states; state_timeline=@($script:stateTimeline);
  ast_parse_milliseconds=$fixtureAstParseMilliseconds} | ConvertTo-Json -Depth 8 -Compress
"""
        return body.replace("__PAUSE_FINAL__", "[Threading.Thread]::Sleep(20000);" if pause_final else "")

    def test_failed_final_progress_cannot_return_complete(self):
        result = self.pure_script(self.failed_progress_body(), lifecycle=True)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["states"], ["started", "ended", "failed"])
        self.assertIn("kind", result["error"])
        self.assertEqual(result["process"]["returncode"], 0)
        self.assertTrue(result["process"]["reaped"])
        self.assertTrue(result["process"]["streams_drained"])
        self.assertEqual(result["process"]["job_after"]["active"], 0)
        self.assertEqual(result["process"]["cleanup_errors"], [])
        self.assertEqual(result["process"]["timeout_seconds"], 10)
        self.assertFalse(result["process"]["timed_out"])
        self.assertGreaterEqual(result["process"]["ready_seconds"], 0)
        self.assertGreaterEqual(result["ast_parse_milliseconds"], 0)
        self.assertEqual([row["state"] for row in result["state_timeline"]], result["states"])
        self.assertEqual([row["stage"] for row in result["process"]["checkpoint_observation"]["events"]],
            ["fixture-ready", "go-received", "ast-started", "ast-ended", "worker-entered",
             "progress-started", "progress-ended", "progress-failed", "worker-returned"])
        self.assertEqual(result["process"]["checkpoint_observation"]["status"], "observed")

    def test_lifecycle_timeout_is_preserved_after_owned_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            command = [str(self.powershell()), "-NoProfile", "-NonInteractive", "-NoLogo",
                       "-Command", "[Console]::ReadLine()"]
            with patch("builtins.print") as output, self.assertRaises(subprocess.TimeoutExpired):
                self.run_lifecycle_fixture(command, Path(directory))
            output.assert_called_once()
            record = json.loads(output.call_args.args[0])["lifecycle"]
            self.assertTrue(record["timed_out"])
            self.assertEqual(record["timeout_seconds"], 10)
            self.assertIsNone(record["ready_seconds"])
            self.assertTrue(record["reaped"])
            self.assertNotEqual(record["returncode"], 0)
            self.assertEqual(record["job_after"]["active"], 0)
            self.assertEqual(record["cleanup_errors"], [])
            self.assertEqual(record["checkpoint_observation"]["status"], "missing")

    def test_timeout_keeps_post_gate_checkpoint_without_a_final_report(self):
        with patch("builtins.print") as output, self.assertRaises(subprocess.TimeoutExpired):
            self.pure_script(self.failed_progress_body(pause_final=True), lifecycle=True)
        output.assert_called_once()
        record = json.loads(output.call_args.args[0])["lifecycle"]
        self.assertTrue(record["timed_out"])
        self.assertIsNone(record["outcome"])
        self.assertEqual(record["checkpoint_observation"]["status"], "observed")
        self.assertEqual([row["stage"] for row in record["checkpoint_observation"]["events"]],
                         ["fixture-ready", "go-received", "ast-started", "ast-ended", "worker-entered",
                          "progress-started", "progress-ended"])
        self.assertEqual(record["timeout_seconds"], 10)
        self.assertTrue(record["reaped"])
        self.assertTrue(record["streams_drained"])
        self.assertEqual(record["job_after"]["active"], 0)
        self.assertEqual(record["cleanup_errors"], [])

    def test_tip_inventory_enumerates_a_non_enumerated_cmdlet_collection(self):
        body = r"""
$Operation = 'tip'; $phase = $Operation
$report = [ordered]@{status = 'failed'; data = [ordered]@{}}
function Save-Progress([string]$Phase, [string]$State, $ErrorInfo = $null) {}
function Test-Path { param($LiteralPath) return $LiteralPath.StartsWith('HKLM:') }
function Get-WinUserLanguageList {
    $languages = [Collections.Generic.List[object]]::new()
    $languages.Add([pscustomobject]@{LanguageTag='en-US'; InputMethodTips=@('0409:00000409')})
    $languages.Add([pscustomobject]@{LanguageTag='__ZH_TAG__'; InputMethodTips=@(
        '0804:{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}{FA550B04-5AD7-411F-A5AC-CA038EC515D7}')})
    return ,$languages
}
$body = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($body.Count -ne 1) { throw 'Expected one worker operation boundary' }
. ([scriptblock]::Create($body[0].Extent.Text))
$report | ConvertTo-Json -Depth 8 -Compress
"""
        for tag in ("zh-CN", "zh-Hans-CN"):
            with self.subTest(tag=tag):
                result = self.pure_script(body.replace("__ZH_TAG__", tag))
                self.assertEqual(result["status"], "complete")
                self.assertTrue(result["data"]["user_listed"])
                self.assertEqual(result["data"]["relevant_languages"], ["en-US", tag])

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
