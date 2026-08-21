<#
.SYNOPSIS
Runs the local OBS WhisperBleep verification layers.

.DESCRIPTION
The default run performs Python bridge syntax/protocol checks and the
dependency-free C++ build/test suite. It never downloads a model. Use
-ModelPath to run a real local Whisper transcription check against a model that
has already been downloaded and verified by the user. Use -InstallRuntime only
when a Python 3.11 environment may be created and packages may be downloaded.
#>

[CmdletBinding()]
param(
    [string]$PythonPath,
    [string]$ModelPath,
    [string]$AudioPath,
    [string]$BuildDirectory = "build/local",
    [string]$ObsSdkDir,
    [string]$ObsLib,
    [switch]$InstallRuntime,
    [switch]$SkipCpp,
    [switch]$RequireNative
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Write-Host "==> $Label"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
}

function Resolve-Python {
    if ($PythonPath) {
        $resolved = (Resolve-Path $PythonPath -ErrorAction Stop).Path
        return [pscustomobject]@{ Executable = $resolved; Prefix = @() }
    }

    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $launcher) {
        $installed = @()
        try {
            $installed = @(& $launcher.Source -0p 2>$null)
        } catch {
            $installed = @()
        }
        if ($installed -match "3\.11") {
            return [pscustomobject]@{ Executable = $launcher.Source; Prefix = @("-3.11") }
        }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $python) {
        throw "Python was not found. Install Python 3.11 x64 or pass -PythonPath."
    }
    return [pscustomobject]@{ Executable = $python.Source; Prefix = @() }
}

