// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>

#include "obs_whisperbleep/core/replacement_renderer.hpp"

namespace obs_whisperbleep::core {

struct BeepOptions {
  double frequency_hz = 1000.0;
  float amplitude = 0.25F;
};

/** Generates a deterministic, dependency-free replacement beep. */
class SyntheticReplacement {
 public:
  [[nodiscard]] static AudioBuffer beep(std::uint32_t sample_rate,
                                         std::uint16_t channels,
                                         std::size_t frame_count,
                                         BeepOptions options = {});
};

}  // namespace obs_whisperbleep::core
