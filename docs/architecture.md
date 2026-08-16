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

=> M2 deterministic audio pipeline

M2 adds a bounded, dependency-free audio path that can be exercised before a
Whisper runtime is connected. The OBS-facing producer submits frames to an
`AudioFrameQueue`, implemented as a single-producer/single-consumer bounded
queue. Submission never waits; when the queue is full it drops the newest
frame and exposes a counter for diagnostics. This keeps back-pressure from
blocking the realtime OBS callback.

`AudioProcessingWorker` drains the queue on a dedicated worker thread and
passes accepted frames to the processing callback. Its shutdown sequence
requests a stop, drains frames already accepted by the queue, and only then
joins the worker. The worker is therefore the place for future preprocessing
and inference work; the callback remains free of model, disk and network
operations.

==> Simulated speech timestamps

M2 uses the Whisper-independent `SpeechSegment` structure for deterministic
tests. `TimestampCoordinator` converts segment start/end seconds to sample
frames at a configured sample rate and applies a configurable non-negative
delay. Invalid or non-finite segments are ignored. The resulting intervals
are passed to `CensorScheduler`, which sorts them and merges overlapping or
touching intervals deterministically.

==> Replacement rendering

`SyntheticReplacement` supplies a deterministic sine-wave beep without an
external asset or runtime dependency. `ReplacementRenderer` applies the
scheduled intervals while preserving the input buffer shape. It loops a short
replacement across a longer interval, trims it at the interval boundary, and
maps channels deterministically; an optional fade reduces replacement edges
and can be configured in frames. Signed intervals are clamped to the input
buffer before index conversion; intervals ending at or before frame zero are
ignored safely. This is the M2 duration and overlap policy.

=> Deferred milestones

Whisper inference, model downloads and cache management, Python/PyTorch
execution, and the CUDA backend remain later milestones. M2 deliberately uses
simulated speech segments and a synthetic beep; it does not download or bundle
Whisper models. M3 will connect the isolated runtime interface, verified model
catalog and asynchronous model lifecycle.
