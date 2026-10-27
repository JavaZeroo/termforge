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
    schema = 'termforge-upstream-conpty-build-v1'
    purpose = 'isolated source-build and adapter experiment; not an official ConPTY package'
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
    test_binaries = @()
    adapter_tests_verified = $false
    native_runtime_verified = $false
    full_r2_contract_verified = $false
    product_backend_selected = $false
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
function Test-Adapter([string]$Name, [string]$Expected, [switch]$Full) {
    $testBinary = "$Source/bin/x64/Release/ConAdapter.Unit.Tests.dll"
    $binaryHash = Digest $testBinary
    $state.test_binaries += [ordered]@{stage=$Name; test_dll_sha256=$binaryHash;
        taef_sha256=(Digest "$Source/packages/Microsoft.Taef.10.100.251104001/build/Binaries/x64/te.exe")}
    $testArgs = @("$Source/bin/x64/Release/ConAdapter.Unit.Tests.dll", '/labMode',
        '/coloredConsoleOutput:false', '/unicodeOutput:false', '/console:flushWrites')
    if ($Full) { $testArgs += '/sessionTimeout:0:10:0' }
    else { $testArgs += @('/name:*AlternateScreenMarginsTest', '/sessionTimeout:0:1:0') }
    $code = Invoke-Native $Name "$Source/packages/Microsoft.Taef.10.100.251104001/build/Binaries/x64/te.exe" $testArgs -AllowFailure
    [void](Invoke-Native "$Name-contract" 'python' @("$PSScriptRoot/taef_result.py", "$Out/_private/$Name.log",
        '--exit-code', "$code", '--expected', $Expected, '--out', "$Out/public/$Name.json"))
    if ((Digest $testBinary) -ne $binaryHash) { throw 'Test binary identity changed during execution.' }
}
try {
    Save-State
    if ((& git -C $Source rev-parse HEAD) -ne $state.upstream_source) { throw 'Wrong Terminal source.' }
    if ((& git -C $Vcpkg rev-parse HEAD) -ne $state.vcpkg_source) { throw 'Wrong vcpkg source.' }
    if (@(& git -C $Source status --porcelain).Count -ne 0) { throw 'Upstream checkout is dirty.' }
    $prior = Get-Content -Raw -LiteralPath "$repo/experiments/conpty/results/2026-10-26-native-margin-path/validation.json" | ConvertFrom-Json -AsHashtable
    foreach ($file in $prior.upstream_source_sha256.Keys) {
        if ((Digest (Join-Path $Source $file)) -ne $prior.upstream_source_sha256[$file]) { throw "Source hash differs: $file" }
    }
    $patch = "$PSScriptRoot/dc8-native-margin-candidate.patch"
    $state.patch_sha256 = Digest $patch
    if ($state.patch_sha256 -ne 'dc27377e399bd0217a0b76e16cc89f087c1b781929e4c958ad30c3a475322b81') { throw 'Patch identity differs.' }
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

    $testFile = 'src/terminal/adapter/ut_adapter/adapterTest.cpp'
    [void](Invoke-Native 'baseline-patch-check' 'git' @('apply', '--unidiff-zero', "--include=$testFile", '--check', $patch))
    [void](Invoke-Native 'baseline-test-patch' 'git' @('apply', '--unidiff-zero', "--include=$testFile", $patch))
    if ((@(& git diff --name-only) -join ',') -ne $testFile) { throw 'Baseline changed production source.' }
    Build-Project 'baseline-adapter-build' 'src/terminal/adapter/ut_adapter/Adapter.UnitTests.vcxproj'
    Test-Adapter 'baseline-regression' 'red'

    [void](Invoke-Native 'production-patch-check' 'git' @('apply', '--unidiff-zero', "--exclude=$testFile", '--check', $patch))
    [void](Invoke-Native 'production-patch' 'git' @('apply', '--unidiff-zero', "--exclude=$testFile", $patch))
    $expectedFiles = @('src/terminal/adapter/adaptDispatch.cpp', 'src/terminal/adapter/adaptDispatch.hpp', $testFile)
    if ((@(& git diff --name-only) -join ',') -ne ($expectedFiles -join ',')) { throw 'Unexpected patched source files.' }
    Build-Project 'patched-adapter-build' 'src/terminal/adapter/ut_adapter/Adapter.UnitTests.vcxproj'
    Test-Adapter 'patched-regression' 'focused-green'
    Test-Adapter 'patched-adapter' 'full-green' -Full
    $state.adapter_tests_verified = $true
    Build-Project 'patched-host-build' 'src/host/exe/Host.EXE.vcxproj'
    Build-Project 'patched-dll-build' 'src/winconpty/dll/winconptydll.vcxproj'

    $currentStage = 'artifact-identity'
    New-Item -ItemType Directory -Path "$Out/pair" | Out-Null
    $state.files = [ordered]@{}
    foreach ($name in @('OpenConsole.exe', 'conpty.dll')) {
        $binary = "$Source/bin/x64/Release/$name"
        $state.files[$name] = [ordered]@{sha256=(Digest $binary); bytes=(Get-Item -LiteralPath $binary).Length}
        Copy-Item -LiteralPath $binary -Destination "$Out/pair/$name"
    }
    $state.nuget_packages = @()
    foreach ($package in Get-ChildItem -LiteralPath "$Source/packages" -Filter '*.nupkg' -Recurse -File) {
        $state.nuget_packages += [ordered]@{name=$package.Name; sha256=(Digest $package.FullName)}
    }
    $state.vcpkg_status_sha256 = Digest "$Source/obj/x64/vcpkg/vcpkg/status"
    $state.status = 'adapter-verified-runtime-unverified'
    Save-State
    Copy-Item -LiteralPath "$Out/public/summary.json", $patch, "$Source/LICENSE", "$Source/NOTICE.md" -Destination "$Out/pair"
} catch {
    $state.status = 'failed'
    $state.failed_stage = $currentStage
    throw
} finally {
    Save-State
}
