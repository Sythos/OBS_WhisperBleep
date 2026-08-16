// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"

namespace obs_whisperbleep::obs {

struct FilterSettings {
  bool enabled{true};
  std::string phrases;
  std::string model{"tiny"};
  std::string backend{"auto"};
  std::string replacement{"beep"};
};

/** Testable filter state shared by the native OBS adapter and the fallback. */
class ObsFilter {
 public:
  ObsFilter() = default;

  [[nodiscard]] bool load() noexcept;
  void unload() noexcept;
  [[nodiscard]] bool loaded() const noexcept { return loaded_; }

  void update(FilterSettings settings);
  [[nodiscard]] const FilterSettings& settings() const noexcept {
    return settings_;
  }

  [[nodiscard]] core::AudioFrame process(
      const core::AudioFrame& input,
      const std::vector<core::CensorInterval>& intervals,
      const core::AudioBuffer& replacement) const;

 private:
  FilterSettings settings_{};
  bool loaded_{false};
};

}  // namespace obs_whisperbleep::obs
