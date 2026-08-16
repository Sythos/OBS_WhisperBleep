<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Models and cache

M3 does not download, convert or bundle Whisper weights into the repository.
The catalog records the six required OpenAI Whisper identifiers, official source
URLs, the upstream manifest version, the PyTorch checkpoint format, the MIT
model license and the SHA-256 values published in the upstream manifest.

The model cache is per-user and platform-resolved: Windows uses `LOCALAPPDATA`,
macOS uses `~/Library/Caches`, and Linux follows `XDG_CACHE_HOME` or
`~/.cache`. A cache path is accepted only when it is absolute; if resolution
fails, selection stops before any downloader call. The downloader writes to a
temporary file, verifies the catalog checksum before publication, and preserves
a previously valid destination when replacement fails.

The portable default transport supports local `file://` sources for deterministic
tests. HTTPS transfers must be supplied through an explicit platform transport
adapter; no network operation is performed in the OBS realtime callback. The
manager-owned asynchronous worker exposes downloading, verification and
activation states, keeps the active model unchanged until strict file
verification and optional runtime activation succeed, and restores the previous
runtime when a replacement activation fails. Worker shutdown propagates a
cancellation token to cooperative download transports before joining. It
supports rollback plus retain-previous or selected-only retention.
