<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Development setup

=> M0 prerequisites

Minimum requirements:

- CMake 3.20 or newer;
- a C++20 compiler supported by the platform;
- Git.

Configure and build:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The M0 build uses the OBS-independent plugin stub. The Whisper runtime, OBS SDK,
CUDA and model weights are not required for M0 verification.

=> M1 native OBS setup

M1 requires an OBS SDK that matches the target OBS installation and exposes the
native filter, properties and audio callback interfaces. The CMake cache option
for the SDK location is:

```text
-DOBS_SDK_DIR=C:\path\to\obs-sdk
```

For example, an M1 configure command is:

```text
cmake -S . -B build/m1 -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBS_SDK_DIR=C:\path\to\obs-sdk -DOBS_LIB=C:\path\to\libobs.lib -DOBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON -DOBS_WHISPERBLEEP_BUILD_PLUGIN_STUB=OFF
cmake --build build/m1
ctest --test-dir build/m1 --output-on-failure
```

`OBS_SDK_DIR` supplies the OBS headers and `OBS_LIB` supplies the `libobs`
library when CMake cannot locate it automatically. The native target is built
only when `OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON`; the default M0-M2 CI build
does not require an OBS SDK.

The M1 verification must cover filter registration, instance lifecycle,
pass-through audio, initial Properties defaults, settings updates and settings
persistence. Keep these tests independent from Whisper weights, network access,
Python, PyTorch and CUDA; those dependencies belong to later milestones.

==> Missing-model startup behavior

The native filter checks the selected model cache entry when the instance is
created. If it is missing or empty, OBS opens that filter's Properties window
on the UI task queue, allowing the user to select another model. Audio remains
safe delayed pass-through while the runtime is unavailable. This prompt is
deliberately presence-only: model downloads and SHA-256 activation must remain
asynchronous and are not performed by the OBS audio callback.

=> M2 deterministic pipeline verification

M2 keeps the verification path independent of OBS installation, Whisper model
weights, network access, Python, PyTorch and CUDA. Build the same deterministic
targets and run the complete test suite:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The M2 tests cover the bounded queue's non-blocking full-queue policy, worker
startup and drain-before-join shutdown, simulated speech timestamps with a
configurable delay, deterministic merging of overlapping and touching
intervals, synthetic beep generation, and replacement duration handling. The
renderer must loop a replacement that is shorter than the censored interval,
trim a replacement that is longer, and apply the configured edge fade without
changing the output buffer length. It must also ignore intervals fully before
the input buffer and clip intervals that start before frame zero without
performing out-of-bounds writes.

M2 is intentionally a testable audio and scheduling boundary. Whisper
inference, model downloads, checksum verification and model-cache management
are covered by the separate M3 tests below and are not setup prerequisites.

=> M3 model and cache verification

M3 verification remains dependency-free and must not download Whisper weights.
Run the complete test suite as above; the M3 tests cover the official catalog
metadata, SHA-256-aware local cache transfer, cancellation, injected transport,
strict model verification/activation/rollback states, the manager-owned
asynchronous selection worker and CPU fallback metadata. The default downloader
accepts local `file://` fixtures; an HTTPS transport is an explicit integration
dependency and is never called by the OBS realtime path. A missing or relative
per-user cache path is rejected before transfer.

=> M4 matching, replacement and UI verification

M4 verification must cover the configurable phrase policy independently from
Whisper inference: case-insensitive matching, whitespace normalization, empty
and duplicate entry handling, timestamp association and deterministic interval
generation. The replacement selector must preserve the stable `beep`, duck,
bark and custom-audio identifiers. Custom audio validation and loading belong
on a worker, while a missing or unsupported file must preserve pass-through
audio and expose a readable error state.

The default replacement is the dependency-free synthetic `beep`. It is created
from the input format without a bundled or downloaded sound file. If a selected
duck, bark or custom asset is not available or fails validation, the renderer
must keep the original audio unchanged.

The native Properties workflow must be checked with the first left-hand menu
item selected on initial opening, its content visible in the wide right-hand
context pane, and the remaining sections reachable through the vertical menu.
The general or about section must expose `Check Updates`; the action compares
the canonical installed version with the latest GitHub release, reports a newer
version in a popup and offers an explicit external-browser link. It must never
silently download or install an update or block the OBS audio callback.

