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

The native Properties workflow must be checked with the first left-hand menu
item selected on initial opening, its content visible in the wide right-hand
context pane, and the remaining sections reachable through the vertical menu.
The general or about section must expose `Check Updates`; the action compares
the canonical installed version with the latest GitHub release, reports a newer
version in a popup and offers an explicit external-browser link. It must never
silently download or install an update or block the OBS audio callback.
