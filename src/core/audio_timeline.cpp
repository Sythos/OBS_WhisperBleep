// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_timeline.hpp"

#include <limits>
#include <utility>

namespace obs_whisperbleep::core {

namespace {

[[nodiscard]] bool add_overflows(const std::int64_t left,
                                 const std::int64_t right) noexcept {
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  return (right > 0 && left > maximum - right) ||
         (right < 0 && left < minimum - right);
}

[[nodiscard]] std::optional<std::int64_t> add_frames(
    const std::int64_t left, const std::int64_t right) noexcept {
  if (add_overflows(left, right)) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] bool valid_relative_interval(const CensorInterval& interval,
                                            const std::int64_t frame_count)
    noexcept {
  return frame_count > 0 && interval.start_frame >= 0 &&
         interval.start_frame < interval.end_frame &&
         interval.end_frame <= frame_count;
}

[[nodiscard]] std::uint64_t next_generation(
    const std::uint64_t generation) noexcept {
  // Generation zero is reserved for the uninitialized state. Saturating at
  // the maximum keeps reset/discontinuity handling deterministic even after
  // an unrealistically long-running process.
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  return generation == maximum ? maximum : generation + 1;
}

}  // namespace

const char* timeline_status_name(const TimelineStatus status) noexcept {
  switch (status) {
    case TimelineStatus::anchored:
      return "anchored";
    case TimelineStatus::advanced:
      return "advanced";
    case TimelineStatus::reset:
      return "reset";
    case TimelineStatus::invalid_chunk:
      return "invalid_chunk";
    case TimelineStatus::sample_rate_mismatch:
      return "sample_rate_mismatch";
    case TimelineStatus::non_contiguous:
      return "non_contiguous";
  }
  return "unknown";
}

AudioTimelineCoordinator::AudioTimelineCoordinator(AudioTimelineConfig config)
    : config_(config) {
  // Keep the zero-value configuration safe for callers constructing settings
  // from optional host properties. A chunk with rate zero is still rejected.
  if (config_.sample_rate == 0) {
    config_.sample_rate = 48000;
  }
}

const AudioTimelineConfig& AudioTimelineCoordinator::config() const noexcept {
  return config_;
}

TimelineUpdate AudioTimelineCoordinator::accept(
    const TimelineChunk& chunk) noexcept {
  TimelineUpdate update;
  update.generation = generation_;

  if (chunk.first_frame < 0 || chunk.frame_count <= 0) {
    update.status = TimelineStatus::invalid_chunk;
    return update;
  }
  if (chunk.sample_rate == 0 || chunk.sample_rate != config_.sample_rate) {
    update.status = TimelineStatus::sample_rate_mismatch;
    return update;
  }

  const auto chunk_end = add_frames(chunk.first_frame, chunk.frame_count);
  if (!chunk_end.has_value()) {
    update.status = TimelineStatus::invalid_chunk;
    return update;
  }

  if (!anchored_ && requires_discontinuity_ && !chunk.discontinuity) {
    update.status = TimelineStatus::non_contiguous;
    return update;
  }

  const bool first_chunk = !anchored_;
  const bool explicit_reset = chunk.discontinuity && anchored_;
  if (!first_chunk && !explicit_reset && chunk.first_frame != next_frame_) {
    // Do not guess where a dropped/seeked range belongs. The next accepted
    // chunk must carry discontinuity=true and starts a fresh epoch.
    anchored_ = false;
    requires_discontinuity_ = true;
    next_frame_ = 0;
    generation_ = next_generation(generation_);
    update.status = TimelineStatus::non_contiguous;
    update.generation = generation_;
    return update;
  }

  if (first_chunk || explicit_reset || chunk.discontinuity) {
    generation_ = next_generation(generation_);
  }
  const auto anchor_generation = generation_;
  const TimelineAnchor anchor{chunk.first_frame, chunk.frame_count,
                              chunk.sample_rate, anchor_generation};
  anchored_ = true;
  requires_discontinuity_ = false;
  next_frame_ = *chunk_end;

  update.status = first_chunk ? TimelineStatus::anchored
                              : ((explicit_reset || chunk.discontinuity)
                                     ? TimelineStatus::reset
                                     : TimelineStatus::advanced);
  update.generation = anchor_generation;
  update.anchor = anchor;
  return update;
}

void AudioTimelineCoordinator::reset() noexcept {
  anchored_ = false;
  requires_discontinuity_ = false;
  next_frame_ = 0;
  generation_ = next_generation(generation_);
}

bool AudioTimelineCoordinator::anchored() const noexcept { return anchored_; }

std::uint64_t AudioTimelineCoordinator::generation() const noexcept {
  return generation_;
}

bool AudioTimelineCoordinator::valid_anchor(
    const TimelineAnchor& anchor) const noexcept {
  return anchored_ && anchor.generation == generation_ &&
         anchor.sample_rate == config_.sample_rate &&
         anchor.first_frame >= 0 && anchor.frame_count > 0 &&
         !add_overflows(anchor.first_frame, anchor.frame_count);
}

std::optional<CensorInterval> AudioTimelineCoordinator::map_interval(
    const TimelineAnchor& anchor, const CensorInterval& relative) const
    noexcept {
  if (!valid_anchor(anchor) ||
      !valid_relative_interval(relative, anchor.frame_count)) {
    return std::nullopt;
  }

  const auto absolute_start = add_frames(anchor.first_frame,
                                         relative.start_frame);
  const auto absolute_end =
      add_frames(anchor.first_frame, relative.end_frame);
  if (!absolute_start.has_value() || !absolute_end.has_value() ||
      *absolute_start < 0 || *absolute_end <= *absolute_start) {
    return std::nullopt;
  }
  return CensorInterval{*absolute_start, *absolute_end};
}

std::optional<runtime::TranscriptSegment> AudioTimelineCoordinator::to_absolute(
    const TimelineAnchor& anchor,
    const runtime::TranscriptSegment& relative) const {
  const auto interval = map_interval(
      anchor, CensorInterval{relative.start_frame, relative.end_frame});
  if (!interval.has_value()) {
    return std::nullopt;
  }

  runtime::TranscriptSegment absolute = relative;
  absolute.start_frame = interval->start_frame;
  absolute.end_frame = interval->end_frame;
  return absolute;
}

std::optional<CensorInterval> AudioTimelineCoordinator::to_absolute(
    const TimelineAnchor& anchor, const CensorInterval& relative) const {
  return map_interval(anchor, relative);
}

std::vector<runtime::TranscriptSegment> AudioTimelineCoordinator::to_absolute(
    const TimelineAnchor& anchor,
    const std::vector<runtime::TranscriptSegment>& relative) const {
  std::vector<runtime::TranscriptSegment> absolute;
  absolute.reserve(relative.size());
  for (const auto& segment : relative) {
    const auto mapped = to_absolute(anchor, segment);
    if (mapped.has_value()) {
      absolute.push_back(*mapped);
    }
  }
  return absolute;
}

std::vector<CensorInterval> AudioTimelineCoordinator::to_absolute(
    const TimelineAnchor& anchor,
    const std::vector<CensorInterval>& relative) const {
  std::vector<CensorInterval> absolute;
  absolute.reserve(relative.size());
  for (const auto& interval : relative) {
    const auto mapped = to_absolute(anchor, interval);
    if (mapped.has_value()) {
      absolute.push_back(*mapped);
    }
  }
  return absolute;
}

}  // namespace obs_whisperbleep::core
