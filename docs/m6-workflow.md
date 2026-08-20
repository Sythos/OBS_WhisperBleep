<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> M6 subagent workflow

This document is the execution contract for the M6 workstreams that cover:

1. internationalization/localization (i18n/l10n), including the language
   dropdown;
2. an English-only recognition policy for Whisper models; and
3. the dependency-free synthetic beep as the default replacement.

It also defines the cross-cutting M6 hardening gates for end-to-end latency,
the `Debug` diagnostic log, CI, independent review and release hygiene.

This is an operational plan for the project `main` branch. Product code and
tests may be changed by the assigned workstreams, but no model weights may be
created and no subagent may commit or push changes.

=> 1. Source of truth and non-negotiable invariants

The parent project brief is authoritative for M6. In particular, the following
requirements are blocking gates:

- the complete OBS ingress -> buffering -> Whisper -> phrase matching ->
  replacement rendering -> processed-output path has a 1.5-second end-to-end
  budget;
- validation starts with three consecutive 500 ms audio delays (approximately
  1.5 seconds total); if that is insufficient, the video delay must be
  explicitly reassessed and may be raised to 2.0 seconds with the decision and
  evidence recorded;
- `Debug` is disabled by default and, when enabled, writes a diagnostic text
  log below the OS-resolved user home using
  `WhisperBleep_yyyymmdd_xxx.log`;
- `yyyymmdd` is the compact ISO 8601 calendar date, `xxx` is a zero-padded
  progressive daily sequence, and an existing log must never be overwritten;
- no model weights, user caches, credentials or unverified audio may enter Git,
  source archives, staging or release artifacts;
- the OBS realtime callback must not perform inference, download, file I/O,
  unbounded allocation, blocking waits or slow logging;
- uncertain recognition or unavailable assets must preserve deterministic
  pass-through behavior; the plugin must not guess silently.

The M6 plan must also preserve stable replacement identifiers, the existing
OBS-independent CMake/test boundary and Sythos-only project attribution.

=> 2. Operating model

The parent agent owns the `main` integration branch and the final decision. Three
implementation agents work in parallel on disjoint areas, then a separate
review agent performs a read-only review of the integrated diff.

| Role | Owns | Must not own |
| --- | --- | --- |
| Agent A: i18n/l10n | Message IDs, locale catalogs, locale resolution, language dropdown, UI translation tests | Model policy or audio generation |
| Agent B: English models | Catalog policy, manifest evidence, runtime language restriction, model-selection tests | General UI translation or replacement rendering |
| Agent C: synthetic beep | Default replacement setting, deterministic beep contract, replacement/default tests | Locale catalogs or model manifests |
| Parent/integrator | Shared contracts, cross-workstream wiring, latency/Debug integration, CI and final acceptance | Delegating overlapping edits without an explicit merge decision |
| Independent reviewer | Read-only review, regression/risk assessment and gate checklist | Approving its own implementation or pushing changes |

Subagents must not commit or push. Each agent reports the changed paths, tests
run, evidence produced, unresolved decisions and any files that the parent must
merge manually. Avoid parallel edits to shared files such as `CMakeLists.txt`,
`CMakePresets.json`, README files and OBS property registration; the parent owns
those integrations.

==> Dependency graph

```text
M6 decisions and existing contracts
          |
          +--> Agent A: i18n/l10n + language dropdown --+
          |                                               |
          +--> Agent B: English model policy ------------+--> parent integration
          |                                               |         |
          +--> Agent C: synthetic default beep ----------+          v
          |                                          independent review
          +--> latency/Debug test plan -----------------------+    |
                                                               v    v
                                                    full CI matrix and packaging
                                                               |
                                                     green-gate commit decision
                                                               |
                                                push only after explicit approval
```

Agent A and Agent B share translated model labels and therefore must exchange
message-ID and catalog contracts before either hard-codes user-visible text.
Agent C only needs the stable replacement ID (`beep`) and the existing audio
buffer/rendering contract. The latency and Debug gates must be exercised only
after all three workstreams are wired into the same settings and processing
path.

=> 3. Decisions required before implementation

The parent records these decisions in the task handoff before agents start.
They are dependencies, not details that an agent may infer silently.

==> 3.1 Locale contract

Recommended M6 baseline:

