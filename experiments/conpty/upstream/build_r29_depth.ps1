param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Vcpkg,
    [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This entry requires a disposable GitHub-hosted Windows runner.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$Source = (Resolve-Path -LiteralPath $Source).Path
$Vcpkg = (Resolve-Path -LiteralPath $Vcpkg).Path
$Out = [IO.Path]::GetFullPath($Out, (Get-Location).Path)
$workspacePrefix = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path.TrimEnd('\') + '\'
foreach ($path in @($Source, $Vcpkg, $Out)) {
    if (-not $path.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Source, dependencies and output must be children of this disposable workspace.'
    }
}
if (Test-Path -LiteralPath $Out) { throw 'A fresh output directory is required.' }
New-Item -ItemType Directory -Path "$Out/public", "$Out/_private", "$Out/cache" | Out-Null
$state = [ordered]@{
    schema = 'termforge-r29-conpty-depth-build-v1'
    purpose = 'isolated same-source ConPTY depth experiment'
    status = 'running'
    termforge_source = (& git -C $repo rev-parse HEAD)
    upstream_source = 'dc8ae365847d41f62d1ebca93815bb00959bc12f'
    vcpkg_source = '927f62e4b8838bd7e441e9c45103a16ffd75007e'
    image_os = $env:ImageOS
    image_version = $env:ImageVersion
    workflow_run = $env:GITHUB_RUN_ID
    workflow_attempt = $env:GITHUB_RUN_ATTEMPT
    configuration = 'Release/x64/v145/WindowsSDK10.0.26100.0'
    stages = @()
}
$currentStage = 'source-identity'
function Save-State {
    $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$Out/public/summary.json" -Encoding utf8
}
function Digest([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Invoke-Native([string]$Name, [string]$Executable, [string[]]$Arguments, [switch]$AllowFailure) {
    $script:currentStage = $Name
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Set-Content -LiteralPath "$Out/_private/$Name.log" -Value '' -NoNewline -Encoding utf8
    & $Executable @Arguments 2>&1 | Tee-Object -FilePath "$Out/_private/$Name.log" | Out-Host
    $code = $LASTEXITCODE
    $state.stages += [ordered]@{name=$Name; exit_code=$code; elapsed_seconds=$clock.Elapsed.TotalSeconds; log_sha256=(Digest "$Out/_private/$Name.log")}
    Save-State
    if ($code -ne 0 -and -not $AllowFailure) { throw "Stage $Name failed with exit code $code." }
    return $code
}
function Build-Project([string]$Name, [string]$Project) {
    [void](Invoke-Native $Name 'msbuild.exe' @($Project, '/nologo', '/restore', '/m:1', '/nr:false', '/v:minimal',
        '/p:Configuration=Release', '/p:Platform=x64', '/p:PlatformToolset=v145',
        '/p:WindowsTargetPlatformVersion=10.0.26100.0', '/p:CL_MPCount=1', '/p:UseMultiToolTask=false',
        "/p:VCToolsVersion=$env:VCToolsVersion", "/p:SolutionDir=$Source\", "/p:VcpkgRoot=$Vcpkg\"))
}
function Build-HostPair([string]$Prefix) {
    Build-Project "$Prefix-propsheet-build" 'src/propsheet/propsheet.vcxproj'
    Build-Project "$Prefix-host-build" 'src/host/exe/Host.EXE.vcxproj'
    Build-Project "$Prefix-dll-build" 'src/winconpty/dll/winconptydll.vcxproj'
}
function Copy-Pair([string]$Directory) {
    New-Item -ItemType Directory -Path "$Out/$Directory" | Out-Null
    $files = [ordered]@{}
    foreach ($name in @('OpenConsole.exe', 'conpty.dll')) {
        $binary = "$Source/bin/x64/Release/$name"
        $files[$name] = [ordered]@{sha256=(Digest $binary); bytes=(Get-Item -LiteralPath $binary).Length}
        Copy-Item -LiteralPath $binary -Destination "$Out/$Directory/$name"
        if ((Digest "$Out/$Directory/$name") -ne $files[$name].sha256) { throw 'Copied pair identity differs.' }
    }
    return $files
}

try {
    Save-State
    if ((& git -C $Source rev-parse HEAD) -ne $state.upstream_source) { throw 'Wrong Terminal source.' }
    if ((& git -C $Vcpkg rev-parse HEAD) -ne $state.vcpkg_source) { throw 'Wrong vcpkg source.' }
    $state.vcpkg_shallow = (& git -C $Vcpkg rev-parse --is-shallow-repository)
    if ($LASTEXITCODE -ne 0 -or $state.vcpkg_shallow -ne 'false') { throw 'Pinned port versions require the full vcpkg Git history.' }
    if (@(& git -C $Source status --porcelain).Count -ne 0) { throw 'Upstream checkout is dirty.' }
    $sourceHashes = [ordered]@{
        'src/host/screenInfo.cpp' = 'fced7e15d63ce8e7cf791d09fd1ba57a46a7bf65430e8770a372340650b483b0'
        'src/host/getset.cpp' = 'aab0a2d8c6b4f50e3ab37d6454797a91850f1b0e325e289ea378210eb6012187'
    }
    foreach ($file in $sourceHashes.Keys) {
        if ((Digest (Join-Path $Source $file)) -ne $sourceHashes[$file]) { throw "Source hash differs: $file" }
    }
    $state.source_hashes = $sourceHashes
    $patch = "$PSScriptRoot/r29-native-depth.patch"
    $state.patch_sha256 = Digest $patch
    # Both pinned upstream files use CRLF on disk.
    $applicablePatch = "$Out/_private/r29-native-depth.patch"
    $patchText = [IO.File]::ReadAllText($patch).Replace("`r`n", "`n").Replace("`n", "`r`n")
    [IO.File]::WriteAllText($applicablePatch, $patchText)
    $state.applied_patch_sha256 = Digest $applicablePatch
    $state.source_hashes_verified = $true
    $triplet = Get-Content -Raw -LiteralPath "$Source/dep/vcpkg-overlay-triplets/x64-windows-static.cmake"
    if ($triplet -notmatch 'set\(VCPKG_PLATFORM_TOOLSET v145\)') { throw 'Unexpected upstream toolset requirement.' }

    $currentStage = 'toolchain'
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'The pinned triplet requires an installed v145 toolchain.' }
    Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -winsdk=10.0.26100.0'
    if ($env:VisualStudioVersion -notlike '18.*') { throw 'Expected the v145 Visual Studio environment.' }
    $state.tools = [ordered]@{visual_studio=$env:VisualStudioVersion; msvc=$env:VCToolsVersion;
        sdk=$env:WindowsSDKVersion; compiler_sha256=(Digest (Get-Command cl.exe).Source);
        msbuild_sha256=(Digest (Get-Command msbuild.exe).Source)}
    $env:VCPKG_ROOT = $Vcpkg
    $env:VCPKG_DISABLE_METRICS = '1'
    $env:VCPKG_MAX_CONCURRENCY = '1'
    $env:VCPKG_DOWNLOADS = "$Out/cache/vcpkg-downloads"
    $env:VCPKG_DEFAULT_BINARY_CACHE = "$Out/cache/vcpkg-binary"
    $env:VCPKG_BINARY_SOURCES = "clear;files,$env:VCPKG_DEFAULT_BINARY_CACHE,readwrite"
    $env:NUGET_PACKAGES = "$Out/cache/nuget-global"
    $env:NUGET_HTTP_CACHE_PATH = "$Out/cache/nuget-http"
    $env:NUGET_PLUGINS_CACHE_PATH = "$Out/cache/nuget-plugins"
    foreach ($folder in @($env:VCPKG_DOWNLOADS, $env:VCPKG_DEFAULT_BINARY_CACHE, $env:NUGET_PACKAGES,
                         $env:NUGET_HTTP_CACHE_PATH, $env:NUGET_PLUGINS_CACHE_PATH)) {
        New-Item -ItemType Directory -Path $folder | Out-Null
    }
    Set-Location -LiteralPath $Source
    [void](Invoke-Native 'vcpkg-tool-download' 'curl.exe' @('--fail', '--location', '--silent', '--show-error',
        '--connect-timeout', '10', '--max-time', '180', '--retry', '0', '--output', "$Vcpkg/vcpkg.exe",
        'https://github.com/microsoft/vcpkg-tool/releases/download/2026-04-08/vcpkg.exe'))
    $state.vcpkg_exe_sha256 = Digest "$Vcpkg/vcpkg.exe"
    if ($state.vcpkg_exe_sha256 -ne 'f1edbf3a39de350e2bb065214fdc057111aa87a2e2ed9a7dcb8ddc86e17751b9') { throw 'vcpkg release digest differs.' }
    [void](Invoke-Native 'vcpkg-version' "$Vcpkg/vcpkg.exe" @('version', '--disable-metrics'))
    [void](Invoke-Native 'nuget-restore' "$Source/dep/nuget/nuget.exe" @('restore', "$Source/dep/nuget/packages.config",
        '-ConfigFile', "$Source/NuGet.Config", '-PackagesDirectory', "$Source/packages", '-DisableParallelProcessing', '-NonInteractive'))

    [void](Invoke-Native 'candidate-patch-check' 'git' @('apply', '--unidiff-zero', '--check', $applicablePatch))

    # Single-project builds do not inherit OpenConsole.slnx BuildDependency edges.
    Build-Project 'baseline-proxy-build' 'src/host/proxy/Host.Proxy.vcxproj'
    Build-Project 'baseline-types-build' 'src/types/lib/types.vcxproj'
    Build-Project 'baseline-buffer-build' 'src/buffer/out/lib/bufferout.vcxproj'
    Build-Project 'baseline-render-base-build' 'src/renderer/base/lib/base.vcxproj'
    Build-Project 'baseline-render-gdi-build' 'src/renderer/gdi/lib/gdi.vcxproj'
    Build-HostPair 'baseline'
    $currentStage = 'baseline-pair-identity'
    $state.baseline_files = Copy-Pair 'baseline-pair'

    [void](Invoke-Native 'candidate-patch' 'git' @('apply', '--unidiff-zero', $applicablePatch))
    $expectedFiles = @('src/host/getset.cpp', 'src/host/screenInfo.cpp')
    if ((@(& git diff --name-only) -join ',') -ne ($expectedFiles -join ',')) { throw 'Unexpected patched source files.' }
    $state.candidate_source_hashes = [ordered]@{}
    foreach ($file in $expectedFiles) { $state.candidate_source_hashes[$file] = Digest (Join-Path $Source $file) }
    Build-HostPair 'candidate'
    $currentStage = 'candidate-pair-identity'
    $state.candidate_files = Copy-Pair 'candidate-pair'
    if ($state.candidate_files['OpenConsole.exe'].sha256 -eq $state.baseline_files['OpenConsole.exe'].sha256) {
        throw 'Candidate host is unchanged.'
    }
    $state.status = 'built'
    Save-State
    foreach ($directory in @('baseline-pair', 'candidate-pair')) {
        Copy-Item -LiteralPath "$Out/public/summary.json", $patch, "$Source/LICENSE", "$Source/NOTICE.md" -Destination "$Out/$directory"
    }

} catch {
    $state.status = 'failed'
    $state.failed_stage = $currentStage
    throw
} finally {
    Save-State
}