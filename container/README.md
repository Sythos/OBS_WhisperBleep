<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> OBS WhisperBleep runtime container

This image packages the optional CPU Python bridge used by OBS WhisperBleep.
It reads the same JSON-lines protocol as the native host adapter and keeps
model weights outside the image.

=> Usage

The bridge reads one JSON request per line from standard input and writes one
JSON response per line to standard output. Mount an existing verified model
cache when running a real transcription session; the image never downloads
weights by itself.

```text
docker run --rm -i \
  -v /absolute/path/to/models:/models:ro \
  ghcr.io/sythos/obs-whisperbleep-runtime:0.1.2
```

The container is CPU-only. The desktop OBS plugin remains a native host
component; this image is intended for a separately managed bridge process,
headless verification and Linux x86_64 host deployments.

=> Publication

Normal version tags publish both the immutable version tag and `latest` to:

`ghcr.io/sythos/obs-whisperbleep-runtime`

The release workflow publishes the container after the installer package lane
has completed successfully.
