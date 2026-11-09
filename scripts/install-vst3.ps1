# Copies the built granulr.vst3 bundle into the system VST3 folder.
# Usage (from the repository root, in an elevated PowerShell if needed):
#   .\scripts\install-vst3.ps1 [-BuildDir build] [-InstallDir "C:\Program Files\Common Files\VST3"]
param(
    [string]$BuildDir = "build",
    [string]$InstallDir = "C:\Program Files\Common Files\VST3"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$bundle = Join-Path $repoRoot "$BuildDir\granulr_artefacts\Release\VST3\granulr.vst3"

if (-not (Test-Path $bundle)) {
    Write-Host "Bundle not found: $bundle"
    Write-Host "Build the plugin first: cmake --build $BuildDir --config Release --target granulr_VST3"
    exit 1
}

$target = Join-Path $InstallDir "granulr.vst3"
try {
    if (-not (Test-Path $InstallDir)) {
        New-Item -ItemType Directory -Path $InstallDir | Out-Null
    }
    # Only this project's bundle is replaced. Nothing else in the folder is touched.
    if (Test-Path $target) {
        Remove-Item -Recurse -Force $target
    }
    Copy-Item -Recurse -Force $bundle $target
    Write-Host "Installed: $target"
}
catch [System.UnauthorizedAccessException] {
    Write-Host "Access denied. Run this script from a PowerShell window opened as administrator."
    exit 2
}
catch [System.IO.IOException] {
    Write-Host "Could not replace the plugin: $($_.Exception.Message)"
    Write-Host "If a DAW has granulr loaded, close it and run the script again."
    exit 3
}
