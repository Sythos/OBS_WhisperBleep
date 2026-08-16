<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Architecture

OBS WhisperBleep is separated into three areas from the beginning:

- `src/core/`: matching, timestamps, pipeline state and replacement planning;
- `src/model/` and `src/runtime/`: catalog, model lifecycle and the abstract Whisper runtime interface;
- `src/obs/` and `src/platform/`: OBS integration points and operating-system integration.

=> M0 baseline

The `obs_whisperbleep_plugin_stub` target is an OBS-independent compilation
target. It exercises the initial filter and property interfaces without loading
the OBS API or running inference. The M0 contract is deterministic: tests
require no OBS installation, GPU, network access or Whisper weights.

=> M1 native OBS integration

M1 connects the filter to the native OBS audio-filter API. The integration will
register the plugin filter, create and destroy one filter instance per OBS
source, and forward each incoming audio frame through the existing core audio
pipeline. The OBS-facing layer owns OBS callbacks and settings translation;
the core layer remains independent of OBS types.

==> Initial Properties

The first OBS properties surface contains the following user-facing settings:

- phrases or words to censor;
- Whisper model selection;
- processing backend selection;
- replacement sound selection.

M1 keeps these properties deliberately small. They are translated into the
filter instance settings and are persisted through OBS's settings object so
that reopening a scene or restarting OBS restores the selected values.

==> Pass-through behavior

When the filter is disabled, not yet ready, or has no confirmed censor interval,
the input audio must pass through unchanged. M1 must not drop frames while the
filter is initializing or while a setting is being edited. Replacement audio is
introduced only for intervals that the core pipeline has explicitly scheduled.

==> Lifecycle and persistence test scope

The M1 test scope covers native registration, filter creation, update and
destruction, safe repeated teardown, unchanged pass-through, initial property
defaults, settings updates, and settings persistence across an instance
recreation. Tests must not require Whisper weights, network access or CUDA.

=> Deferred milestones

Whisper inference, model downloads and cache management, Python/PyTorch
execution, and the CUDA backend remain later milestones. M1 establishes the
OBS boundary and deterministic pass-through contract before those runtimes are
connected.
