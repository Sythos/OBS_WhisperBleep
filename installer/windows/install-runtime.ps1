# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

[CmdletBinding()]
param(
    [string]$InstallRoot = "$env:ProgramData\Sythos\OBS-WhisperBleep\runtime-venv",
    [switch]$NonInteractive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requirementsPath = Join-Path $PSScriptRoot 'requirements-cpu.txt'
if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) {
    throw "The packaged requirements file is missing: $requirementsPath"
}

function Get-Python311 {
    $candidates = @()
    $pyLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $pyLauncher) {
        try {
            $selected = (& $pyLauncher.Source -3.11 -c 'import sys; print(sys.executable)').Trim()
            if ($LASTEXITCODE -eq 0 -and $selected -and (Test-Path -LiteralPath $selected)) {
                $candidates += $selected
            }
        } catch {
            # Continue with the regular PATH candidates.
        }
    }
    foreach ($name in @('python3.11.exe', 'python.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            $candidates += $command.Source
        }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        try {
            $version = & $candidate -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")'
            if ($LASTEXITCODE -eq 0 -and $version.Trim() -match '^3\.(1[1-9]|[2-9][0-9])$') {
                return $candidate
            }
        } catch {
            # Try the next candidate.
        }
    }
    return $null
}

$python = Get-Python311
if ($null -eq $python) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw 'Python 3.11 or newer is required. Install Python from python.org or enable WinGet, then retry.'
    }
    $arguments = @(
        'install', '--id', 'Python.Python.3.11', '--exact', '--source', 'winget',
        '--accept-source-agreements', '--accept-package-agreements'
    )
    if ($NonInteractive) {
        $arguments += '--silent'
    } else {
        $arguments += '--interactive'
    }
    & $winget.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "WinGet could not install Python 3.11 (exit code $LASTEXITCODE)."
    }
    $python = Get-Python311
}
if ($null -eq $python) {
    throw 'Python was installed but could not be located. Restart the installer or sign out and in, then retry.'
}

New-Item -ItemType Directory -Path (Split-Path -Parent $InstallRoot) -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'Scripts\python.exe'))) {
    & $python -m venv $InstallRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Python could not create the virtual environment at $InstallRoot."
    }
}

$venvPython = Join-Path $InstallRoot 'Scripts\python.exe'
& $venvPython -m pip install --disable-pip-version-check --upgrade pip
& $venvPython -m pip install --disable-pip-version-check `
    --index-url 'https://download.pytorch.org/whl/cpu' `
    --extra-index-url 'https://pypi.org/simple' `
    --requirement $requirementsPath
if ($LASTEXITCODE -ne 0) {
    throw 'Python dependency installation failed. Check network access and retry the installer.'
}

Set-Content -LiteralPath (Join-Path $InstallRoot 'python-path.txt') -Value $venvPython -Encoding ascii
[Environment]::SetEnvironmentVariable('OBS_WHISPERBLEEP_PYTHON', $venvPython, 'Machine')
Write-Output "Installed the CPU Whisper runtime at $InstallRoot"
Write-Output 'Restart OBS Studio before using the plugin so it can see the installed runtime.'
