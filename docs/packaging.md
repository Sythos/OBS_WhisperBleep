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
