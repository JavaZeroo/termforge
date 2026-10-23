# This changes language settings only in a disposable GitHub-hosted test VM.
# Never run by exporting fake CI variables on a developer workstation.
param([Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
    $env:RUNNER_OS -ne 'Windows' -or $env:GITHUB_REPOSITORY -ne 'JavaZeroo/termforge' -or
    $env:ImageOS -notin @('win22', 'win25', 'win25-vs2026') -or [string]::IsNullOrWhiteSpace($env:ImageVersion)) {
    throw "Refusing language setup outside the designated disposable GitHub-hosted Windows job. repo=$env:GITHUB_REPOSITORY runner=$env:RUNNER_ENVIRONMENT os=$env:RUNNER_OS image=$env:ImageOS version=$env:ImageVersion"
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Hosted IME setup requires an administrator token.'
}
$workspacePath = [IO.Path]::GetFullPath($env:GITHUB_WORKSPACE).TrimEnd('\') + '\'
$outputPath = [IO.Path]::GetFullPath($Out)
if (-not $outputPath.StartsWith($workspacePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence output must stay inside the checked-out workspace.'
}
if (Test-Path -LiteralPath $outputPath) { throw 'Evidence output already exists; choose a new directory.' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$started = [Diagnostics.Stopwatch]::StartNew()
$report = [ordered]@{
    schema = 'termforge-hosted-ime-environment-v1'
    status = 'preparing'
    setup_route = 'existing-provider-activation'
    real_ime_workflow_verified = $false
    phases = [Collections.Generic.List[object]]::new()
    image_os = $env:ImageOS
    image_version = $env:ImageVersion
    os = $null
    session_id = [Diagnostics.Process]::GetCurrentProcess().SessionId
    capability = $null
    capability_after = $null
    installation = [ordered]@{attempted = $false; reason = 'existing-provider-only; no capability servicing'}
    input_profile = '0804:{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}{FA550B04-5AD7-411F-A5AC-CA038EC515D7}'
    input_languages = $null
    machine_tip_registered = $false
    user_tip_listed = $false
    ime_binary = $null
    ime_binary_after = $null
    error = $null
}
function Save-Environment {
    $path = [IO.Path]::Combine($outputPath, 'environment.json')
    $temporary = $path + '.tmp'
    [IO.File]::WriteAllText($temporary, ($report | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    if ([IO.File]::Exists($path)) { [IO.File]::Replace($temporary, $path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $path) }
}
function Start-SetupPhase([string]$Name) {
    $report.phases.Add([ordered]@{name = $Name; state = 'started';
        started_utc = [DateTime]::UtcNow.ToString('o'); started_seconds = $started.Elapsed.TotalSeconds;
        ended_utc = $null; elapsed_seconds = $null; error = $null})
    Save-Environment
}
function Complete-SetupPhase {
    $phase = $report.phases[$report.phases.Count - 1]
    $phase.state = 'ended'; $phase.ended_utc = [DateTime]::UtcNow.ToString('o')
    $phase.elapsed_seconds = $started.Elapsed.TotalSeconds - $phase.started_seconds
    Save-Environment
}
function Get-ExistingProviderBinary([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { throw 'Existing Microsoft Pinyin binary is missing; this route does not install it.' }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        $hash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $algorithm.Dispose()
    }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path).FileVersion
    if ([string]::IsNullOrWhiteSpace($version) -or $hash -notmatch '^[0-9a-f]{64}$') {
        throw 'Existing Microsoft Pinyin binary identity could not be recorded.'
    }
    return [ordered]@{name = 'ChsIME.exe'; version = $version; sha256 = $hash}
}
function Get-LocalBasicCapability {
    # Read only and no online source access. NotPresent remains an observation;
    # an existing registered provider is tested by the separate real IME probe.
    $values = @(Get-WindowsCapability -Online -Name 'Language.Basic~~~zh-CN~0.0.1.0' -LimitAccess `
        -LogPath ([IO.Path]::Combine($outputPath, 'capability.private.log')))
    if ($values.Count -ne 1 -or $values[0].Name -ne 'Language.Basic~~~zh-CN~0.0.1.0') {
        throw 'This image does not expose exactly the fixed zh-CN Basic capability.'
    }
    return [ordered]@{name = $values[0].Name; state = $values[0].State.ToString()}
}
function Invoke-ExistingProviderSetup {
    Start-SetupPhase 'read-host-environment'
    $report.os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber
    Complete-SetupPhase

    Start-SetupPhase 'read-local-capability'
    $report.capability = Get-LocalBasicCapability
    Complete-SetupPhase

    Start-SetupPhase 'identify-existing-provider'
    $imePath = [IO.Path]::Combine($env:SystemRoot, 'System32\InputMethod\CHS\ChsIME.exe')
    $report.ime_binary = Get-ExistingProviderBinary $imePath
    $tip = 'HKLM:\SOFTWARE\Microsoft\CTF\TIP\{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}\LanguageProfile\0x00000804\{FA550B04-5AD7-411F-A5AC-CA038EC515D7}'
    $report.machine_tip_registered = Test-Path -LiteralPath $tip
    if (-not $report.machine_tip_registered) { throw 'Fixed Microsoft Pinyin machine TIP is not registered.' }
    Complete-SetupPhase

    Start-SetupPhase 'set-fixed-user-language-tip'
    # This changes the disposable account's language list, not the system UI
    # language or locale. A listed TIP does not certify TSF activation/input.
    $languages = New-WinUserLanguageList -Language 'en-US'
    $chinese = New-WinUserLanguageList -Language 'zh-CN'
    $chinese[0].InputMethodTips.Clear()
    $chinese[0].InputMethodTips.Add($report.input_profile)
    $languages.Add($chinese[0])
    Set-WinUserLanguageList -LanguageList $languages -Force
    Complete-SetupPhase

    Start-SetupPhase 'verify-user-tip-and-provider-identity'
    # The cmdlet can emit its typed collection as one pipeline object. Select-
    # Object on that wrapper produces null fields; foreach enumerates the
    # collection before projecting each actual language value.
    $report.input_languages = @(foreach ($language in (Get-WinUserLanguageList)) {
        [pscustomobject]@{LanguageTag = $language.LanguageTag; InputMethodTips = @($language.InputMethodTips)}
    })
    $matching = @($report.input_languages | Where-Object { $_.LanguageTag -eq 'zh-CN' -and
        $report.input_profile -in $_.InputMethodTips })
    $report.user_tip_listed = $matching.Count -eq 1
    if (-not $report.user_tip_listed) { throw 'Fixed Microsoft Pinyin profile was not retained in the disposable user language list.' }
    $report.ime_binary_after = Get-ExistingProviderBinary $imePath
    if ($report.ime_binary.sha256 -cne $report.ime_binary_after.sha256) { throw 'Microsoft Pinyin binary changed during user activation.' }
    Complete-SetupPhase

    Start-SetupPhase 'record-capability-after-activation'
    $report.capability_after = Get-LocalBasicCapability
    Complete-SetupPhase
    $report.status = 'prepared-not-verified'
}
Save-Environment
try {
    Invoke-ExistingProviderSetup
} catch {
    $report.status = 'unavailable'
    $report.error = [ordered]@{kind = $_.Exception.GetType().Name; hresult = $_.Exception.HResult}
    if ($report.phases.Count -gt 0) {
        $phase = $report.phases[$report.phases.Count - 1]
        if ($phase.state -eq 'started') {
            $phase.state = 'failed'; $phase.error = $report.error
            $phase.ended_utc = [DateTime]::UtcNow.ToString('o')
            $phase.elapsed_seconds = $started.Elapsed.TotalSeconds - $phase.started_seconds
        }
    }
    throw
} finally {
    Save-Environment
}
