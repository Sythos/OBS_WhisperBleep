<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Packaging

The project uses CMake/CPack and installs the core library, the plugin stub,
headers, documentation and license notices. M0 packages contain no Whisper
weights, caches, credentials or unverified audio assets.

The future release workflow must create Windows x64, Linux x86_64 and macOS
universal archives only for a new version tag, after the full matrix has passed
build and test.

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

A successful CPack staging check does not mean that a public installer or
release is ready. The native Whisper runtime, complete OBS/runtime dependency
bundling, Windows/Linux CUDA distribution decisions, macOS signing and
notarization, and tag-gated GitHub binary publication remain deferred to the
next release milestone.

=> M7 optional bridge boundary

M7 adds `runtime/openai_whisper_bridge.py` as an optional JSON-lines bridge for
an OpenAI Whisper host integration. Its presence does not bundle Python,
OpenAI Whisper, PyTorch, NumPy, model weights, a Python environment or a
process launcher. The portable core requires an injected host runner and does
not create a bridge process itself.

No M7 packaging decision may imply that the bridge is runnable on an end-user
system. A later release gate must define the supported Python/runtime strategy,
dependency licenses and notices, platform process lifecycle, model-cache
location, package contents and the failure/pass-through behavior when the
optional runtime is absent. Until then, staged packages must continue to
exclude model weights, caches, credentials and undeclared runtime dependencies.
