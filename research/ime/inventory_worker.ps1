# Fixed read-only operations. The parent assigns this host to its Job before GO.
# Do not add profile/module loading, dynamic parameters, or queries before GO.
param([string]$Operation, [string]$Out)
if ([Console]::ReadLine() -cne 'GO') { exit 90 }
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
    $env:RUNNER_OS -ne 'Windows' -or $env:GITHUB_REPOSITORY -ne 'JavaZeroo/termforge' -or
    $env:ImageOS -notin @('win22', 'win25', 'win25-vs2026') -or
    $env:ImageVersion -notmatch '^\d{8}\.\d+\.\d+$') { exit 91 }
$workspacePath = [IO.Path]::GetFullPath($env:GITHUB_WORKSPACE).TrimEnd('\') + '\'
$outputPath = [IO.Path]::GetFullPath($Out)
if (-not $outputPath.StartsWith($workspacePath, [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Directory]::Exists($outputPath)) { exit 92 }

$progressPath = [IO.Path]::Combine($outputPath, 'progress.json')
$started = [Diagnostics.Stopwatch]::StartNew()
function Save-Progress([string]$Phase, [string]$State, $ErrorInfo = $null) {
    $progress = [ordered]@{phase = $Phase; state = $State; utc = [DateTime]::UtcNow.ToString('o');
        elapsed_seconds = $started.Elapsed.TotalSeconds; error = $ErrorInfo}
    $temporary = $progressPath + '.tmp'
    [IO.File]::WriteAllText($temporary, ($progress | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    if ([IO.File]::Exists($progressPath)) {
        # Windows PowerShell converts $null to an empty string for this .NET
        # string argument; Replace rejects that backup path. Pass a CLR null.
        [IO.File]::Replace($temporary, $progressPath, [NullString]::Value)
    } else { [IO.File]::Move($temporary, $progressPath) }
}

function Get-FileSha256([string]$LiteralPath) {
    # A hosted Windows PowerShell child may not resolve Get-FileHash through
    # its inherited module path. Stream through the CLR without loading a module.
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($LiteralPath)
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $algorithm.Dispose()
    }
}

function Get-QueryContract($Command) {
    $required = @('Online', 'Name', 'LimitAccess', 'LogPath')
    $candidate = [ordered]@{online_parameter_set = $null; online_parameters = @(); missing_parameters = $required}
    foreach ($set in $Command.ParameterSets) {
        $names = @($set.Parameters | ForEach-Object { $_.Name })
        if ('Online' -notin $names) { continue }
        $missing = @($required | Where-Object { $_ -notin $names })
        if ($missing.Count -lt $candidate.missing_parameters.Count) {
            $candidate = [ordered]@{online_parameter_set = $set.Name;
                online_parameters = @($required | Where-Object { $_ -in $names }); missing_parameters = $missing}
        }
        if ($missing.Count -eq 0) { break }
    }
    # Inspect an actual parameter set; never union incompatible sets or rely on
    # a display-name convention from a documentation generator.
    return $candidate
}

$report = [ordered]@{schema = 'termforge-ime-inventory-worker-v1'; operation = $Operation;
    status = 'failed'; data = [ordered]@{}}
$phase = $Operation
try {
    Save-Progress $phase 'started'
    switch ($Operation) {
        'os' {
            $os = Get-CimInstance Win32_OperatingSystem
            $version = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
            $report.data = [ordered]@{caption = $os.Caption; version = $os.Version; build = $os.BuildNumber;
                installation_type = $version.InstallationType; edition = $version.EditionID;
                ubr = $version.UBR; session_id = [Diagnostics.Process]::GetCurrentProcess().SessionId}
        }
        'powershell' {
            $report.data = [ordered]@{version = $PSVersionTable.PSVersion.ToString(); edition = $PSVersionTable.PSEdition;
                process_bitness = [IntPtr]::Size * 8; clr_version = $PSVersionTable.CLRVersion.ToString()}
        }
        { $_ -in @('dism', 'capability') } {
            $phase = 'resolve-dism-command'
            Save-Progress $phase 'started'
            $command = Get-Command Dism\Get-WindowsCapability
            $report.data = Get-QueryContract $command
            $report.data.module_version = $command.Module.Version.ToString()
            Save-Progress $phase 'ended'
            if ($report.data.missing_parameters.Count -ne 0) { throw [NotSupportedException]::new('Required DISM query parameters unavailable') }
            if ($Operation -eq 'capability') {
                $phase = 'query-local-zh-CN-basic'
                Save-Progress $phase 'started'
                $values = @(Dism\Get-WindowsCapability -Online -Name 'Language.Basic~~~zh-CN~0.0.1.0' -LimitAccess `
                    -LogPath ([IO.Path]::Combine($outputPath, 'query.private.log')))
                $report.data.capabilities = @($values | ForEach-Object {
                    if ($_.Name -ne 'Language.Basic~~~zh-CN~0.0.1.0') { throw 'Unexpected capability identity' }
                    [ordered]@{name = $_.Name; state = $_.State.ToString()}
                })
                $report.data.exposed = $values.Count -gt 0
                # Missing/NotPresent is inventory data, not a query failure or readiness.
            }
        }
        'ime-binary' {
            $binary = [IO.Path]::Combine($env:SystemRoot, 'System32\InputMethod\CHS\ChsIME.exe')
            $report.data = [ordered]@{name = 'ChsIME.exe'; present = [IO.File]::Exists($binary); version = $null; sha256 = $null}
            if ($report.data.present) {
                $report.data.version = [Diagnostics.FileVersionInfo]::GetVersionInfo($binary).FileVersion
                $report.data.sha256 = Get-FileSha256 $binary
            }
        }
        'tip' {
            $profile = '0804:{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}{FA550B04-5AD7-411F-A5AC-CA038EC515D7}'
            $tip = 'SOFTWARE\Microsoft\CTF\TIP\{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}\LanguageProfile\0x00000804\{FA550B04-5AD7-411F-A5AC-CA038EC515D7}'
            $report.data = [ordered]@{input_profile = $profile;
                machine_registered = (Test-Path -LiteralPath ('HKLM:\' + $tip));
                user_registered = (Test-Path -LiteralPath ('HKCU:\' + $tip)); user_listed = $false;
                relevant_languages = @()}
            $phase = 'read-user-language-list'
            Save-Progress $phase 'started'
            # Enumerate actual language entries even when the cmdlet emits a
            # non-enumerated typed collection as one pipeline object.
            $languages = @(foreach ($language in (Get-WinUserLanguageList)) {
                [pscustomobject]@{LanguageTag = $language.LanguageTag; InputMethodTips = @($language.InputMethodTips)}
            })
            $report.data.relevant_languages = @($languages | Where-Object { $_.LanguageTag -in @('en-US', 'zh-CN', 'zh-Hans-CN') } |
                ForEach-Object { $_.LanguageTag })
            $report.data.user_listed = @($languages | Where-Object { $profile -in $_.InputMethodTips }).Count -gt 0
        }
        default { throw [ArgumentException]::new('Unknown fixed inventory operation') }
    }
    $report.status = 'complete'
    Save-Progress $phase 'ended'
} catch {
    $report.status = 'failed'
    $failure = [ordered]@{kind = $_.Exception.GetType().Name; hresult = $_.Exception.HResult}
    $report.data.error = $failure
    Save-Progress $phase 'failed' $failure
}
$report | ConvertTo-Json -Depth 8 -Compress
# #40: the report is written but the parent may never receive it when the pipe
# stalls, so record that the final serialisation completed and was flushed.
$stdout = [Console]::OpenStandardOutput()
$bytes = [Text.Encoding]::UTF8.GetBytes('')
$stdout.Write($bytes, 0, 0)
$stdout.Flush()
Save-Progress 'report' 'emitted'
if ($report.status -ne 'complete') { exit 1 }
