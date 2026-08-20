<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Models and cache

M3 does not download, convert or bundle Whisper weights into the repository.
The catalog records the six multilingual OpenAI Whisper identifiers plus the
four official English-only variants (`tiny.en`, `base.en`, `small.en` and
`medium.en`). Each entry carries its canonical selector name, official source
URL, upstream manifest version, PyTorch checkpoint format, MIT model license
and the SHA-256 value published in the upstream manifest. The `english_only`
metadata flag makes the language restriction explicit without changing the
canonical upstream name.

The selector accepts the canonical names from the upstream manifest. English-
only checkpoints are offered only for the tiny, base, small and medium sizes;
the large and turbo entries remain multilingual because OpenAI publishes no
`.en` variants for those sizes. The manifest is metadata only: selecting a
model may request an on-demand download later, but no model weights are stored
in Git or downloaded by catalog and metadata tests. Before an adapter is
created, the runtime checks the descriptor's `english_only` flag and rejects a
non-English or automatic-language request for an English-only model; accepted
English tags are normalized to `en`.

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
