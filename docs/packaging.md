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