- `en-US` is the canonical source locale and the default;
- the dropdown exposes the complete locale identifier set supported by the
  pinned OBS Studio reference;
- `it-IT` has the first translated catalog; every other locale currently uses
  the English source catalog until a complete translation is approved;
- unknown or incomplete locale data falls back per message key to `en-US`;
- locale identifiers use normalized BCP-47 spelling and are persisted in the
  plugin settings, not in process-global mutable state.

If the pinned OBS Studio reference changes its locale set, the parent records
the new evidence before Agent A updates the dropdown. A locale entry must
remain usable through the explicit English fallback even when its translation
catalog is not yet complete.

==> 3.2 Meaning of English-only models

"English-only" must be an enforceable runtime policy, not an English-looking
label. Before Agent B edits the catalog, the parent chooses and records one of
these compatible policies:

1. **Strict artifact policy (recommended when English-only weights are
   required):** expose only upstream artifacts whose manifest and source prove
   English-only support. Preserve an explicit `language_scope=english_only`
   field and hide/reject every other artifact.
2. **English-pinned runtime policy:** retain an official multilingual artifact
   only when the selected runtime can enforce `language=en` and the UI labels
   it as multilingual-with-English-lock, never as an English-only weight.

The existing catalog uses stable family IDs (`tiny`, `base`, `small`, `medium`,
`large`, `turbo`) and the parent brief expects those IDs to be validated. The
strict policy may therefore require a brief-level decision for families that do
not publish a matching English-only artifact. No agent may append `.en`, rename
an ID or claim a checksum without source evidence. Model weights remain outside
Git and all deterministic tests use fixtures or injected transports.

Implementation decision for `main`: use the strict artifact policy. The
four upstream `.en` checkpoints are selectable and carry explicit
`english_only` metadata; `large` and `turbo` remain multilingual because no
official `.en` artifacts are published. The runtime rejects incoherent
language requests before an adapter is created and normalizes accepted English
tags to `en`.

==> 3.3 Beep defaults

The stable replacement ID is `beep`. The parent records the default sample-rate
independent options (frequency, amplitude, phase and any edge fade) before
Agent C changes a setting contract. Existing `BeepOptions` defaults are the
starting point; changing them requires an audible/regression rationale. The
default choice is generated locally and must not require an audio asset.

=> 4. Workstream A: i18n/l10n and language dropdown

==> Scope and contract

Agent A should:

- inventory every user-visible string in OBS Properties, the two-pane menu,
  model/replacement status, update messages and diagnostics that are presented
  to users;
- replace ad-hoc display strings with stable message IDs and an English source
  catalog;
- add approved locale catalogs with the same key set and explicit metadata;
- resolve the selected locale deterministically, falling back to `en-US` per
  missing key and never throwing from the audio path;
- expose a language dropdown in the existing UI, with the canonical locale
  selected by default and the selection persisted through the normal OBS
  settings lifecycle;
- keep catalog loading/validation outside the realtime callback; a malformed
  or missing catalog must leave the UI usable in English;
- cover model labels, replacement labels and error/status text supplied by
  Agents B and C through message IDs rather than translated string literals.

The dropdown must not imply that a locale is supported unless its catalog is
complete enough for the declared M6 surface. A locale change may refresh the
Properties/menu immediately or on the next open, but the chosen behavior must
be documented and tested.

==> Dependencies and handoff

Agent A needs the locale decision, the UI string inventory and the stable model
and replacement IDs. Agent A publishes:

- the message-ID schema and catalog validation rule;
- the list of supported locales and fallback behavior;
- the settings key and migration behavior for an existing installation;
- deterministic tests for default, switch, persistence, missing-key and
  malformed-catalog cases.

==> Workstream-A gate

The workstream is ready for integration only when:

- a fresh instance selects `en-US` (or the recorded canonical locale);
- the dropdown contains the complete approved OBS locale set and persists its
  value;
- every visible M6 string has a stable ID and an English fallback;
- missing translations are visibly English rather than blank, stale or mixed
  with raw message IDs;
- locale resolution performs no file I/O, network operation, allocation-heavy
  work or blocking wait in the audio callback;
- automated tests cover the dropdown, persistence, fallback, catalog errors and
  every supported locale identifier.

=> 5. Workstream B: English-only model policy

