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
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$report = [ordered]@{
    schema = 'termforge-hosted-ime-environment-v1'
    status = 'preparing'
    image_os = $env:ImageOS
    image_version = $env:ImageVersion
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber)
    session_id = (Get-Process -Id $PID).SessionId
    capability = $null
    capability_after = $null
    installation = $null
    input_profile = '0804:{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}{FA550B04-5AD7-411F-A5AC-CA038EC515D7}'
    input_languages = $null
    ime_binary = $null
    error = $null
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'environment.json') -Encoding utf8
try {
    # Online servicing availability is a measured platform condition, not an
    # assumption that the hosted Server image includes a desktop-client FOD.
    $capability = Get-WindowsCapability -Online -Name 'Language.Basic~~~zh-CN~0.0.1.0'
    if ($null -eq $capability -or [string]::IsNullOrWhiteSpace($capability.Name)) {
        throw 'This image does not expose the zh-CN Basic capability.'
    }
    $report.capability = $capability | Select-Object Name, State
    if ($capability.State -ne 'Installed') {
        $installed = Add-WindowsCapability -Online -Name $capability.Name -LogPath (Join-Path $outputPath 'language-dism.log')
        $report.installation = $installed | Select-Object Online, RestartNeeded
        if ($installed.RestartNeeded) { throw 'Language capability requires reboot; this job cannot claim IME readiness.' }
    }
    $report.capability_after = Get-WindowsCapability -Online -Name $capability.Name | Select-Object Name, State
    if ($report.capability_after.State -ne 'Installed') { throw 'Language capability is not installed after servicing.' }
    $languages = New-WinUserLanguageList -Language 'en-US'
    $languages.Add('zh-CN')
    $languages[1].InputMethodTips.Clear()
    $languages[1].InputMethodTips.Add($report.input_profile)
    Set-WinUserLanguageList -LanguageList $languages -Force
    $report.input_languages = @(Get-WinUserLanguageList | Select-Object LanguageTag, InputMethodTips)
    $imePath = Join-Path $env:SystemRoot 'System32\InputMethod\CHS\ChsIME.exe'
    if (-not (Test-Path -LiteralPath $imePath -PathType Leaf)) { throw 'Microsoft Pinyin binary is missing after setup.' }
    $file = Get-Item -LiteralPath $imePath
    $report.ime_binary = [ordered]@{name = $file.Name; version = $file.VersionInfo.FileVersion; sha256 = (Get-FileHash -LiteralPath $imePath -Algorithm SHA256).Hash}
    $report.status = 'prepared-not-verified'
} catch {
    $report.status = 'unavailable'
    $report.error = $_.Exception.Message
    throw
} finally {
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'environment.json') -Encoding utf8
}