function Invoke-Python {
    param(
        [Parameter(Mandatory = $true)][pscustomobject]$Python,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Invoke-Checked -FilePath $Python.Executable `
        -Arguments (@($Python.Prefix) + @($Arguments)) -Label $Label
}

$python = Resolve-Python
$versionText = & $python.Executable @($python.Prefix) -c `
    "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query the selected Python interpreter."
}
$version = [version]$versionText.Trim()
if ($version.Major -ne 3 -or $version.Minor -lt 10) {
    throw "Python 3.10 or newer is required; selected version is $version."
}
if ($version.Minor -gt 11) {
    Write-Warning "Python $version is outside the conservative OpenAI Whisper 3.10-3.11 baseline. Use Python 3.11 for the real-model test."
}

if ($InstallRuntime) {
    if ($version.Minor -ne 11) {
        throw "-InstallRuntime requires Python 3.11. Select it with -PythonPath or install it through the py launcher."
    }
    $venvPath = Join-Path $repoRoot ".runtime-venv"
    if (-not (Test-Path $venvPath)) {
        Invoke-Python -Python $python -Arguments @("-m", "venv", $venvPath) -Label "Create Python runtime environment"
    }
    $venvPython = if ($env:OS -eq "Windows_NT") {
        Join-Path $venvPath "Scripts/python.exe"
    } else {
        Join-Path $venvPath "bin/python"
    }
    $python = [pscustomobject]@{ Executable = (Resolve-Path $venvPython).Path; Prefix = @() }
    Invoke-Python -Python $python -Arguments @("-m", "pip", "install", "--upgrade", "pip") `
        -Label "Upgrade pip in the isolated runtime"
    Invoke-Python -Python $python -Arguments @("-m", "pip", "install", "--requirement", "runtime/requirements-cpu.txt") `
        -Label "Install pinned CPU runtime dependencies"
}

Invoke-Python -Python $python -Arguments @(
    "-m", "py_compile",
    "runtime/openai_whisper_bridge.py",
    "runtime/test_bridge.py"
) -Label "Compile Python bridge sources"

$bridgeArguments = @(
    "runtime/test_bridge.py",
    "--python", $python.Executable,
    "--bridge", (Join-Path $repoRoot "runtime/openai_whisper_bridge.py")
)
if ($ModelPath) {
    $bridgeArguments += @("--model-path", (Resolve-Path $ModelPath -ErrorAction Stop).Path)
}
if ($AudioPath) {
    $bridgeArguments += @("--audio-path", (Resolve-Path $AudioPath -ErrorAction Stop).Path)
}
Invoke-Python -Python $python -Arguments $bridgeArguments -Label "Run Whisper bridge protocol/model smoke test"

$probeCode = @'
import importlib.util
import sys
modules = {name: importlib.util.find_spec(name) is not None for name in ('numpy', 'torch', 'whisper')}
print(modules)
try:
    import torch
    print({'torch': torch.__version__, 'cuda_available': bool(torch.cuda.is_available()), 'cuda_version': torch.version.cuda})
except Exception as error:
    print({'torch_error': str(error)})
'@
& $python.Executable @($python.Prefix) -c $probeCode
if ($LASTEXITCODE -ne 0) {
    throw "Python dependency probe failed."
}

$cppIncomplete = $false
if ($SkipCpp) {
    Write-Warning "C++ verification skipped by request."
} else {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -eq $cmake) {
        if ($RequireNative) {
            throw "CMake is required for -RequireNative but was not found."
        }
        Write-Warning "CMake was not found; C++ verification is incomplete. Install CMake, Ninja and a C++20 toolchain or rerun with -SkipCpp."
        $cppIncomplete = $true
    } elseif ($RequireNative) {
        if (-not $ObsSdkDir) { $ObsSdkDir = $env:OBS_SDK_DIR }
        if (-not $ObsLib) { $ObsLib = $env:OBS_LIB }
        if (-not $ObsSdkDir -or -not $ObsLib) {
            throw "-RequireNative needs -ObsSdkDir and -ObsLib (or OBS_SDK_DIR and OBS_LIB environment variables)."
        }
        Invoke-Checked -FilePath $cmake.Source -Arguments @(
            "-S", ".", "-B", $BuildDirectory, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DOBS_WHISPERBLEEP_BUILD_TESTS=ON",
            "-DOBS_WHISPERBLEEP_BUILD_PLUGIN_STUB=OFF",
            "-DOBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON",
            "-DOBS_SDK_DIR=$ObsSdkDir",
            "-DOBS_LIB=$ObsLib"
        ) -Label "Configure native OBS build"
        Invoke-Checked -FilePath $cmake.Source -Arguments @("--build", $BuildDirectory, "--parallel") `
            -Label "Build native OBS module and C++ tests"
        Invoke-Checked -FilePath "ctest" -Arguments @("--test-dir", $BuildDirectory, "--output-on-failure") `
            -Label "Run native C++ tests"
    } else {
        Invoke-Checked -FilePath $cmake.Source -Arguments @("--preset", "debug") `
            -Label "Configure dependency-free C++ tests"
        Invoke-Checked -FilePath $cmake.Source -Arguments @("--build", "--preset", "debug") `
            -Label "Build dependency-free C++ tests"
        Invoke-Checked -FilePath "ctest" -Arguments @("--test-dir", "build/debug", "--output-on-failure") `
            -Label "Run dependency-free C++ tests"
    }
}

$reportDirectory = Join-Path $repoRoot "test-results"
New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
$report = [ordered]@{
    python = $versionText.Trim()
    model_path_supplied = [bool]$ModelPath
    audio_path_supplied = [bool]$AudioPath
    cpp_verification = if ($SkipCpp) { "skipped" } elseif ($cppIncomplete) { "unavailable" } elseif ($RequireNative) { "native" } else { "portable" }
    model_weights_downloaded_by_script = $false
}
$report | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $reportDirectory "runtime-local-report.json")

if ($cppIncomplete) {
    [Console]::Error.WriteLine("Python bridge verification passed, but the complete local test is incomplete because the C++ toolchain is unavailable.")
    exit 2
}

Write-Host "Local verification completed successfully."
