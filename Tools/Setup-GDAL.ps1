<#
.SYNOPSIS
    Installs GDAL (built from source via vcpkg) so GRT's GeoTIFF importer can link
    against it.

.DESCRIPTION
    GRT's GeoTIFF/DEM importer (Plugins/GRT/Source/ThirdParty/GDAL) links
    against GDAL's C API. GDAL is not bundled with Unreal Engine and there is no first-party
    UE5 GDAL plugin, so this script builds it locally with Microsoft's vcpkg -- the same MSVC
    toolchain already required to build the project -- rather than downloading a prebuilt
    binary blob from a third-party mirror.

    Safe to re-run: vcpkg and "vcpkg install" are both idempotent, and this script skips the
    clone/bootstrap steps if they already exist.

    First run takes roughly 30-60+ minutes and a few GB of disk -- it is compiling GDAL and
    its own dependencies (PROJ, libtiff, zlib, sqlite3, and others) from source, not just
    downloading them.

.PARAMETER VcpkgRoot
    Where to install vcpkg itself. Defaults to Tools/vcpkg next to this script, so the whole
    toolchain stays self-contained under the repo (and gitignored -- see .gitignore).

.EXAMPLE
    ./Tools/Setup-GDAL.ps1
#>
param(
    [string]$VcpkgRoot = (Join-Path $PSScriptRoot "vcpkg")
)

$ErrorActionPreference = "Stop"

function Write-Step($Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is required (to clone vcpkg) and was not found on PATH."
}

if (-not (Test-Path $VcpkgRoot)) {
    Write-Step "Cloning vcpkg (Microsoft, https://github.com/microsoft/vcpkg) to $VcpkgRoot"
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
}
else {
    Write-Step "vcpkg already present at $VcpkgRoot, skipping clone"
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Test-Path $VcpkgExe)) {
    Write-Step "Bootstrapping vcpkg"
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg bootstrap failed (exit $LASTEXITCODE)"
    }
}
else {
    Write-Step "vcpkg already bootstrapped, skipping"
}

Write-Step "Installing GDAL for x64-windows (building from source -- this is the long step)"
& $VcpkgExe install gdal:x64-windows
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg install gdal:x64-windows failed (exit $LASTEXITCODE)"
}

$InstalledRoot = Join-Path $VcpkgRoot "installed\x64-windows"
$IncludeDir = Join-Path $InstalledRoot "include"
$LibDir = Join-Path $InstalledRoot "lib"
$BinDir = Join-Path $InstalledRoot "bin"

if (-not (Test-Path (Join-Path $IncludeDir "gdal.h"))) {
    throw "GDAL install appears to have failed: gdal.h not found under $IncludeDir"
}

Write-Step "GDAL installed successfully"
Write-Host "  Headers: $IncludeDir"
Write-Host "  Import libs: $LibDir"
Write-Host "  Runtime DLLs: $BinDir"
Write-Host ""
Write-Host "Plugins/GRT/Source/ThirdParty/GDAL/GDAL.Build.cs reads GDAL from this" -ForegroundColor Cyan
Write-Host "same Tools/vcpkg/installed/x64-windows layout -- nothing else to configure." -ForegroundColor Cyan
