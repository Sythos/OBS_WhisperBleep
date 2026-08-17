// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::core {

/**
 * Result of accepting an audio chunk into the absolute timeline.
 *
 * A mismatch is never silently translated. Callers must mark the next chunk
 * as discontinuous after a seek, dropped range, stream restart, or similar
 * timeline break.
 */
enum class TimelineStatus {
  anchored,
  advanced,
  reset,
  invalid_chunk,
  sample_rate_mismatch,
  non_contiguous,
};

[[nodiscard]] const char* timeline_status_name(
    TimelineStatus status) noexcept;

struct AudioTimelineConfig {
  std::uint32_t sample_rate = 48000;
};

/**
 * Absolute metadata for one captured audio chunk.
 *
 * Transcript and censor results are relative to the beginning of this chunk;
 * first_frame is the absolute frame supplied by the host audio path.
 */
struct TimelineChunk {
  std::int64_t first_frame = 0;
  std::int64_t frame_count = 0;
  std::uint32_t sample_rate = 48000;
  bool discontinuity = false;
};

/**
 * An accepted chunk anchor. The generation prevents results from a previous
 * stream epoch being applied after reset or discontinuity.
 */
struct TimelineAnchor {
  std::int64_t first_frame = 0;
  std::int64_t frame_count = 0;
  std::uint32_t sample_rate = 0;
  std::uint64_t generation = 0;
};

struct TimelineUpdate {
  TimelineStatus status = TimelineStatus::invalid_chunk;
  std::uint64_t generation = 0;
  std::optional<TimelineAnchor> anchor;
};

/**
 * Bridges chunk-relative recognition results to the absolute audio frame
 * timeline used by the scheduler and renderer.
 *
 * The coordinator is deliberately independent of OBS and performs no I/O,
 * waiting, or audio processing. It accepts only positive, matching sample
 * rates, rejects overflow and out-of-window results, and requires an explicit
 * discontinuity marker whenever consecutive chunks are not contiguous.
 */
class AudioTimelineCoordinator {
 public:
  explicit AudioTimelineCoordinator(AudioTimelineConfig config = {});

  [[nodiscard]] const AudioTimelineConfig& config() const noexcept;

  /**
   * Accept a chunk and return its anchor when it belongs to the active epoch.
   * The first valid chunk establishes the epoch. A marked discontinuity starts
   * a new epoch and invalidates anchors from the previous one.
   */
  [[nodiscard]] TimelineUpdate accept(const TimelineChunk& chunk) noexcept;

  /** Invalidate all outstanding anchors and require a new first chunk. */
  void reset() noexcept;

  [[nodiscard]] bool anchored() const noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept;

  /** Translate one chunk-relative transcript segment to absolute frames. */
  [[nodiscard]] std::optional<runtime::TranscriptSegment> to_absolute(
      const TimelineAnchor& anchor,
      const runtime::TranscriptSegment& relative) const;

  /** Translate one chunk-relative censor interval to absolute frames. */
  [[nodiscard]] std::optional<CensorInterval> to_absolute(
      const TimelineAnchor& anchor, const CensorInterval& relative) const;

  [[nodiscard]] std::vector<runtime::TranscriptSegment> to_absolute(
      const TimelineAnchor& anchor,
      const std::vector<runtime::TranscriptSegment>& relative) const;

  [[nodiscard]] std::vector<CensorInterval> to_absolute(
      const TimelineAnchor& anchor,
      const std::vector<CensorInterval>& relative) const;

 private:
  [[nodiscard]] bool valid_anchor(const TimelineAnchor& anchor) const noexcept;
  [[nodiscard]] std::optional<CensorInterval> map_interval(
      const TimelineAnchor& anchor, const CensorInterval& relative) const
      noexcept;

  AudioTimelineConfig config_;
  bool anchored_ = false;
  bool requires_discontinuity_ = false;
  std::int64_t next_frame_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace obs_whisperbleep::core
