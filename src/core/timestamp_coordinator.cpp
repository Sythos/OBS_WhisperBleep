// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/timestamp_coordinator.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace obs_whisperbleep::core {

namespace {

// 2^63 is exactly representable as a binary floating-point value. Keeping
// the upper bound exclusive prevents an out-of-range floating-to-integer
// conversion on platforms where int64_t's maximum rounds to 2^63.
constexpr long double kInt64ExclusiveUpperBound = 9223372036854775808.0L;

[[nodiscard]] std::optional<std::int64_t> round_seconds_to_frames(
    const double seconds, const std::uint32_t sample_rate) noexcept {
  if (!std::isfinite(seconds) || seconds < 0.0 || sample_rate == 0) {
    return std::nullopt;
  }

  const long double scaled =
      static_cast<long double>(seconds) * sample_rate;
  if (!std::isfinite(scaled)) {
    return std::nullopt;
  }

  const long double rounded = std::round(scaled);
  if (rounded < 0.0L || rounded >= kInt64ExclusiveUpperBound) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

[[nodiscard]] std::optional<std::int64_t> add_delay(
    const std::int64_t frame, const std::int64_t delay) noexcept {
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  if (delay > 0 && frame > maximum - delay) {
    return std::nullopt;
  }
  if (delay < 0 && frame < minimum - delay) {
    return std::nullopt;
  }
  return frame + delay;
}

}  // namespace

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
  if (config_.sample_rate == 0 || !std::isfinite(segment.start_seconds) ||
      !std::isfinite(segment.end_seconds) || segment.start_seconds < 0.0 ||
      segment.end_seconds <= segment.start_seconds) {
    return std::nullopt;
  }

  const auto start =
      round_seconds_to_frames(segment.start_seconds, config_.sample_rate);
  const auto end =
      round_seconds_to_frames(segment.end_seconds, config_.sample_rate);
  if (!start.has_value() || !end.has_value()) {
    return std::nullopt;
  }

  const auto delayed_start = add_delay(*start, config_.delay_frames);
  const auto delayed_end = add_delay(*end, config_.delay_frames);
  if (!delayed_start.has_value() || !delayed_end.has_value()) {
    return std::nullopt;
  }

  const CensorInterval interval{*delayed_start, *delayed_end};
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