==> Scope and contract

Agent B should:

- encode the approved English-only policy in the model descriptor/manifest
  rather than relying on a display-name suffix;
- verify each source URL, artifact format, expected size where available,
  checksum, upstream version and language scope before exposing a model;
- keep stable IDs and add an explicit language-scope field or equivalent
  runtime contract;
- pass the English language restriction through model initialization and
  transcription requests when the selected policy is English-pinned;
- reject a catalog entry that lacks language evidence, has a mismatched
  checksum/format or would be mislabeled as English-only;
- preserve the active model and pass-through behavior on failed download,
  verification, language-policy or activation transitions;
- provide UI-ready status/error message IDs to Agent A without adding ad-hoc
  translated strings.

No test or build step may download model weights from the network. Use checked-in
metadata and small deterministic fixtures only; a manifest URL is evidence, not
permission to place weights in the repository or package.

==> Dependencies and handoff

Agent B needs the recorded model policy, approved upstream manifest evidence,
the runtime adapter's language parameter and the stable catalog IDs. Agent B
publishes:

- a manifest/schema decision for language scope and compatibility;
- source/checksum evidence for every visible entry;
- runtime tests proving that a non-English request is rejected or normalized to
  the recorded English policy;
- catalog and UI-selection tests for unsupported/ambiguous entries;
- a package-scan result proving that no model weights or caches were produced.

==> Workstream-B gate

The workstream is ready for integration only when:

- every selectable model is either proven English-only or explicitly classified
  as multilingual with an enforced English lock under the recorded policy;
- no multilingual artifact is silently relabeled as an English-only model;
- the language restriction reaches the runtime boundary and is covered by a
  deterministic test;
- source metadata and SHA-256 verification are complete and stable;
- a failed or unavailable model cannot block OBS and does not discard the
  previously active valid model;
- model weights, caches and credentials are absent from Git, CI artifacts,
  CPack staging and source archives.

=> 6. Workstream C: synthetic beep as the default replacement

==> Scope and contract

Agent C should:

- preserve the stable `beep` replacement identifier and make it the default for
  a new filter instance and for a missing/invalid persisted replacement value;
- generate the beep through the existing dependency-free synthetic path;
- keep the default frequency, amplitude, phase, channel mapping, duration and
  optional fade deterministic and documented;
- ensure the generated buffer matches the requested sample rate, channel count
  and frame count without clipping or channel misalignment;
- ensure replacement rendering loops/trims/fades the beep without changing the
  input buffer shape or introducing an audible click in the existing tests;
- leave custom/duck/bark asset validation and decoding off the realtime path;
- keep the existing safe pass-through behavior for missing, corrupt or
  unsupported non-synthetic assets. An asset failure must not cause hidden I/O
  or an unannounced replacement-policy change.

==> Dependencies and handoff

Agent C needs the stable replacement catalog, current `BeepOptions`, renderer
contract and OBS setting persistence behavior. Agent C publishes:

- the default-setting and migration decision;
- deterministic generation/rendering tests for mono/stereo and short/long
  intervals;
- a test proving no beep asset, download or filesystem access is required;
- readable status behavior for invalid non-beep replacements.

==> Workstream-C gate

The workstream is ready for integration only when:

- a fresh filter reports `beep` as its persisted/default replacement;
- the same inputs produce the same samples and bounded amplitude;
- all requested frame counts and channels are handled safely;
- the default path works with no model weights, audio files, network or runtime
  adapter;
- the OBS callback performs no generation that can allocate, block or touch the
  filesystem; generation/rendering is bounded and prepared at the correct
  boundary;
- deterministic tests cover defaults, setting migration, duration, clipping,
  channel mapping, missing assets and pass-through.

=> 7. Cross-cutting M6 hardening gates

==> 7.1 End-to-end latency budget

The parent owns the integrated measurement. Instrumentation must use a monotonic
clock and correlate one frame/chunk through these milestones:

```text
capture ingress
  -> buffered/accepted
  -> inference start/end
  -> phrase match
  -> replacement plan
  -> processed output
```

The reported end-to-end value is processed-output time minus capture-ingress
time. It must include queue wait, preprocessing, inference, matching,
scheduling and rendering. Report the workload, sample rate, channels, model,
backend, warm/cold state, queue drops, p50, p95 and maximum; do not report only
the fastest successful sample.

