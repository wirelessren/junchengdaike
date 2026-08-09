[CmdletBinding()]
param(
    [string]$BuildDir = "build-ucrt64",
    [string]$BuildType = "Release",
    [string]$PackageDir = "dist\均程代课管理-ucrt64",
    [string]$Msys2Root = $env:MSYS2_ROOT,
    [switch]$IncludeDataFiles
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$packagePath = Join-Path $repoRoot $PackageDir
$zipPath = "$packagePath.zip"
$resolvedMsys2Root = if ($Msys2Root) {
    $Msys2Root
} elseif (Test-Path "C:\msys64") {
    "C:\msys64"
} elseif (Test-Path (Join-Path $env:USERPROFILE "msys64")) {
    Join-Path $env:USERPROFILE "msys64"
} else {
    Join-Path $env:USERPROFILE "tools\msys64\msys64"
}
$bash = Join-Path $resolvedMsys2Root "usr\bin\bash.exe"
$windeployqt = Join-Path $resolvedMsys2Root "ucrt64\bin\windeployqt.exe"
$exeName = "均程代课管理.exe"

& (Join-Path $PSScriptRoot "build_ucrt64.ps1") -BuildDir $BuildDir -BuildType $BuildType -Msys2Root $resolvedMsys2Root

if (-not (Test-Path $bash)) {
    throw "MSYS2 bash not found: $bash"
}
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found: $windeployqt"
}

$buildUnix = (& $bash -lc "cygpath -u '$buildPath'").Trim()
$exePath = Join-Path $buildPath $exeName
$exeUnix = (& $bash -lc "cygpath -u '$exePath'").Trim()

& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw $exePath
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed."
}

try {
    $runtimeDeployScript = Join-Path $env:TEMP "package_ucrt64_runtime.sh"
    $runtimeDeployContent = @"
set -euo pipefail
export PATH=/usr/bin:/ucrt64/bin:`$PATH
{
  ldd "$exeUnix"
  ldd "$buildUnix/platforms/qwindows.dll"
  ldd "$buildUnix/sqldrivers/qsqlite.dll"
} | awk '/=> \/ucrt64\/bin\// {print `$3}' | sort -u | while read -r dll; do
  cp -u "`$dll" "$buildUnix/"
done
"@
    [System.IO.File]::WriteAllText(
        $runtimeDeployScript,
        $runtimeDeployContent,
        [System.Text.UTF8Encoding]::new($false))

    & $bash $runtimeDeployScript
    if ($LASTEXITCODE -ne 0) {
        throw "Runtime deployment failed."
    }
} finally {
    if ($runtimeDeployScript -and (Test-Path $runtimeDeployScript)) {
        Remove-Item -LiteralPath $runtimeDeployScript -Force
    }
}

if (Test-Path $packagePath) {
    Remove-Item -LiteralPath $packagePath -Recurse -Force
}
New-Item -ItemType Directory -Path $packagePath | Out-Null

$runtimeFiles = Get-ChildItem -Path $buildPath -File | Where-Object {
    $_.Name -eq $exeName -or
    $_.Name -like "Qt6*.dll" -or
    $_.Name -like "lib*.dll" -or
    $_.Name -eq "zlib1.dll"
}
foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath $file.FullName -Destination $packagePath -Force
}

$runtimeDirs = @(".qt", "generic", "imageformats", "networkinformation", "platforms", "sqldrivers", "styles", "tls")
foreach ($dirName in $runtimeDirs) {
    $sourceDir = Join-Path $buildPath $dirName
    if (Test-Path $sourceDir) {
        Copy-Item -LiteralPath $sourceDir -Destination $packagePath -Recurse -Force
    }
}

$documentationFiles = @("README.md", "LICENSE")
foreach ($fileName in $documentationFiles) {
    $sourceFile = Join-Path $repoRoot $fileName
    if (Test-Path $sourceFile) {
        Copy-Item -LiteralPath $sourceFile -Destination $packagePath -Force
    }
}

if ($IncludeDataFiles) {
    $dataFiles = @("教师课表统计.xlsx", "教师源课表.xlsx", "通知单模版.xlsx", "教师课表.db")
    foreach ($fileName in $dataFiles) {
        $sourceFile = Join-Path $repoRoot $fileName
        if (Test-Path $sourceFile) {
            Copy-Item -LiteralPath $sourceFile -Destination $packagePath -Force
        }
    }
}

$iconSource = Join-Path $repoRoot "resources\app_icon.ico"
if (Test-Path $iconSource) {
    Copy-Item -LiteralPath $iconSource -Destination (Join-Path $packagePath "均程代课管理.ico") -Force
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $packagePath "*") -DestinationPath $zipPath

Write-Host "Package directory: $packagePath"
Write-Host "Package archive:   $zipPath"
