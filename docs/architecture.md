<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> M0 architecture

OBS WhisperBleep is separated into three areas from the beginning:

- `src/core/`: matching, timestamps, pipeline state and replacement planning;
- `src/model/` and `src/runtime/`: catalog, model lifecycle and the abstract Whisper runtime interface;
- `src/obs/` and `src/platform/`: OBS integration points and operating-system integration.

The `obs_whisperbleep_plugin_stub` target is only a compilable stub: it does not
use the OBS API and does not run inference yet. The realtime pipeline, model
downloads and CUDA backend are reserved for later milestones.

The M0 contract is deterministic: tests require no OBS installation, GPU,
network access or Whisper weights and verify only the initial core interfaces.
