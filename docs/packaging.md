<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Packaging

The project uses CMake/CPack and installs the core library, the plugin stub,
headers, documentation and license notices. M0 packages contain no Whisper
weights, caches, credentials or unverified audio assets.

The release workflow is tag-gated and may create artifacts only after the full
matrix has passed build, test and package validation.

=> M5 packaging boundary

M5 defines the first platform-aware CPack staging boundary for these target
layouts:

- Windows x64;
- Linux x86_64;
- macOS universal for Intel and Apple Silicon.

Staging may contain the plugin binaries, the project documentation, applicable
license notices and only audio assets whose provenance, 100% royalty-free
status and redistribution rights have been verified. Replacement audio is
validated as WAV data before it reaches the realtime renderer. Asset metadata
must retain the source URL, author, license reference, download date and
checksum. A file downloaded from the Internet is never trusted merely because
it is labelled free or royalty-free.

The M5 staging boundary deliberately excludes Whisper model weights, per-user
model caches, credentials, temporary files, unverified audio and undeclared
runtime dependencies. CUDA libraries and drivers are not bundled by this
boundary; CUDA 13.2 is a capability that must be validated on the target
Windows or Linux host. macOS remains a non-CUDA target for this phase.

A successful CPack staging check does not by itself make a public installer
ready. The native Whisper runtime, complete OBS/runtime dependency bundling,
Windows/Linux CUDA distribution decisions, and macOS signing and notarization
remain outside this unsigned package boundary. Tag-gated GitHub publication is
implemented for the validated installer payloads and the optional runtime
container.

=> Release artifact milestone

For version `X.Y.Z`, the release CI contract must produce exactly these four
payloads after package validation:

- `OBS-WhisperBleep-X.Y.Z-Windows-x64-unsigned.exe`: an unsigned Windows x64 NSIS
  installer;
- `OBS-WhisperBleep-X.Y.Z-Linux-x86_64.tar.gz`: a Linux x86_64 TGZ archive;
- `OBS-WhisperBleep-X.Y.Z-Linux-x86_64.deb`: an amd64 DEB built on and
  targeted primarily at Ubuntu 26.04 LTS;
- `OBS-WhisperBleep-X.Y.Z-macOS-universal.tar.gz`: an unsigned macOS universal
  binary archive for Intel and Apple Silicon.

The CI upload groups are platform-specific: one Windows x64 artifact containing
the `.exe`, one Linux x86_64 artifact containing the `.tar.gz` and `.deb`, and
one macOS universal artifact containing the `.tar.gz`. Release assembly must
download only those four payloads, reject missing or additional payload files,
scan every archive/installer staging tree for forbidden model, cache and
credential material, and publish one `SHA256SUMS` file alongside the payloads.
The exact version in every filename must equal `VERSION` and the newly created
release tag.

The DEB is intentionally built on the Ubuntu 26.04 LTS GitHub runner and is
declared `amd64`; the runner label is currently a GitHub Actions public preview.
The general build/test matrix remains on Ubuntu 24.04 for a stable baseline.
Debian 13 installation and RPM generation are deferred until dedicated
compatibility tests and dependency metadata are available.

The Windows installer and macOS archive are unsigned delivery artifacts.
Linux archives and the DEB use their normal platform filenames without an
`unsigned` suffix; this workflow does not attach a separate signature to them.
Signing is deliberately outside the CI build boundary: after the release
payloads have passed their checks, a separate SignPath handoff may sign the
Windows installer and any later platform-specific deliverables. The handoff
must not replace package validation, modify the release payload set silently or
claim that an unsigned artifact is signed.

The current package contents are limited by the implementation boundary. The
default CI builds the portable core and plugin stub; the opt-in native lane
builds the OBS module against an explicitly supplied OBS SDK and `libobs`, and
M7 now wires delayed processed output back into the native callback through a
bounded adapter worker. The optional Whisper runtime still requires a host
provided Python environment, dependencies and model, so these artifacts must
not be presented as a complete, production-ready runtime.

=> M7 optional bridge boundary

M7 adds `runtime/openai_whisper_bridge.py` as an optional JSON-lines bridge for
an OpenAI Whisper host integration. The native OBS adapter keeps the bridge
process and its runner on the host-owned worker side; the OBS audio callback
never launches a process or performs inference. The repository does not bundle
Python, OpenAI Whisper, PyTorch, NumPy, model weights, a Python environment or
a process launcher. The portable core requires an injected host runner and does
not create a bridge process itself.

No M7 packaging decision may imply that the bridge is runnable on an end-user
system without that host setup. A later release gate must define the supported
Python/runtime strategy, dependency licenses and notices, platform process
lifecycle, model-cache
location, package contents and the failure/pass-through behavior when the
optional runtime is absent. Until then, staged packages must continue to
exclude model weights, caches, credentials and undeclared runtime dependencies.

=> Optional native OBS package lane

The normal `main` push package lane remains OBS-independent and builds the
portable plugin stub. This keeps deterministic CI green when no OBS development
environment is available. A deliberate native run can be requested through the
`native_obs` boolean input on `workflow_dispatch` or `workflow_call`; the
release workflow enables this input automatically for its Linux and Windows
payloads.

The native Linux and Windows lane checks out the pinned OBS Studio `32.1.2`
source tag with its recursive submodules, lets the OBS CMake preset fetch its
verified build dependencies, builds only `libobs`, and uses the resulting
headers and library as an explicit `OBS_SDK_DIR`/`OBS_LIB` pair for this
project. OBS binaries are not copied into the WhisperBleep packages; the
target OBS installation remains the host for `libobs` and its dependencies.

The macOS job deliberately remains the unsigned universal portable archive.
Hosted macOS runners do not provide the Apple SDK/signing toolchain required by
the current OBS source, and macOS native packaging is outside this unsigned
installer lane.

Native staging uses the host plugin locations: `obs-plugins/64bit` on Windows,
`lib/x86_64-linux-gnu/obs-plugins` on the Ubuntu package lane and `obs-plugins`
on macOS. The native input is intentionally opt-in: if the pinned OBS build or
native module fails, that run fails visibly rather than silently publishing a
stub under a native package name. The default stub artifacts remain available
for deterministic fallback and boundary testing. The macOS release payload
remains the unsigned universal archive described above.

=> Runtime container publication

The release workflow also publishes a Linux x86_64 CPU runtime image to
`ghcr.io/sythos/obs-whisperbleep-runtime`. The image contains Python 3.11,
OpenAI Whisper, the CPU-only PyTorch wheel, FFmpeg and the JSON-lines bridge;
it contains no model weights and does not replace the native OBS plugin. The
immutable version tag and `latest` are pushed only after the installer package
lane succeeds.
