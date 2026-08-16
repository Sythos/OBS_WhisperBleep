// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/replacement_renderer.hpp"

#include <algorithm>

namespace obs_whisperbleep::core {

std::size_t AudioBuffer::frame_count() const noexcept {
  return channels == 0 ? 0 : samples.size() / channels;
}

namespace {

float fade_gain(std::size_t frame, std::size_t length, std::size_t fade_frames) {
  if (fade_frames == 0 || length == 0) {
    return 1.0F;
  }
  const auto effective_fade = std::min(fade_frames, length / 2);
  if (effective_fade == 0) {
    return 1.0F;
  }
  if (frame < effective_fade) {
    return static_cast<float>(frame + 1) /
           static_cast<float>(effective_fade + 1);
  }
  const auto remaining = length - frame;
  if (remaining <= effective_fade) {
    return static_cast<float>(remaining) /
           static_cast<float>(effective_fade + 1);
  }
  return 1.0F;
}

float replacement_sample(const AudioBuffer& replacement, std::size_t frame,
                         std::size_t channel, std::uint16_t output_channels) {
  if (replacement.channels == 0 || replacement.frame_count() == 0) {
    return 0.0F;
  }
  const auto source_frame = frame % replacement.frame_count();
  if (replacement.channels == output_channels &&
      channel < replacement.channels) {
    return replacement.samples[source_frame * replacement.channels + channel];
  }
  if (replacement.channels == 1) {
    return replacement.samples[source_frame];
  }
  const auto source_channel =
      std::min<std::size_t>(channel, replacement.channels - 1);
  return replacement.samples[source_frame * replacement.channels + source_channel];
}

}  // namespace

AudioBuffer ReplacementRenderer::render(
    const AudioBuffer& input, const std::vector<CensorInterval>& intervals,
    const AudioBuffer& replacement, RenderOptions options) {
  AudioBuffer output = input;
  if (input.channels == 0 || input.frame_count() == 0) {
    return output;
  }

  const auto merged = CensorScheduler::merge(intervals);
  for (const auto interval : merged) {
    // Clamp while the interval is still signed. Converting a negative end
    // frame to size_t before this check would wrap to a very large value and
    // could make the renderer write past the output buffer.
    const auto input_frames = input.frame_count();
    const auto begin_frame = std::max<std::int64_t>(0, interval.start_frame);
    const auto end_frame = std::min<std::int64_t>(
        static_cast<std::int64_t>(input_frames), interval.end_frame);
    if (end_frame <= 0 || begin_frame >= end_frame) {
      continue;
    }
    const auto begin = static_cast<std::size_t>(begin_frame);
    const auto end = static_cast<std::size_t>(end_frame);
    if (begin >= end) {
      continue;
    }
    const auto length = end - begin;
    for (std::size_t frame = 0; frame < length; ++frame) {
      const auto gain = fade_gain(frame, length, options.fade_frames);
      for (std::size_t channel = 0; channel < input.channels; ++channel) {
        output.samples[(begin + frame) * input.channels + channel] =
            replacement_sample(replacement, frame, channel, input.channels) * gain;
      }
    }
  }
  return output;
}

}  // namespace obs_whisperbleep::core