The validation sequence is:

1. begin with three consecutive 500 ms audio delays, for approximately 1.5 s;
2. exercise the reference workload and confirm that the complete operation
   finishes before the processed audio is emitted;
3. compare the measured distribution with the 1.5-second target and investigate
   queue drops, underruns, inference stalls and replacement-boundary errors;
4. if 1.5 s is insufficient, stop the gate and write an explicit video-delay
   reassessment: reason, measured evidence, affected synchronization trade-off,
   updated settings/docs/tests and the decision to raise the total to 2.0 s;
5. never silently change the target or hide an over-budget run by changing the
   measurement window.

The latency gate is blocked when the workload, metric, clock domain or
reference hardware is not recorded. The final report must state whether the
1.5-second target passed or whether the documented 2.0-second reassessment was
approved.

==> 7.2 `Debug` diagnostic logging

The settings contract must define a `Debug` option whose default is disabled.
When disabled, it must not create a diagnostic file. When enabled:

- Windows resolves the base directory from `%USERPROFILE%`; Linux/macOS use
  `$HOME`, with platform-specific APIs preferred over string concatenation;
- the filename is exactly `WhisperBleep_yyyymmdd_xxx.log`;
- `yyyymmdd` is the compact ISO 8601 calendar date and `xxx` is the next unused
  zero-padded daily sequence, starting at `001`;
- creation uses an exclusive/no-overwrite operation (for example,
  `CREATE_NEW` on Windows or `O_CREAT|O_EXCL` on POSIX), retrying the next
  sequence if a race is detected;
- existing files are never truncated or overwritten, including after restart;
- the clock and home-directory resolver are injectable in tests so all three
  platform rules and collision cases are deterministic;
- logging is queued or performed by a worker and never blocks the realtime
  callback; queue overflow is counted and surfaced as a diagnostic condition;
- entries contain useful states, model/backend transitions, timing, queue and
  fallback information, but no raw audio or unnecessary sensitive transcript
  data;
- an unwritable home/path produces a readable status and preserves audio
  pass-through rather than crashing or blocking OBS.

The logging gate requires tests for default-off behavior, Windows/Linux/macOS
path resolution, date formatting, first and subsequent daily sequence numbers,
collision/race handling, restart behavior, redaction and callback safety.

=> 8. CI and verification matrix

The existing GitHub Actions build/test workflow is the baseline. M6 must keep
the matrix green for Windows x64 (`windows-2022`), Linux x86_64 (`ubuntu-24.04`)
and macOS universal (`macos-14`). CI must remain deterministic and must not
require OBS SDK installation, CUDA, network access or Whisper weights for the
core test path.

Required checks:

| Check | Required evidence | Blocking rule |
| --- | --- | --- |
| Configure/build | CMake configure and C++20 build for all three runners | Any compiler/configuration failure blocks merge |
| Existing tests | `ctest --test-dir build --output-on-failure` (or platform-equivalent) | Any failure blocks merge |
| i18n validation | Duplicate/missing IDs, complete approved catalogs, fallback and persistence tests | Any catalog or untranslated-required-key failure blocks merge |
| Model policy | Manifest schema, source/checksum evidence, language restriction and no-weight scan | Any ambiguous entry, checksum mismatch or weight/cache artifact blocks merge |
| Replacement/default | Synthetic beep determinism, defaults, duration, channel and pass-through tests | Any regression in audio safety or default selection blocks merge |
| Realtime safety | Queue/back-pressure, callback boundary and stress/regression tests | Any callback I/O, blocking wait, drop corruption or unsafe shutdown blocks merge |
| Latency | Versioned measurement artifact with workload, p50/p95/max and budget decision | Missing evidence, unexplained over-budget result or silent target change blocks merge |
| Debug logging | Cross-platform path/sequence/no-overwrite and redaction tests | Any overwrite, default-on file or callback logging blocks merge |
| Package scan | CPack staging/source archive scan for weights, caches, credentials and unverified assets | Any forbidden artifact blocks merge |

The CI workflow should upload only useful, non-sensitive evidence (test output,
manifest validation and sanitized latency summaries). Never upload model
weights, user logs, credentials, raw audio or unredacted transcripts. Release
packaging remains tag-gated and must consume only artifacts from a fully green
matrix.

