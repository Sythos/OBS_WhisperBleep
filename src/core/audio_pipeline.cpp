// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_pipeline.hpp"

namespace obs_whisperbleep::core {

AudioPipeline::AudioPipeline(AudioPipelineConfig config) : config_(config) {
  if (config_.max_buffered_frames == 0) {
    config_.max_buffered_frames = 1;
  }
}

AudioFrame AudioPipeline::process(
    const AudioFrame& input, const std::vector<CensorInterval>& intervals,
    const AudioBuffer& replacement) const {
  AudioFrame output = input;
  output.audio = ReplacementRenderer::render(
      input.audio, intervals, replacement,
      RenderOptions{config_.replacement_fade_frames});
  return output;
}

const AudioPipelineConfig& AudioPipeline::config() const noexcept {
  return config_;
}

}  // namespace obs_whisperbleep::core
