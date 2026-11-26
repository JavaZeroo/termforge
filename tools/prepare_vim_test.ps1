# Prepare the test-only Git for Windows binaries. The Qt scenario also checks
# each file identity before copying the binaries and terminfo entry into its isolated workspace.
param([string]$SevenZip = "7z.exe")
$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot "../local/r28-vim"
New-Item -ItemType Directory -Force -Path $root | Out-Null
$archive = Join-Path $root "git246.exe"
$source = "https://github.com/git-for-windows/git/releases/download/v2.46.0.windows.1/PortableGit-2.46.0-64-bit.7z.exe"
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $source -OutFile "$archive.partial"
    Move-Item -LiteralPath "$archive.partial" -Destination $archive -Force
}
if ((Get-FileHash -LiteralPath $archive).Hash -ne "dedae83f4d0851bcbf473c516701e2da6a5d7c574d694d5eceec46d1307132ea") {
    throw "PortableGit 2.46.0 archive identity differs"
}
& $SevenZip x -y "-o$root/fixture" $archive usr/bin/vim.exe usr/bin/msys-2.0.dll usr/bin/msys-ncursesw6.dll usr/bin/msys-intl-8.dll usr/bin/msys-iconv-2.dll usr/share/terminfo/78/xterm-256color
if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed ($LASTEXITCODE)" }
