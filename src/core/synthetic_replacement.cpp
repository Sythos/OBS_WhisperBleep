// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/synthetic_replacement.hpp"

#include <algorithm>
#include <cmath>

namespace obs_whisperbleep::core {

AudioBuffer SyntheticReplacement::beep(std::uint32_t sample_rate,
                                       std::uint16_t channels,
                                       std::size_t frame_count,
                                       BeepOptions options) {
  AudioBuffer output{sample_rate, channels, {}};
  if (sample_rate == 0 || channels == 0 || frame_count == 0) {
    return output;
  }

  constexpr double pi = 3.14159265358979323846;
  const auto nyquist = static_cast<double>(sample_rate) / 2.0;
  const auto frequency = std::clamp(
      std::isfinite(options.frequency_hz) ? options.frequency_hz : 1000.0,
      1.0, std::max(1.0, nyquist));
  const auto amplitude = std::clamp(
      std::isfinite(options.amplitude) ? options.amplitude : 0.25F, 0.0F,
      1.0F);

  output.samples.resize(frame_count * channels);
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    const auto phase = 2.0 * pi * frequency *
                       static_cast<double>(frame) /
                       static_cast<double>(sample_rate);
    const auto sample = static_cast<float>(std::sin(phase)) * amplitude;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      output.samples[frame * channels + channel] = sample;
    }
  }
  return output;
}

}  // namespace obs_whisperbleep::core
