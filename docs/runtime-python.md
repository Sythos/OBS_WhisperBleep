<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Python, Whisper, PyTorch and model runtime strategy

The first public installer strategy keeps Python, OpenAI Whisper, PyTorch,
CUDA libraries and model weights outside the installer. The OBS plugin remains
portable and host-owned: it starts one persistent JSON-lines bridge per filter
instance, performs no process launch or inference in the realtime callback and
falls back to delayed pass-through audio when the optional runtime is absent or
fails.

=> Supported ownership boundary

The host owns the Python environment, the bridge process, its lifetime and the
per-user model cache. The C++ side owns bounded audio capture, timestamps,
phrase matching, replacement rendering and safe shutdown. The bridge owns only
Whisper model loading and transcription. This separation keeps the default C++
build and the unsigned packages free from Python and model-weight licensing or
redistribution obligations.

The current native adapter accepts `OBS_WHISPERBLEEP_PYTHON` for an explicit
interpreter path and `OBS_WHISPERBLEEP_BRIDGE_SCRIPT` for an explicit bridge
path. Until interpreter selection is exposed in the OBS UI, set these variables
in the environment inherited by OBS, or make the intended virtual-environment
interpreter available as `python` on the plugin process `PATH`. The local test
harness accepts an explicit interpreter path and is the recommended way to
validate a venv.

=> Reproducible Windows workstation setup

The supported local path is a user-owned Python 3.11 x64 environment plus a
normal C++ toolchain. Install the tools with the Windows package manager or
their official installers, then open a new terminal so `py`, `cmake` and the
compiler are visible:

```text
winget install --id Python.Python.3.11 --exact --source winget --interactive
winget install --id Kitware.CMake --exact --source winget --interactive
winget install --id Ninja-build.Ninja --exact --source winget --interactive
```

The repository also provides a read-only check for these tools. Add
`-InstallTools` only when you explicitly want WinGet to install the missing
Python, CMake and Ninja packages:

```text
pwsh -File scripts/bootstrap-local.ps1
pwsh -File scripts/bootstrap-local.ps1 -InstallTools
```

For native C++ builds, install the Visual Studio 2022 C++ workload (Desktop
development with C++) or use another C++20 compiler supported by CMake. The
OBS runtime installed on a workstation is not an SDK: native compilation also
needs matching OBS headers and the `libobs` import/library file. The CI lane
builds these from the pinned OBS source; a local native run must provide the
same paths through `OBS_SDK_DIR` and `OBS_LIB`.

Create the project environment and install the pinned CPU baseline with:

```text
pwsh -File scripts/test-local.ps1 -InstallRuntime -SkipCpp
```

Use `-RequireNative` only after CMake, a C++20 compiler and matching OBS SDK
paths are available. GPU testing is a separate opt-in: install the PyTorch
wheel selected by the official selector for the installed NVIDIA driver, then
run the same harness with an existing verified model path. The bootstrap never
downloads model weights and never places Python, CUDA or models inside the
installer.

=> Python baseline and dependency policy

Use a dedicated 64-bit virtual environment per user. Python 3.11 is the
conservative baseline because the upstream OpenAI Whisper README states that
its code is expected to work with Python 3.8 through 3.11. Newer Python
versions may work with current PyTorch wheels, but they are not the baseline
until the complete bridge/model test passes on that interpreter.

The repository provides `runtime/requirements-cpu.txt` for the first CPU
baseline:

- `openai-whisper==20250625`;
- `torch==2.7.1`;
- `numpy>=1.26,<3`.

The file installs packages only. It never downloads a Whisper model. After a
successful installation, record the resolved package versions and platform in
the local test report; a future release gate may promote a fully hashed lock
file after the supported Python matrix is fixed.

