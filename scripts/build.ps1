# !/usr/bin/env powershell
#
# Usage:
#   .\scripts\build.ps1 [build-name] [-VcpkgRoot <path>] [-VlcSdkDir <path>] [-Clean]
#
# Examples:
#   .\scripts\build.ps1 cmake-release
#   .\scripts\build.ps1 cmake-debug
#   .\scripts\build.ps1 cmake-release-static
#   .\scripts\build.ps1 cmake-release-x86
#

[CmdletBinding()]
param (
    [Parameter(Position=0)]
    [string]$BuildName = "cmake-release",

    [Parameter()]
    [string]$VcpkgRoot = "",

    [Parameter()]
    [string]$VlcSdkDir = "",

    [Parameter()]
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
Set-Location $ProjectRoot

# Detect target architecture, configuration, and vcpkg triplet
$Config = "Release"
$Arch = "x64"
$Triplet = "x64-windows"

switch ($BuildName) {
    "cmake-release" {
        $Config = "Release"
        $Arch = "x64"
        $Triplet = "x64-windows"
    }
    "cmake-debug" {
        $Config = "Debug"
        $Arch = "x64"
        $Triplet = "x64-windows"
    }
    "cmake-release-static" {
        $Config = "Release"
        $Arch = "x64"
        $Triplet = "x64-windows-static"
    }
    "cmake-debug-static" {
        $Config = "Debug"
        $Arch = "x64"
        $Triplet = "x64-windows-static"
    }
    "cmake-release-x86" {
        $Config = "Release"
        $Arch = "Win32"
        $Triplet = "x86-windows"
    }
    "cmake-debug-x86" {
        $Config = "Debug"
        $Arch = "Win32"
        $Triplet = "x86-windows"
    }
    "cmake-release-arm64" {
        $Config = "Release"
        $Arch = "ARM64"
        $Triplet = "arm64-windows"
    }
    default {
        Write-Host -ForegroundColor Yellow "Unknown build name '$BuildName', defaulting to Release x64 (x64-windows)"
        $Config = "Release"
        $Arch = "x64"
        $Triplet = "x64-windows"
    }
}

Write-Host -ForegroundColor Cyan "=========================================================="
Write-Host -ForegroundColor Cyan " vlc-subtitle Windows Build: $BuildName"
Write-Host -ForegroundColor Cyan " Architecture: $Arch | Config: $Config | Triplet: $Triplet"
Write-Host -ForegroundColor Cyan "=========================================================="

# 1. Ensure submodules are checked out
if (-not (Test-Path "runtime/whisper.cpp/CMakeLists.txt")) {
    Write-Host -ForegroundColor Yellow "Initializing git submodules..."
    git submodule update --init --recursive
}

# 2. Locate or bootstrap vcpkg
if (-not $VcpkgRoot) {
    if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        $VcpkgRoot = $env:VCPKG_ROOT
    } elseif (Test-Path "$HOME/vcpkg") {
        $VcpkgRoot = "$HOME/vcpkg"
    } elseif (Test-Path "$ProjectRoot/vcpkg") {
        $VcpkgRoot = "$ProjectRoot/vcpkg"
    } else {
        Write-Host -ForegroundColor Yellow "Cloning vcpkg to $HOME/vcpkg..."
        git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
        $VcpkgRoot = "$HOME/vcpkg"
    }
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $VcpkgExe)) {
    Write-Host -ForegroundColor Yellow "Bootstrapping vcpkg..."
    & "$VcpkgRoot/bootstrap-vcpkg.bat" -disableMetrics
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path $ToolchainFile)) {
    Write-Error "vcpkg toolchain file not found at: $ToolchainFile"
    Exit 1
}

$BuildDir = "cmake-out/$Triplet-$Config"
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host -ForegroundColor Yellow "Cleaning build directory: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# 3. Configure CMake
$CmakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", $Arch,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
    "-DVCPKG_TARGET_TRIPLET=$Triplet"
)

if ($VlcSdkDir) {
    $CmakeArgs += "-DVLC_SDK_DIR=$VlcSdkDir"
}

Write-Host -ForegroundColor Green "`n$(Get-Date -Format o) Configuring CMake..."
& cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed"
    Exit $LASTEXITCODE
}

# 4. Build Plugin Target
Write-Host -ForegroundColor Green "`n$(Get-Date -Format o) Building suboffline_plugin ($Config)..."
& cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    Exit $LASTEXITCODE
}

# 5. Output summary
Write-Host -ForegroundColor Green "`n=========================================================="
Write-Host -ForegroundColor Green " Build succeeded!"
Write-Host -ForegroundColor Green " Output plugin staged in: $ProjectRoot/$BuildDir/plugin/"
Write-Host -ForegroundColor Green "=========================================================="
Get-ChildItem -Path "$BuildDir/plugin" -Filter "*.dll" -ErrorAction SilentlyContinue | `
    Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize

Exit 0
