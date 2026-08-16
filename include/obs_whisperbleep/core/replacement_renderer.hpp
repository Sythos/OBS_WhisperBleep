// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"

namespace obs_whisperbleep::core {

struct AudioBuffer {
  std::uint32_t sample_rate = 48000;
  std::uint16_t channels = 1;
  std::vector<float> samples;

  [[nodiscard]] std::size_t frame_count() const noexcept;
};

struct RenderOptions {
  std::size_t fade_frames = 0;
};

/**
 * Replaces scheduled frames by looping, trimming, or zero-padding a supplied
 * replacement buffer. The operation is deterministic and channel-aware.
 */
class ReplacementRenderer {
 public:
  [[nodiscard]] static AudioBuffer render(
      const AudioBuffer& input, const std::vector<CensorInterval>& intervals,
      const AudioBuffer& replacement, RenderOptions options = {});
};

}  // namespace obs_whisperbleep::core