For a GPU environment, install OpenAI Whisper and NumPy from the same base,
then install the PyTorch wheel selected by the official
[PyTorch installation selector](https://pytorch.org/get-started/locally/) for
the host driver and operating system. Do not copy CUDA redistributables into
the OBS package. The current selector exposes CUDA wheel choices independently
of the plugin's historical `CUDA 13.2` capability label; that label must not be
presented as a verified wheel target until a real host test records the driver,
PyTorch and CUDA versions together.

=> Model acquisition and cache policy

Model weights remain outside Git and outside all installers. A model may be
used only after the catalog metadata, source, format and SHA-256 value have
been verified. The model manager writes a temporary file and publishes it by
rename, preserving a previous valid model if a transfer or activation fails.

The native bridge currently receives an explicit cache file path derived from
the selected model identifier. The model-manager-to-native-OBS wiring is still
a release gate: a real OBS test must prove that a UI selection resolves to the
same verified cache entry before the bridge is initialized. Until that wiring
is complete, the local real-model test requires `--model-path` and refuses to
download or infer from a model name.

Start real testing with `tiny.en` for English or `tiny` for multilingual audio.
On a GPU shared with OBS and a game or application, the model and the streamed
application must fit together in VRAM. Prefer CPU or a smaller model when the
remaining VRAM is not clearly sufficient.

=> Device and language policy

The C++ settings currently expose `Auto`, `CPU` and a historical CUDA label,
but the optional Python bridge does not yet receive the selected device as a
protocol field. `Auto` therefore follows the PyTorch/Whisper host default, and
the UI selection cannot yet be claimed as a complete GPU policy. The next
runtime integration step must add an explicit `device` field, validate
`torch.cuda.is_available()`, perform a VRAM preflight and report a controlled
CPU fallback.

The OBS UI locale and the spoken-language policy must also be separate values.
The local bridge test sends a Whisper language such as `en` or `it`; the native
adapter still needs the final locale-to-language mapping and English-only model
enforcement wired through the model manager.

=> Local verification levels

The complete local procedure is implemented by
`scripts/test-local.ps1`. It is deliberately staged:

==> Protocol smoke test

Run without a model to compile both Python sources, launch the bridge, send two
JSON-lines requests and verify the controlled `unavailable` or `error` response.
No network access or model download is performed.

==> Real bridge/model test

Pass an existing absolute model file with `-ModelPath`. Optionally pass a local
16-bit PCM WAV file with `-AudioPath`. The harness starts one bridge process,
sends `initialize` and `transcribe`, validates the response shape and checks
all returned transcript intervals. It never stores transcript text in the
repository or downloads an audio asset.

==> C++ and native OBS tests

Without switches, the harness configures, builds and runs the dependency-free
C++ suite through CMake/CTest. `-RequireNative` additionally requires
`-ObsSdkDir` and `-ObsLib` and builds the native OBS module. `-SkipCpp` is an
explicit Python-only run; if CMake is missing during a default run, the script
returns exit code `2` to distinguish an incomplete environment from a failed
test.

Examples from the repository root:

```powershell
pwsh -File scripts/test-local.ps1 -SkipCpp
pwsh -File scripts/test-local.ps1 -PythonPath C:\Python311\python.exe -SkipCpp
pwsh -File scripts/test-local.ps1 -PythonPath C:\Python311\python.exe -ModelPath C:\Users\User\AppData\Local\Sythos\OBS-WhisperBleep\models\tiny.en.model -AudioPath C:\test\speech.wav -SkipCpp
pwsh -File scripts/test-local.ps1 -RequireNative -ObsSdkDir C:\obs-sdk -ObsLib C:\obs-sdk\libobs.lib
```

`-InstallRuntime` is opt-in. It creates `.runtime-venv`, installs the pinned
CPU requirements and then runs the same checks; it requires Python 3.11 and an
explicit user decision to allow package downloads.

Every run writes only the ignored `test-results/runtime-local-report.json`.
The report records whether a model was supplied and whether C++ verification
was portable or native, but it does not copy model weights, audio or transcript
text.

=> Acceptance gates before bundling

The runtime must not be bundled or called production-ready until all of these
are demonstrated on each supported platform:

- a clean venv install and protocol smoke test;
- CPU model initialization and transcription with a verified `tiny` model;
- explicit GPU device selection, CUDA availability and VRAM preflight where
  GPU support is claimed;
- model-manager download, checksum, cache activation and rollback through the
  actual OBS UI;
- separate UI and spoken-language settings, including English-only enforcement;
- persistent bridge restart and shutdown without callback blocking;
- p50/p95 end-to-end latency within the 1.5-second budget, or an explicit
  operator decision to reassess the video delay at 2 seconds;
- pass-through behavior when Python, dependencies, model or bridge are absent.

Until then, the official unsigned installers remain host-runtime packages and
must not include Python, PyTorch, CUDA redistributables, model weights or
unverified audio assets.

=> License and redistribution boundary

The optional host environment is not part of the plugin license or installer
payload. The current inventory is:

- OpenAI Whisper and its published model weights: MIT;
- PyTorch: BSD-style license as published by the PyTorch project;
- NumPy: BSD-3-Clause;
- tiktoken, used by OpenAI Whisper: MIT;
- Python: Python Software Foundation License;
- FFmpeg, if a host chooses to use it for file decoding: host-provided and
  subject to the exact build's LGPL/GPL configuration.

If a future release bundles any of these components, the corresponding license
texts, notices, source obligations and redistribution permissions must be added
to `NOTICE`, `third_party/` and the package scan before publication. No optional
runtime dependency is currently bundled by this repository.

=> References

- [OpenAI Whisper setup and model documentation](https://github.com/openai/whisper)
- [PyTorch local installation and CUDA selector](https://pytorch.org/get-started/locally/)
- [Repository model/cache contract](model-cache.md)
- [Repository packaging contract](packaging.md)
