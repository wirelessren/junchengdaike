[CmdletBinding()]
param(
    [string]$BuildDir = "build-ucrt64",
    [string]$BuildType = "Release",
    [string]$Msys2Root = $env:MSYS2_ROOT
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$resolvedMsys2Root = if ($Msys2Root) {
    $Msys2Root
} elseif (Test-Path "C:\msys64") {
    "C:\msys64"
} elseif (Test-Path (Join-Path $env:USERPROFILE "msys64")) {
    Join-Path $env:USERPROFILE "msys64"
} else {
    Join-Path $env:USERPROFILE "tools\msys64\msys64"
}
$msysBin = Join-Path $resolvedMsys2Root "ucrt64\bin"
$exePath = Join-Path $buildPath "SubstituteSmokeCheck.exe"
$outputPath = Join-Path $buildPath "smoke-output"

& (Join-Path $PSScriptRoot "build_ucrt64.ps1") -BuildDir $BuildDir -BuildType $BuildType -Msys2Root $resolvedMsys2Root

if (-not (Test-Path $exePath)) {
    throw "Smoke executable not found: $exePath"
}

$originalPath = $env:PATH
try {
    $env:PATH = "$buildPath;$msysBin;$originalPath"
    & $exePath $repoRoot $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "Smoke validation failed."
    }
} finally {
    $env:PATH = $originalPath
}

Write-Host "Smoke validation output: $outputPath"
