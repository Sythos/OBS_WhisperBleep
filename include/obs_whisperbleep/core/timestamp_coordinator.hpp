// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/core/speech_segment.hpp"

namespace obs_whisperbleep::core {

struct TimestampConfig {
  std::uint32_t sample_rate = 48000;
  std::int64_t delay_frames = 0;
};

/** Converts simulated speech timestamps into deterministic censor intervals. */
class TimestampCoordinator {
 public:
  explicit TimestampCoordinator(TimestampConfig config = {});

  [[nodiscard]] const TimestampConfig& config() const noexcept;
  [[nodiscard]] std::optional<CensorInterval> to_interval(
      const SpeechSegment& segment) const;
  [[nodiscard]] std::vector<CensorInterval> plan(
      const std::vector<SpeechSegment>& segments) const;

 private:
  TimestampConfig config_;
};

}  // namespace obs_whisperbleep::core
