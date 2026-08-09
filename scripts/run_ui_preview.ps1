[CmdletBinding()]
param(
    [string]$StylePath = "ui/local_override.qss"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exePath = Join-Path $repoRoot "build-ucrt64\均程代课管理.exe"

if (-not (Test-Path $exePath)) {
    throw "Preview executable not found: $exePath`nRun .\scripts\build_ucrt64.ps1 first."
}

Push-Location $repoRoot
try {
    & $exePath --ui-preview --style $StylePath
} finally {
    Pop-Location
}