=> 9. Independent review protocol

After the parent integrates all three workstreams, a reviewer that did not write
the implementation performs a read-only review in this order:

1. inspect the complete diff and `git diff --check` output;
2. trace the settings path from OBS Properties through persistence to runtime;
3. verify every visible locale/model/replacement label has a stable contract;
4. trace the realtime callback for I/O, allocations, locks, inference and slow
   logging;
5. check model-language evidence, no-weight boundaries and failure fallback;
6. verify beep defaults, generated-buffer bounds and pass-through behavior;
7. inspect latency instrumentation for a complete monotonic path and the
   1.5 -> 2.0 s reassessment rule;
8. inspect Debug path resolution, sequence collision handling and redaction;
9. rerun or independently reproduce the relevant tests and record blockers.

The reviewer returns a checklist with `pass`, `fail` or `needs decision` for
each gate. The parent resolves every blocker or records an explicit deferral;
"looks reasonable" is not an acceptance result.

=> 10. Definition of done and commit/push gate

M6 is ready for a commit only when all of the following are true:

- all three workstream gates pass;
- the latency report passes the 1.5-second target or contains the approved,
  evidence-backed 2.0-second video-delay reassessment;
- Debug logging passes default-off, OS-path, progressive-sequence,
  no-overwrite and callback-safety tests;
- the independent review has no unresolved blocking finding;
- the complete Windows/Linux/macOS CI matrix, package scan and documentation
  checks are green;
- the parent has inspected the final staged scope and confirmed that only the
  intended M6 files are included.

The parent then performs the repository hygiene checks, including attribution,
SPDX headers, `git diff --check` and the required Sythos Git identity. No agent
creates a commit or pushes while a check is pending or red. A push requires a
separate, explicit authorization after the green result; this planning task
does not authorize either operation.

=> 10.1 Current implementation tranche

The implementation workstreams on `main` now cover the locale contract,
the complete pinned OBS locale selector with English fallback, the native and
stub language dropdown, all visible menu/property/update labels, the strict
English-only model metadata and runtime policy, the synthetic beep default, the
safe asset pass-through, the Debug flag/log writer and the deterministic
latency evaluator. The implementation commit is published on `main` with
green Linux, macOS and Windows CI. The remaining M6 hardening gates are
package scanning and end-to-end latency evidence through a real Whisper
adapter; the portable runtime remains an injected boundary until that later
runtime milestone is implemented.

=> 11. Acceptance checklist

==> Localization

- [x] Canonical locale and supported locale list are recorded.
- [x] Language dropdown is visible, deterministic, persisted and accessible.
- [x] English fallback works per key for missing/malformed translations.
- [x] All visible M6 UI/status strings use stable IDs and have tests.
- [x] Locale loading and switching do not touch the realtime audio callback.

==> English-only models

- [x] The strict artifact policy is recorded before catalog edits.
- [x] Every visible model has source, format, checksum and language-scope evidence.
- [x] No multilingual model is mislabeled as an English-only artifact.
- [x] Runtime enforcement and unsupported-language tests pass.
- [x] No weights, caches or credentials appear in Git, CI artifacts or packages.

==> Synthetic default beep

- [x] New and migrated settings select the stable `beep` identifier by default.
- [x] Beep generation is local, deterministic, bounded and asset-free.
- [x] Duration, channels, amplitude, fade and pass-through tests pass.
- [x] Non-beep asset failures remain readable and safe without hidden I/O.

==> M6 hardening and release

- [ ] End-to-end latency evidence covers the complete path and the 1.5-second
      target.
- [ ] If needed, the 2.0-second video-delay reassessment is explicit and
      approved rather than implicit.
- [x] Debug is disabled by default and logs use OS home resolution, the exact
      filename pattern, progressive daily numbering and exclusive creation.
- [ ] Stress and long-running regression tests are complete.
- [x] The Linux, macOS and Windows CI matrix is green for the published M6
      implementation commit.
- [ ] CPack/source-archive scans exclude weights, caches, credentials and
      unverified assets.
- [x] Independent review of the published implementation commit is complete.
- [x] The implementation commit and explicit push use the Sythos identity on
      `main`.
