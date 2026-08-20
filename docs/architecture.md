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

==> Future plugin menu workflow

The later dynamic Properties surface shall use a two-pane plugin menu: a
vertical section navigation on the left and a wide contextual pane on the
right. Every initial opening selects the first left-hand item and displays its
content in the contextual pane. A general or about section shall expose a
`Check Updates` action that compares the canonical installed version with the
latest release at
`https://github.com/Sythos/OBS_WhisperBleep/releases`. If a newer release is
available, the UI shows a popup with the release information and offers to
open that URL in the external browser; it does not silently download or install
the update.

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

The beep is the default replacement for matched blacklist phrases. It is
generated on demand from the requested sample rate and channel count, so the
default path does not read or download an audio asset. Asset-backed choices
remain registered through `ReplacementCatalog`; when one is missing or invalid,
`ReplacementRenderer` leaves the input unchanged as the safe pass-through
fallback rather than replacing the interval with silence.

=> M3 model catalog and cache boundary

M3 records the official OpenAI Whisper model manifest without storing model
weights in Git. The catalog exposes the six multilingual sizes and the four
official English-only variants (`tiny.en`, `base.en`, `small.en` and
`medium.en`). Each descriptor carries its canonical selector identifier,
approved source URL, upstream version metadata, checkpoint format, MIT model
license, SHA-256 checksum and an explicit `english_only` flag. The platform
layer resolves a per-user cache directory rather than a repository-relative
path.

`ModelDownloader` publishes only files that pass checksum and optional size
checks. It uses a temporary file and rename flow, supports cancellation, and
accepts an explicit HTTPS transport adapter while keeping deterministic local
`file://` tests available. `ModelManager` serializes selection, exposes pending
and active states, verifies and optionally activates a model before replacing the
previous active record, and supports rollback plus retention policy choices. Its
manager-owned asynchronous selection worker keeps download, verification and
runtime activation outside the realtime callback. A missing or non-absolute
per-user cache path fails before the downloader is invoked. Worker shutdown
signals the downloader cancellation token before joining, so cooperative
transports can terminate without leaving an in-flight transfer behind.

=> M4 matching, replacement selection and UI contract

M4 turns recognized text and timestamps into a configurable matching decision.
The default phrase policy is case-insensitive, collapses runs of whitespace and
discards empty or duplicate entries. The matcher remains a worker-side/core
operation; it must not allocate, read files or wait in the OBS audio callback.
Its results are associated with the transcription timeline so the scheduler can
produce bounded replacement intervals. Punctuation, word boundaries and
partial-match behavior are explicit policy settings and must be covered by
deterministic tests before they are exposed as user options.

Replacement selection has stable identifiers for the synthetic beep, the duck,
the bark and a user-provided custom audio file. Prepackaged duck and bark assets
must be sourced from audio that is fully royalty-free and legally redistributable
before they enter `assets/`. Custom files are checked and decoded on a worker;
missing or unsupported files produce a visible status and preserve pass-through
audio rather than blocking OBS.

The dynamic Properties surface is a two-pane workflow: a vertical section menu
on the left and a wide contextual pane on the right. Opening the menu always
selects the first left-hand item and renders its content in the right pane. A
`Check Updates` action belongs in the general or about section. It compares the
canonical installed project version with the latest release at
`https://github.com/Sythos/OBS_WhisperBleep/releases`; when a newer release is
found, the UI presents a popup with the release information and an explicit
external-browser action. The plugin never silently installs an update, and the
check is performed away from the realtime audio path.

=> M5 backend, timeline and asset boundary

M5 keeps backend selection explicit and capability-driven. `CPU` is the
portable fallback, `Auto` selects the best validated capability reported by
the host, and `CUDA 13.2` is selectable only when the Windows or Linux build,
driver, runtime and model integration report that capability. An unavailable
request falls back to CPU with a readable state rather than blocking OBS.
macOS is a non-CUDA target for this phase, so `Auto` must choose a validated
non-CUDA backend there.

The Models pane must explain the GPU memory trade-off: VRAM has to contain both
the Whisper model and the game or application that OBS is streaming. The UI
must discourage models that are too large for the remaining VRAM and should
recommend `CPU` when a reasonably recent mid-range CPU can perform the work,
leaving VRAM available to the streamed application.

The timeline bridge carries captured frames, transcript segments and
replacement intervals in one absolute frame domain. It converts validated
chunk-relative results before scheduling. The timestamp coordinator owns the
configured delay and applies it exactly once; the bridge handles chunk anchors,
discontinuity generations, unsafe boundaries and invalid values. It does not
invent word-level timestamps; that precision belongs to the real Whisper
runtime.

Replacement audio is loaded and decoded on a worker before rendering. The M5
deterministic asset boundary validates WAV input and rejects malformed,
unsupported or unreadable files while retaining pass-through behavior. No file
I/O or network access is allowed in the OBS realtime callback. Audio downloaded
from the Internet may enter the repository or a package only when its source,
author, license reference, download date and checksum are recorded and the
license is explicitly 100% royalty-free and redistributable.

M5 also defines CPack staging for Windows x64, Linux x86_64 and macOS universal
layouts. Staging contains code, documentation, license notices and verified
assets only; it excludes model weights, user caches, credentials, unverified
audio and undeclared runtime dependencies. Staging is not a release: runtime
bundling, CUDA redistribution, macOS signing/notarization and tag-gated
publication remain open.

=> M6 localization boundary

The UI localization contract is shared by the deterministic Properties stub
and the native OBS adapter. The selector exposes the 67 locale identifiers from
the pinned OBS Studio reference, with `en-US` as the default and `it-IT` as the
first translated catalog. Empty, unknown or malformed locale values resolve to
`en-US`, and every missing translation falls back to its English text. The
language selector is persisted as the `language` filter setting and is exposed
as a stable string dropdown.

The native adapter uses the selected locale when it constructs the Properties
surface. OBS may need to reopen the Properties surface before labels are
re-rendered after a language change; dynamic in-place Qt relayout is outside
the current OBS boundary and is not claimed by this milestone. The
dependency-free M6 test validates locale resolution, translation fallback,
option ordering, visible menu/update labels and the stub Properties contract
without requiring an OBS SDK. The `Debug` setting is disabled by default and
is persisted with the filter settings; when enabled, the native adapter opens
an exclusive `WhisperBleep_yyyymmdd_xxx.log` file below the OS user home and
writes only sanitized lifecycle/settings events outside the realtime callback.

The runtime boundary now validates the selected model's language scope before
creating an adapter. Multilingual models accept `auto` or an explicit language
tag; English-only `.en` models accept only an English tag and pass the
normalized `en` policy to the adapter. The dependency-free latency evaluator
uses a monotonic clock, records the ingress, processing and output stages,
accepts three 500 ms audio delays as the 1.5-second baseline and reports an
explicit 2.0-second video-delay reassessment instead of applying it silently.

=> Deferred milestones

The real Whisper inference runtime and Python/PyTorch integration remain later
milestones. M5 validates the backend and platform boundary but does not claim
production CUDA execution, model-weight redistribution or a finished installer.
The repository continues to keep model weights outside Git and outside release
staging.
