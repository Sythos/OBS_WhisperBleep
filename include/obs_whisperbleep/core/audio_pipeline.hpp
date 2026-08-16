// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/core/replacement_renderer.hpp"

namespace obs_whisperbleep::core {

struct AudioFrame {
  std::int64_t first_frame = 0;
  AudioBuffer audio;
};

struct AudioPipelineConfig {
  std::size_t max_buffered_frames = 48000;
  std::size_t replacement_fade_frames = 0;
};

/**
 * Synchronous M0 audio path. It deliberately has no model or thread
 * dependency; a later milestone can feed it intervals produced by a worker.
 */
class AudioPipeline {
 public:
  explicit AudioPipeline(AudioPipelineConfig config = {});

  [[nodiscard]] AudioFrame process(const AudioFrame& input,
                                   const std::vector<CensorInterval>& intervals,
                                   const AudioBuffer& replacement) const;
  [[nodiscard]] const AudioPipelineConfig& config() const noexcept;

 private:
  AudioPipelineConfig config_;
};

}  // namespace obs_whisperbleep::core
