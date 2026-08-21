<#
.SYNOPSIS
Checks or installs the host tools required by the local verification flow.

.DESCRIPTION
The script never installs Python packages or model weights. Without
-InstallTools it is read-only. With -InstallTools it uses WinGet for the
versioned user tools and leaves the Visual Studio C++ workload to its normal
interactive installer.
#>

[CmdletBinding()]
param(
    [switch]$InstallTools
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-Command {
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-WingetInstall {
    param([Parameter(Mandatory = $true)][string]$Id)

    Write-Host "==> Installing $Id"
    & winget.exe install `
        --id $Id `
        --exact `
        --source winget `
        --interactive `
        --accept-source-agreements `
        --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "WinGet could not install $Id (exit code $LASTEXITCODE)."
    }
}

$tools = [ordered]@{
    Python311 = Test-Command "py.exe"
    CMake = Test-Command "cmake.exe"
    Ninja = Test-Command "ninja.exe"
    CxxCompiler = (Test-Command "cl.exe") -or (Test-Command "clang++.exe")
}

Write-Host "Python launcher: $($tools.Python311)"
Write-Host "CMake: $($tools.CMake)"
Write-Host "Ninja: $($tools.Ninja)"
Write-Host "C++ compiler: $($tools.CxxCompiler)"

if ($InstallTools) {
    if (-not (Test-Command "winget.exe")) {
        throw "WinGet is not available. Install App Installer from Microsoft before using -InstallTools."
    }

    if (-not $tools.Python311) {
        Invoke-WingetInstall "Python.Python.3.11"
    }
    if (-not $tools.CMake) {
        Invoke-WingetInstall "Kitware.CMake"
    }
    if (-not $tools.Ninja) {
        Invoke-WingetInstall "Ninja-build.Ninja"
    }
}

if (-not $tools.CxxCompiler) {
    Write-Warning "A C++20 compiler is missing. Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload, then open a Developer PowerShell."
}

if (-not (Test-Command "py.exe")) {
    Write-Warning "Python launcher is missing. Install Python 3.11 x64 and open a new terminal."
} else {
    $python311 = @()
    try {
        $python311 = @(& py.exe -3.11 -c "import sys; print(sys.executable)" 2>$null)
    } catch {
        $python311 = @()
    }
    if ($LASTEXITCODE -eq 0 -and $python311.Count -eq 1) {
        Write-Host "Python 3.11: $($python311[0])"
    } else {
        Write-Warning "Python 3.11 is not registered with the launcher."
    }
}

if ((Test-Command "cmake.exe") -and (Test-Command "ninja.exe") -and $tools.CxxCompiler) {
    Write-Host "Host tool bootstrap is ready. Run scripts/test-local.ps1 next."
} else {
    Write-Host "Host tool bootstrap is incomplete. Install the reported missing tools, reopen PowerShell, and run this check again."
    exit 2
}
