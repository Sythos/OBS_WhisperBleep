// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/timestamp_coordinator.hpp"

#include <cmath>

namespace obs_whisperbleep::core {

TimestampCoordinator::TimestampCoordinator(TimestampConfig config)
    : config_(config) {
  if (config_.sample_rate == 0) {
    config_.sample_rate = 48000;
  }
  if (config_.delay_frames < 0) {
    config_.delay_frames = 0;
  }
}

const TimestampConfig& TimestampCoordinator::config() const noexcept {
  return config_;
}

std::optional<CensorInterval> TimestampCoordinator::to_interval(
    const SpeechSegment& segment) const {
  if (!std::isfinite(segment.start_seconds) ||
      !std::isfinite(segment.end_seconds) || segment.start_seconds < 0.0 ||
      segment.end_seconds <= segment.start_seconds) {
    return std::nullopt;
  }

  const auto start = static_cast<std::int64_t>(std::llround(
      segment.start_seconds * static_cast<double>(config_.sample_rate)));
  const auto end = static_cast<std::int64_t>(std::llround(
      segment.end_seconds * static_cast<double>(config_.sample_rate)));
  const CensorInterval interval{start + config_.delay_frames,
                                end + config_.delay_frames};
  if (interval.start_frame < 0 || interval.end_frame <= interval.start_frame) {
    return std::nullopt;
  }
  return interval;
}

std::vector<CensorInterval> TimestampCoordinator::plan(
    const std::vector<SpeechSegment>& segments) const {
  std::vector<CensorInterval> intervals;
  intervals.reserve(segments.size());
  for (const auto& segment : segments) {
    const auto interval = to_interval(segment);
    if (interval.has_value()) {
      intervals.push_back(*interval);
    }
  }
  return CensorScheduler::merge(intervals);
}

}  // namespace obs_whisperbleep::core
