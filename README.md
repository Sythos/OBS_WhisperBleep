<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> OBS WhisperBleep

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/) [![GitHub release](https://img.shields.io/github/v/release/Sythos/OBS_WhisperBleep?display_name=tag&sort=semver)](https://github.com/Sythos/OBS_WhisperBleep/releases/latest) [![Last commit](https://img.shields.io/github/last-commit/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/commits/main/) [![Open issues](https://img.shields.io/github/issues/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/issues)<br>
[![License](https://img.shields.io/github/license/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/blob/main/LICENSE) [![Git](https://img.shields.io/badge/Git-Repository-F05032?logo=git&logoColor=white)](https://git-scm.com/) [![GitHub](https://img.shields.io/badge/GitHub-Sythos%2FOBS_WhisperBleep-181717?logo=github&logoColor=white)](https://github.com/Sythos/OBS_WhisperBleep)

OBS WhisperBleep is a native OBS Studio audio filter designed to censor
configured words and phrases in near real time.

The idea is simple: the filter receives the audio that normally flows through
OBS, keeps a small buffer long enough to recognize speech with Whisper and,
when a match is found, replaces the affected interval with a sound selected by
the user. The replacement can be a beep, a duck sound, a bark or a custom audio
file.

The configuration is intended to live entirely in the OBS Properties panel.
The plugin will handle buffering, timestamps and expensive work on dedicated
workers so the OBS audio callback is not blocked by downloads, disk I/O or
inference.

=> Practical synchronization note

To make audio/video synchronization easier to maintain, the recommended setup
is three consecutive audio delays of **500 msec (0.5 seconds) each**, for a
total delay of approximately **1.5 seconds**. Verify the final value against
the actual scene and OBS chain: the required delay depends on the Whisper model,
hardware and total system latency.

=> Project status

This repository now contains the M0 architectural scaffold, the M1 native OBS
integration boundary, the M2 deterministic audio pipeline, the M3 model
catalog/cache boundary, the M4 matching and configuration boundary and the M5
backend/platform boundary. M5 defines capability-based backend selection, an
absolute frame-timeline bridge, deterministic WAV asset loading and the first
cross-platform packaging boundary. The real Whisper inference runtime,
production CUDA integration, signed installers and automated release publishing
remain later milestones.

=> M0 and M1: scaffold and OBS boundary

The first reference version is `0.1.0`. The repository separates core,
model/runtime, OBS and platform code, and now includes a pass-through native OBS
filter target that is enabled only when an OBS SDK and `libobs` library are
provided explicitly.

With CMake 3.20 or newer and a C++20 compiler, verify the scaffold with:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The default CI build remains OBS-independent and deterministic. The native M1
target registers the filter and its initial Properties, manages instance
lifecycle and settings, and returns incoming audio unchanged. It does not run
Whisper inference or download models yet; those capabilities belong to later
milestones.

To enable the native module in a local OBS SDK build, configure
`OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON`, set `OBS_SDK_DIR`, and provide
`OBS_LIB` when CMake cannot locate `libobs` automatically.

=> M2 deterministic audio pipeline

M2 keeps the realtime path bounded and predictable. Incoming frames enter a
non-blocking single-producer/single-consumer queue; if it is full, the newest
frame is dropped and counted rather than blocking OBS. A dedicated worker
drains accepted frames and finishes draining them before shutdown joins the
worker thread.

The test pipeline uses simulated speech segments with timestamp-to-frame
conversion at the configured sample rate and a configurable delay, then merges
overlapping or touching censor intervals deterministically. A dependency-free
synthetic beep is available for tests and as the initial replacement. The
existing renderer loops short replacement
audio, trims long replacement audio at the target interval, maps channels and
can apply a configurable edge fade while preserving the input buffer length.

M2 does not run Whisper or download models. The M3 boundary adds the verified
catalog, cache state and transport abstraction without placing model weights in
the repository.

=> M3 model catalog and cache boundary

The catalog mirrors the six required OpenAI Whisper identifiers and records the
official source URL, upstream version, PyTorch checkpoint format, MIT model
license and SHA-256 value from the upstream manifest. Model weights remain
outside Git and are resolved into a per-user cache directory.

The downloader verifies existing or newly transferred files before publishing
them through a temporary-file and rename flow. Local `file://` sources are
supported for deterministic tests; HTTPS requires an explicit platform transport
adapter. `ModelManager` exposes synchronized downloading, verification and
activation states, provides a manager-owned asynchronous selection worker,
preserves the last active model on failure, supports rollback and provides
retain-previous or selected-only retention policies. A missing or non-absolute
per-user cache path fails safely before any transfer is attempted.

=> M4 matching, replacement choices and UI contract

M4 makes phrase matching configurable and testable. The default matching policy
is case-insensitive and normalizes runs of whitespace; empty and duplicate
entries are ignored. Matching remains outside the OBS realtime callback and
produces intervals that can be associated with Whisper timestamps before the
replacement plan is rendered. Punctuation, word-boundary behavior and partial
matches remain explicit policy choices rather than hidden string operations.

The replacement selector exposes the built-in `beep`, duck and bark choices as
well as a user-provided custom audio file. Built-in duck and bark files may be
added only after their provenance, royalty-free status and redistribution rights
have been verified. Custom audio is validated and loaded outside the realtime
callback; an unavailable or invalid asset must leave the original audio intact
and expose a readable status to the user.

The plugin Properties workflow uses a vertical section menu on the left and a
wide contextual pane on the right. Every initial opening activates the first
left-hand item and shows its corresponding content on the right. A `Check
Updates` action in the general or about section compares the installed version
with the latest GitHub release at
`https://github.com/Sythos/OBS_WhisperBleep/releases`. When a newer version is
available, the plugin shows a popup with the release information and offers to
open the releases page in the external browser. It never silently downloads or
installs an update, and update checks do not run in the realtime audio path.

=> M5 backend, timeline, assets and packaging boundary

The backend boundary exposes `Auto`, `CPU` and `CUDA 13.2` as capability-driven
choices. `CPU` remains the portable fallback. `Auto` may select CUDA only when
the host reports a validated CUDA 13.2 capability; an unavailable requested
backend must produce a readable state and fall back without blocking OBS. CUDA
is not available on macOS, so `Auto` must use the validated non-CUDA backend
there.

When a GPU backend is selected, the graphics card's VRAM must contain both the
Whisper model and the game or application being streamed by OBS. Users should
avoid models that are too large for the remaining VRAM. When a reasonably
recent mid-range CPU is available, selecting `CPU` is recommended to leave the
VRAM available for the game or application.

The timeline bridge keeps captured audio, transcript segments and replacement
intervals in one absolute frame domain. It converts validated chunk-relative
results before scheduling, while the timestamp coordinator applies the
configured delay exactly once, and it rejects invalid or unsafe values. M5
does not claim word-level timestamps or a real Whisper inference engine; those
require the runtime milestone.

Replacement audio is loaded outside the OBS realtime callback. The deterministic
asset boundary is based on validated WAV data and must reject malformed,
unsupported or unreadable files while preserving pass-through audio. Any audio
asset downloaded from the Internet must come from a verifiable source that is
100% royalty-free and explicitly permits use, modification and redistribution.
The source URL, author, license reference, download date and checksum must be
recorded before an asset is included in `assets/`; the plugin must never fetch
an unverified asset automatically.

M5 establishes CPack staging for Windows x64, Linux x86_64 and macOS universal
package layouts, including documentation and applicable license notices. A
staged package is not yet a finished public release: model weights, model
caches, credentials, unverified audio, CUDA redistributables and runtime
dependencies are not bundled by default. macOS signing/notarization, complete
Whisper runtime packaging, and tag-gated binary publication remain part of the
next release milestone.

=> Technologies

The project is built around the following technologies:

- C++20 for the native OBS Studio plugin and real-time audio processing.
- CMake for cross-platform configuration, builds and packaging.
- [OBS Studio 32.2.1](https://obsproject.com/download) as the host
  application and native plugin API target; this is the stable version listed
  on the official OBS download page, verified on 2026-08-16.
- [OpenAI Whisper](https://github.com/openai/whisper) for speech-recognition
  models and the AI processing structure.
- Python for model tooling and supporting automation.
- [PyTorch](https://pytorch.org/) for CPU execution and GPU acceleration through
  CUDA.

=> Inspiration and disclaimer

OBS WhisperBleep is original code and is developed without forking CleanStream.
The project is inspired by Royshil's CleanStream idea; at the time of writing,
the related Git repository appeared abandoned and had not been updated for more
than nine months. I was unable to establish contact or receive a response by
email, repository messages or social media.

This project claim to NOT incorporate CleanStream code, commits, assets or model weights.
Any third-party component will be added only after its origin,
license and attribution have been verified.
