# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$StageDirectory,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'out')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-NsisPath {
    param([Parameter(Mandatory)][string]$Path)

    if ($Path.Contains('"') -or $Path.Contains('$')) {
        throw "NSIS does not support a staging path containing a quote or dollar sign: $Path"
    }
    return $Path.Replace('/', '\')
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$versionPath = Join-Path $projectRoot 'VERSION'
$version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain MAJOR.MINOR.PATCH; found '$version'."
}

$stageRoot = (Resolve-Path -LiteralPath $StageDirectory).Path
$stageFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse)
if ($stageFiles.Count -eq 0) {
    throw "The staging directory contains no files: $stageRoot"
}

$forbiddenExtensions = @(
    '.bin', '.gguf', '.onnx', '.pt', '.pth', '.ckpt', '.pkl', '.pickle',
    '.safetensors', '.pem', '.p12', '.pfx', '.key'
)
$forbiddenFiles = @($stageFiles | Where-Object {
    ($forbiddenExtensions -contains $_.Extension.ToLowerInvariant()) -or
    ($_.FullName -match '[\\/](model-cache|models|weights|__pycache__)[\\/]') -or
    ($_.Name -match '^(\.env|id_rsa|id_ed25519)$|credentials|secret')
})
if ($forbiddenFiles.Count -gt 0) {
    $paths = $forbiddenFiles.FullName -join [Environment]::NewLine
    throw "The staging directory contains forbidden package content:$([Environment]::NewLine)$paths"
}

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$outputFile = Join-Path $outputRoot "OBS-WhisperBleep-$version-Windows-x64-unsigned.exe"
$manifestPath = Join-Path $outputRoot 'uninstall-files.nsh'

$stagePrefix = $stageRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
$uninstallLines = New-Object System.Collections.Generic.List[string]
$directoryPaths = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

foreach ($file in $stageFiles) {
    $relativePath = $file.FullName.Substring($stagePrefix.Length)
    $nsisRelativePath = ConvertTo-NsisPath $relativePath
    $uninstallLines.Add("Delete `"`$INSTDIR\$nsisRelativePath`"")

    $directory = Split-Path -Path $nsisRelativePath -Parent
    while (-not [string]::IsNullOrEmpty($directory)) {
        [void]$directoryPaths.Add($directory)
        $directory = Split-Path -Path $directory -Parent
    }
}

foreach ($directory in @($directoryPaths | Sort-Object { $_.Length } -Descending)) {
    $uninstallLines.Add("RMDir `"`$INSTDIR\$directory`"")
}

[System.IO.File]::WriteAllLines($manifestPath, $uninstallLines, [System.Text.UTF8Encoding]::new($false))

$makensis = Get-Command 'makensis.exe' -ErrorAction SilentlyContinue
if ($null -eq $makensis) {
    $makensis = Get-Command 'makensis' -ErrorAction SilentlyContinue
}
if ($null -eq $makensis) {
    throw 'NSIS was not found. Install NSIS and make makensis available on PATH.'
}

$scriptPath = Join-Path $PSScriptRoot 'OBS-WhisperBleep.nsi'
& $makensis.Source "/DSTAGE_DIR=$(ConvertTo-NsisPath $stageRoot)" "/DOUTPUT_FILE=$(ConvertTo-NsisPath $outputFile)" "/DPRODUCT_VERSION=$version" "/DUNINSTALL_MANIFEST=$(ConvertTo-NsisPath $manifestPath)" $scriptPath
if ($LASTEXITCODE -ne 0) {
    throw "NSIS failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $outputFile -PathType Leaf)) {
    throw "NSIS completed without producing the expected installer: $outputFile"
}

Write-Output "Created unsigned installer: $outputFile"