The `Debug` option is off by default. When enabled, the native module resolves
the user home from `%USERPROFILE%` on Windows or `$HOME` on Linux/macOS and
creates an exclusive `WhisperBleep_yyyymmdd_xxx.log` file there. Existing daily
files are skipped rather than overwritten; diagnostic lines are sanitized and
are written during lifecycle/settings work, never from the realtime callback.

Latency validation starts with three consecutive 500 ms audio delays. The
monotonic evaluator must keep Whisper processing and replacement within the
1.5-second baseline; if measurements exceed it, the result records an explicit
reassessment of a 2.0-second video delay for a later operator decision.

=> M5 backend, timeline and asset verification

M5 keeps backend selection capability-driven and testable without requiring a
Whisper model in the repository. The supported choices are:

- `CPU`, the portable fallback;
- `Auto`, which selects the best backend reported as available by the host;
- `CUDA 13.2`, only when the build, driver, runtime and model integration have
  all reported a validated capability on Windows or Linux.

An unavailable explicit backend must expose a readable state and fall back to
CPU without blocking OBS. CUDA is not available on macOS; `Auto` must choose a
validated non-CUDA backend there. A GPU selection must leave enough VRAM for
both the Whisper model and the game or application being streamed. Prefer a
smaller model, or use `CPU` when a reasonably recent mid-range CPU is available
so that VRAM remains available to the streamed application.

The M5 timeline bridge uses one absolute audio-frame domain across capture,
transcript segments and replacement intervals. The timestamp coordinator owns
the configured delay and must apply it exactly once before scheduling. Tests
must cover conversion from validated timestamps, delay application,
invalid-value rejection, discontinuity generations and boundary rejection. The
bridge must not claim word-level timing until the real Whisper runtime supplies
it, and it must remain outside the OBS realtime callback.

Replacement assets are loaded and decoded on a worker before they are used by
the realtime renderer. The deterministic asset path accepts validated WAV data
and must reject malformed, unsupported or unreadable files while preserving
pass-through audio. No callback may download an asset or read a file. If an
asset is sourced from the Internet, its source, author, license text or URL,
download date and checksum must be recorded; it must be 100% royalty-free and
explicitly usable and redistributable, including in the relevant release
packages. Unverified audio must not enter `assets/` or a package.

M5 packaging checks cover the CPack staging layout for Windows x64, Linux
x86_64 and macOS universal targets. The staging check must confirm that model
weights, model caches, credentials, unverified audio and undeclared runtime
dependencies are absent. A successful staging check is not yet a finished
installer or public release: OBS SDK/runtime bundling, CUDA redistribution,
macOS signing/notarization and tag-gated binary publication remain deferred.

The local dependency-free verification remains:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The same test run includes `m6_i18n`, which verifies the locale fallback and
the language dropdown contract without requiring an OBS installation. It also
includes `m6_debug_log` and `m6_latency`. Native OBS label/debug rendering
still requires the M1 SDK configuration described above.

=> M7 optional OpenAI Whisper vertical-slice verification

M7 remains testable through the normal dependency-free build and test commands
above. Its C++ tests inject a JSON-lines process runner into
`OpenAIWhisperRuntime`; they do not require Python, OpenAI Whisper, PyTorch,
NumPy, a model file, network access or an OBS SDK. The native OBS adapter and
the unsigned Linux/Windows package lane are now CI-validated, while the
optional Python runtime remains host-owned.

For the complete local procedure, use `scripts/test-local.ps1` and read
`docs/runtime-python.md`. The default harness compiles the bridge and runs a
no-model protocol smoke test without network access. Passing an existing
absolute `-ModelPath` enables the real model check; passing `-AudioPath` adds a
local PCM WAV transcription input. The harness never downloads a model itself.

The bridge expects one JSON object per stdin line and returns one JSON object per
stdout line. Do not add process launching to the portable core, and do not treat
the bridge script as proof that the dependencies or a model are installed.

`EndToEndAudioPipeline` accepts frames through a bounded worker. A rejected
submission, unsupported sample rate, runtime error or invalid timeline result
must keep the original frame as pass-through audio. The dependency-free `beep`
is the default replacement for a confirmed match. The remaining local gates are
verified model-cache activation, explicit backend/device propagation and real
end-to-end latency measurements within the 1.5-second budget.
