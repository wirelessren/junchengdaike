[CmdletBinding()]
param(
    [string]$BuildDir = "build-ucrt64",
    [string]$BuildType = "Release",
    [switch]$Clean,
    [string]$Msys2Root = $env:MSYS2_ROOT
)

$ErrorActionPreference = "Stop"

function Resolve-Msys2Root([string]$PreferredRoot) {
    $candidates = @(
        $PreferredRoot,
        "C:\msys64",
        (Join-Path $env:USERPROFILE "msys64"),
        (Join-Path $env:USERPROFILE "tools\msys64\msys64")
    ) | Where-Object { $_ } | Select-Object -Unique

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "usr\bin\bash.exe")) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "MSYS2 not found. Install it or set the MSYS2_ROOT environment variable."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$resolvedMsys2Root = Resolve-Msys2Root $Msys2Root
$bash = Join-Path $resolvedMsys2Root "usr\bin\bash.exe"

if ($Clean -and (Test-Path $buildPath)) {
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}

$repoUnix = (& $bash -lc "cygpath -u '$repoRoot'").Trim()
$buildUnix = (& $bash -lc "cygpath -u '$buildPath'").Trim()

$cmd = @"
export PATH=/ucrt64/bin:`$PATH
cmake -S "$repoUnix" -B "$buildUnix" -G Ninja -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_PREFIX_PATH=/ucrt64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$buildUnix" --parallel
"@

& $bash -lc $cmd
if ($LASTEXITCODE -ne 0) {
    throw "UCRT64 build failed."
}

$legacyExe = Join-Path $buildPath "SubstituteArrangementCpp.exe"
$oldOutputExe = Join-Path $buildPath "走班课表管理.exe"
$currentExe = Join-Path $buildPath "均程代课管理.exe"
if ((Test-Path $legacyExe) -and (Test-Path $currentExe)) {
    Remove-Item -LiteralPath $legacyExe -Force
}
if ((Test-Path $oldOutputExe) -and (Test-Path $currentExe)) {
    Remove-Item -LiteralPath $oldOutputExe -Force
}

Write-Host "Build completed: $buildPath"
