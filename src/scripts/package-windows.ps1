[CmdletBinding()]
param(
    [string]$BuildDir = "src/build",
    [string]$Configuration = "Release",
    [string]$PackageVersion = "",
    [switch]$SkipBuild,
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "../.."))
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if (-not $SkipBuild) {
    & cmake --build $BuildDir --parallel --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE"
    }
}

$ExecutableCandidates = @(
    (Join-Path $BuildDir "$Configuration/Harbor.exe"),
    (Join-Path $BuildDir "Harbor.exe")
)
$Executable = $ExecutableCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Executable) {
    throw "Harbor.exe was not found under $BuildDir"
}

$CMakeText = Get-Content (Join-Path $RepoRoot "src/CMakeLists.txt") -Raw
$VersionMatch = [regex]::Match(
    $CMakeText,
    'project\s*\(\s*fim-config-tool\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $VersionMatch.Success) {
    throw "Unable to read the project version from src/CMakeLists.txt"
}
$NumericVersion = $VersionMatch.Groups[1].Value
$FileVersion = "$NumericVersion.0"

if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    $GitVersion = (& git -C $RepoRoot describe --tags --always 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($GitVersion)) {
        $PackageVersion = $NumericVersion
    } else {
        $PackageVersion = $GitVersion.Trim() -replace '^v', ''
    }
}
$PackageVersion = $PackageVersion -replace '[^0-9A-Za-z._-]', '-'

$DistDir = Join-Path $BuildDir "dist"
$StageRoot = Join-Path $BuildDir "package/windows"
$StageDir = Join-Path $StageRoot "Harbor"
if (Test-Path $StageRoot) {
    Remove-Item -Recurse -Force $StageRoot
}
New-Item -ItemType Directory -Force $StageDir | Out-Null
New-Item -ItemType Directory -Force $DistDir | Out-Null
Copy-Item $Executable (Join-Path $StageDir "Harbor.exe")

# --compiler-runtime puts VCRUNTIME140.dll and friends next to Harbor.exe.
# This replaces shipping vc_redist.x64.exe under _prerequisites: that installs
# machine-wide and needs elevation, which the per-user installer does not have,
# so on a clean machine it failed silently and Harbor would not start. An
# app-local CRT is redistributable and keeps the per-user install UAC-free.
$WinDeployQt = Get-Command "windeployqt.exe" -ErrorAction Stop
& $WinDeployQt.Source `
    --release `
    --dir $StageDir `
    --no-translations `
    --compiler-runtime `
    (Join-Path $StageDir "Harbor.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# windeployqt's runtime deployment has varied across Qt versions, so assert
# rather than assume. Without these the app dies at startup on a machine that
# has never had a Visual Studio redistributable installed.
$RequiredRuntime = @("vcruntime140.dll", "msvcp140.dll")
$MissingRuntime = $RequiredRuntime | Where-Object {
    -not (Test-Path (Join-Path $StageDir $_))
}
if ($MissingRuntime) {
    throw @"
windeployqt did not deploy the MSVC runtime: $($MissingRuntime -join ', ').
The package would fail to start on a machine without the Visual C++
redistributable already installed.
"@
}

Copy-Item `
    (Join-Path $RepoRoot "src/resources/dist/README-Windows.txt") `
    (Join-Path $StageDir "README.txt")

$PortableZip = Join-Path $DistDir "Harbor-$PackageVersion-windows-x64-portable.zip"
if (Test-Path $PortableZip) {
    Remove-Item -Force $PortableZip
}
Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $PortableZip
Write-Host "Portable package: $PortableZip"

if ($SkipInstaller) {
    return
}

$MakeNsisCommand = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
if ($MakeNsisCommand) {
    $MakeNsis = $MakeNsisCommand.Source
} else {
    $MakeNsis = Join-Path ${env:ProgramFiles(x86)} "NSIS/makensis.exe"
}
if (-not (Test-Path $MakeNsis)) {
    throw "makensis.exe was not found. Install NSIS or pass -SkipInstaller."
}

$InstallerScript = Join-Path $ScriptDir "harbor-installer.nsi"
$IconPath = Join-Path $RepoRoot "src/resources/icon/Harbor.ico"
& $MakeNsis `
    "/V3" `
    "/WX" `
    "/DAPP_VERSION=$PackageVersion" `
    "/DAPP_FILE_VERSION=$FileVersion" `
    "/DSTAGE_DIR=$StageDir" `
    "/DOUTPUT_DIR=$DistDir" `
    "/DICON_PATH=$IconPath" `
    $InstallerScript
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

$Installer = Join-Path $DistDir "Harbor-$PackageVersion-windows-x64-setup.exe"
if (-not (Test-Path $Installer)) {
    throw "NSIS did not create the expected installer: $Installer"
}
Write-Host "Installer package: $Installer"
