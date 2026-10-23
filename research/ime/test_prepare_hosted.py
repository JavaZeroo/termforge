"""Pure AST-extracted preparation tests; never change workstation languages."""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "research/ime/prepare_hosted.ps1"
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
PROFILE = "0804:{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}{FA550B04-5AD7-411F-A5AC-CA038EC515D7}"


@unittest.skipUnless(os.name == "nt", "real PowerShell AST/file tests require Windows")
class ExistingProviderPreparation(unittest.TestCase):
    def simulated(self, failure: str = "none") -> dict:
        with tempfile.TemporaryDirectory(prefix="termforge-ime-pure-preparation-") as temporary:
            directory = Path(temporary)
            test = directory / "pure.ps1"
            # Only function bodies, the literal report initializer, and its
            # top-level error handling are evaluated. The production hosted,
            # administrator and workspace guards are never bypassed to run it.
            source = r"""
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$scriptPath = '__SCRIPT__'
$outputPath = $PSScriptRoot
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Production preparation script failed to parse' }
$started = [Diagnostics.Stopwatch]::StartNew()
$initializer = $ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$report'
}
if (@($initializer).Count -ne 1) { throw 'Expected one production report initializer' }
. ([scriptblock]::Create($initializer.Extent.Text))
foreach ($name in @('Save-Environment','Start-SetupPhase','Complete-SetupPhase',
                    'Get-LocalBasicCapability','Invoke-ExistingProviderSetup')) {
    $definition = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name}, $true)
    if ($null -eq $definition) { throw 'Expected production function absent' }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$script:failure = '__FAILURE__'
$script:binaryReads = 0; $script:setCalls = 0; $script:capabilityReads = 0
$script:setLanguages = @()
function Get-CimInstance { param($ClassName)
    if ($ClassName -ne 'Win32_OperatingSystem') { throw 'Unexpected OS query' }
    return [pscustomobject]@{Caption='synthetic OS'; Version='1.2.3'; BuildNumber='123'}
}
function Get-WindowsCapability { param([switch]$Online, $Name, [switch]$LimitAccess, $LogPath)
    if (-not $Online -or -not $LimitAccess -or $Name -ne 'Language.Basic~~~zh-CN~0.0.1.0') {
        throw 'Unexpected capability query or online-source access'
    }
    $script:capabilityReads++
    if ($script:failure -eq 'capability') { return }
    return [pscustomobject]@{Name=$Name; State='NotPresent'}
}
function Get-ExistingProviderBinary { param($Path)
    if (-not $Path.EndsWith('System32\InputMethod\CHS\ChsIME.exe')) { throw 'Unexpected provider path' }
    $script:binaryReads++
    if ($script:failure -eq 'binary') { throw 'Synthetic missing provider' }
    $hash = 'a' * 64
    if ($script:failure -eq 'changed-binary' -and $script:binaryReads -gt 1) { $hash = 'b' * 64 }
    return [ordered]@{name='ChsIME.exe'; version='synthetic-version'; sha256=$hash}
}
function Test-Path { param($LiteralPath)
    if ($LiteralPath -notlike 'HKLM:\SOFTWARE\Microsoft\CTF\TIP\*') { throw 'Unexpected registry read' }
    return $script:failure -ne 'machine-tip'
}
function New-WinUserLanguageList { param($Language)
    if ($Language -notin @('en-US','zh-CN')) { throw 'Unexpected language' }
    $list = [Collections.Generic.List[object]]::new()
    $tips = [Collections.Generic.List[string]]::new()
    $tips.Add('synthetic-default-tip')
    $list.Add([pscustomobject]@{LanguageTag=$Language; InputMethodTips=$tips})
    return ,$list
}
function Set-WinUserLanguageList { param($LanguageList,[switch]$Force)
    if (-not $Force) { throw 'Unexpected interactive prompt' }
    $script:setCalls++
    $script:setLanguages = @($LanguageList | ForEach-Object {
        [pscustomobject]@{LanguageTag=$_.LanguageTag; InputMethodTips=@($_.InputMethodTips)}
    })
}
function Get-WinUserLanguageList {
    if ($script:failure -eq 'user-tip') { return [pscustomobject]@{LanguageTag='en-US'; InputMethodTips=@('0409:00000409')} }
    return $script:setLanguages
}
function Add-WindowsCapability { throw 'Forbidden servicing call reached the pure setup test' }
Save-Environment
$body = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($body.Count -ne 1) { throw 'Expected one production preparation try/finally' }
try { . ([scriptblock]::Create($body[0].Extent.Text)) } catch {}
$saved = Get-Content -LiteralPath ([IO.Path]::Combine($outputPath, 'environment.json')) -Raw | ConvertFrom-Json
@{report=$saved; set_calls=$script:setCalls; capability_reads=$script:capabilityReads;
  binary_reads=$script:binaryReads; temporary_exists=[IO.File]::Exists([IO.Path]::Combine($outputPath,'environment.json.tmp'))} |
  ConvertTo-Json -Depth 12 -Compress
"""
            source = source.replace("__SCRIPT__", str(SCRIPT).replace("'", "''")).replace("__FAILURE__", failure)
            test.write_text(source, encoding="utf-8-sig")
            result = subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                                     "-ExecutionPolicy", "Bypass", "-File", str(test)],
                                    capture_output=True, timeout=12)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
            return json.loads(result.stdout.decode("utf-8-sig"))

    def test_notpresent_is_preserved_and_setting_user_tip_does_not_claim_input_ready(self):
        result = self.simulated()
        report = result["report"]
        self.assertEqual(report["setup_route"], "existing-provider-activation")
        self.assertEqual(report["status"], "prepared-not-verified")
        self.assertFalse(report["real_ime_workflow_verified"])
        self.assertFalse(report["installation"]["attempted"])
        self.assertEqual(report["capability"]["state"], "NotPresent")
        self.assertEqual(report["capability_after"]["state"], "NotPresent")
        self.assertEqual(result["capability_reads"], 2)
        self.assertEqual(result["binary_reads"], 2)
        self.assertEqual(result["set_calls"], 1)
        self.assertTrue(report["machine_tip_registered"] and report["user_tip_listed"])
        self.assertEqual(report["input_profile"], PROFILE)
        self.assertEqual(report["input_languages"], [
            {"LanguageTag": "en-US", "InputMethodTips": ["synthetic-default-tip"]},
            {"LanguageTag": "zh-CN", "InputMethodTips": [PROFILE]},
        ])
        self.assertEqual([phase["name"] for phase in report["phases"]], [
            "read-host-environment", "read-local-capability", "identify-existing-provider",
            "set-fixed-user-language-tip", "verify-user-tip-and-provider-identity", "record-capability-after-activation",
        ])
        for phase in report["phases"]:
            self.assertEqual(phase["state"], "ended")
            self.assertIsNotNone(phase["started_utc"])
            self.assertIsNotNone(phase["ended_utc"])
            self.assertGreaterEqual(phase["elapsed_seconds"], 0)
        self.assertFalse(result["temporary_exists"])

    def test_missing_provider_or_machine_tip_fails_before_user_setting(self):
        for failure in ("binary", "machine-tip"):
            with self.subTest(failure=failure):
                result = self.simulated(failure)
                self.assertEqual(result["set_calls"], 0)
                self.assertEqual(result["report"]["status"], "unavailable")
                self.assertFalse(result["report"]["real_ime_workflow_verified"])
                self.assertEqual(result["report"]["phases"][-1]["state"], "failed")
                self.assertEqual(result["report"]["phases"][-1]["name"], "identify-existing-provider")
                self.assertIsNotNone(result["report"]["error"])

    def test_missing_capability_observation_fails_without_servicing_or_user_changes(self):
        result = self.simulated("capability")
        self.assertEqual(result["set_calls"], 0)
        self.assertEqual(result["report"]["status"], "unavailable")
        self.assertEqual(result["report"]["phases"][-1]["name"], "read-local-capability")
        self.assertEqual(result["report"]["phases"][-1]["state"], "failed")

    def test_lost_user_tip_or_changed_binary_cannot_prepare(self):
        for failure in ("user-tip", "changed-binary"):
            with self.subTest(failure=failure):
                result = self.simulated(failure)
                self.assertEqual(result["set_calls"], 1)
                self.assertEqual(result["report"]["status"], "unavailable")
                self.assertFalse(result["report"]["real_ime_workflow_verified"])
                self.assertEqual(result["report"]["phases"][-1]["name"], "verify-user-tip-and-provider-identity")
                self.assertEqual(result["report"]["phases"][-1]["state"], "failed")

    def test_real_script_refuses_nonhosted_before_creating_output(self):
        environment = dict(os.environ)
        for name in ("GITHUB_ACTIONS", "RUNNER_ENVIRONMENT", "RUNNER_OS", "GITHUB_REPOSITORY", "ImageOS", "ImageVersion"):
            environment.pop(name, None)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "must-not-exist"
            result = subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                                     "-ExecutionPolicy", "Bypass", "-File",
                                     str(SCRIPT), "-Out", str(output)], env=environment,
                                    capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"Refusing language setup outside", result.stderr)
            self.assertFalse(output.exists())

    def test_real_file_hash_matches_independent_python_oracle_and_missing_file_fails(self):
        # Hash the test interpreter executable as a versioned file fixture;
        # never inspect or load a workstation IME or change language settings.
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            test = directory / "file-only.ps1"
            test.write_text(r"""
param([string]$Source, [string]$Fixture)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($Source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Production syntax error' }
$function = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-ExistingProviderBinary'}, $true)
. ([scriptblock]::Create($function.Extent.Text))
$identity = Get-ExistingProviderBinary $Fixture
$missingRejected = $false
try { Get-ExistingProviderBinary ([IO.Path]::Combine($PSScriptRoot, 'missing.exe')) } catch { $missingRejected = $true }
@{identity=$identity; missing_rejected=$missingRejected} | ConvertTo-Json -Compress
""", encoding="utf-8-sig")
            result = subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                                     "-ExecutionPolicy", "Bypass", "-File", str(test),
                                     "-Source", str(SCRIPT), "-Fixture", str(POWERSHELL)],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
            actual = json.loads(result.stdout.decode("utf-8-sig"))
            self.assertEqual(actual["identity"]["sha256"], hashlib.sha256(POWERSHELL.read_bytes()).hexdigest())
            self.assertTrue(actual["identity"]["version"])
            self.assertTrue(actual["missing_rejected"])


if __name__ == "__main__":
    unittest.main()
