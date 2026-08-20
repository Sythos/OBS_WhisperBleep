<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> OBS WhisperBleep

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/) [![GitHub release](https://img.shields.io/github/v/release/Sythos/OBS_WhisperBleep?display_name=tag&sort=semver)](https://github.com/Sythos/OBS_WhisperBleep/releases/latest) [![Last commit](https://img.shields.io/github/last-commit/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/commits/main/) [![Open issues](https://img.shields.io/github/issues/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/issues)<br>
[![License](https://img.shields.io/github/license/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/blob/main/LICENSE) [![Git](https://img.shields.io/badge/Git-Repository-F05032?logo=git&logoColor=white)](https://git-scm.com/) [![GitHub](https://img.shields.io/badge/GitHub-Sythos%2FOBS_WhisperBleep-181717?logo=github&logoColor=white)](https://github.com/Sythos/OBS_WhisperBleep)

> **Pre-alpha notice:** OBS WhisperBleep is currently in pre-alpha. Expect
> incomplete features, breaking changes and possible malfunctions. Do not use
> it in production or live broadcasts without thorough testing.

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

=> Contributors and support

Bug reports, malfunctions and other problems should be reported through a
GitHub [Issue](https://github.com/Sythos/OBS_WhisperBleep/issues). Please write
in English whenever possible; if needed, using Google Translate is perfectly
fine. Include as much detail as possible, especially:

- the plugin version;
- the OBS Studio version in use;
- the selected Whisper model;
- the operating system, relevant hardware and selected backend;
- exact step-by-step instructions to reproduce the problem;
- the expected result and what happened instead;
- relevant logs, screenshots or other supporting information.

Without sufficient detail, it may not be possible to understand, reproduce and
investigate the problem clearly and safely.

=> Project status

The project is still in pre-alpha. The remaining development milestones are:

- native OBS wiring for the M7 Whisper vertical slice, including the host-owned
  bridge-process lifecycle and delayed processed-output hand-off;
- complete native OBS/Qt configuration UI and the full update-check flow;
- production CPU/GPU backend integration, runtime dependency bundling and
  validation on supported systems;
- verified royalty-free and redistributable replacement assets with complete
  provenance metadata;
- realtime hardening, stress testing, regression coverage and long-running
  stability checks;
- release packaging, checksums, signing or notarization where applicable and
  tag-gated publication;
- complete user, setup and support documentation.

The current codebase provides early architectural boundaries and deterministic
test coverage, but it does not yet claim public-release quality.

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

The catalog mirrors the six multilingual OpenAI Whisper identifiers and the
four official English-only variants (`tiny.en`, `base.en`, `small.en` and
`medium.en`). It records each canonical selector name, official source URL,
upstream version, PyTorch checkpoint format, MIT model license, explicit
`english_only` metadata and SHA-256 value from the upstream manifest. Model
weights remain outside Git and are resolved into a per-user cache directory.

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

The default replacement for a matched blacklist phrase is `beep`. It is
generated as a bounded sine wave in the core and requires no bundled file,
network request or external audio resource. If an asset-backed replacement is
selected but is unavailable or invalid, the renderer preserves the original
audio instead of emitting silence.

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
package layouts, including documentation and applicable license notices. The
release-artifact milestone expects an unsigned Windows x64 NSIS installer,
a Linux x86_64 TGZ archive, an amd64 DEB targeted at Ubuntu 26.04 LTS and an
unsigned macOS universal binary archive. CI must accept exactly those four
versioned payloads, validate their contents and publish checksums; Debian 13
and RPM support remain a later evaluation, while a SignPath signing handoff is
a separate step.

A staged package is not yet a finished public release: model weights, model
caches, credentials, unverified audio, CUDA redistributables and runtime
dependencies are not bundled by default. The default CI builds the portable
core and plugin stub; the native OBS module still requires an explicit OBS SDK
and `libobs`, and M7 processed output is not yet wired into its callback.
macOS signing/notarization, complete Whisper runtime packaging and tag-gated
publication remain part of the next release milestone.

=> M7 optional OpenAI Whisper vertical slice

Version `0.1.0` now contains an optional, testable path from accepted audio to
transcription, phrase matching and replacement rendering. `EndToEndAudioPipeline`
uses one bounded worker: realtime submission only timestamps and enqueues a
frame, while Whisper calls, matching, timeline conversion, rendering and result
delivery run off the OBS audio callback. A full or rejected queue has a safe
default: the host keeps the original frame as immediate pass-through audio.

`OpenAIWhisperRuntime` is an optional adapter for
`runtime/openai_whisper_bridge.py`, a JSON-lines bridge to the OpenAI Whisper
Python package. The portable core neither links Python, PyTorch, NumPy or
Whisper nor launches a process. Instead, the host injects a process runner;
without one the adapter reports itself unavailable and the audio path remains
safe. The bridge imports its optional dependencies only after initialization.

The built-in, dependency-free `beep` remains the default replacement. M7 is a
vertical slice, not a claim of a finished native OBS runtime: native OBS
processed-output wiring, bridge-process ownership/lifetime in a platform host,
real latency evidence and production dependency packaging are the next gate.

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

==> Binary availability and warranty

Prebuilt binaries will be released as soon as they are ready, with Linux
x86_64 and Windows x64 as the initial targets for fully operational builds. The
current pre-alpha packages are still limited to the portable core and
plugin-stub boundary described above. A universal macOS build may also be
provided, but it will likely remain unsigned for now: Apple signing and
notarization require a paid developer membership, certificates and additional
administrative work that are not financially sustainable for the project at
this stage.

Everything is provided `as is`, without warranty of any kind. Please test it
carefully before relying on it in a live or production environment.
