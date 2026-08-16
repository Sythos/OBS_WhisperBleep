// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"

namespace obs_whisperbleep::obs {

/** OBS-independent filter stub used until the OBS SDK is selected in M1. */
class ObsFilter {
 public:
  ObsFilter() = default;
  [[nodiscard]] core::AudioFrame process(
      const core::AudioFrame& input,
      const std::vector<core::CensorInterval>& intervals,
      const core::AudioBuffer& replacement) const;
};

}  // namespace obs_whisperbleep::obs
