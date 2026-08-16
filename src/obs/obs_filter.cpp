// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/obs_filter.hpp"

#include <utility>

namespace obs_whisperbleep::obs {

bool ObsFilter::load() noexcept {
  loaded_ = true;
  return loaded_;
}

void ObsFilter::unload() noexcept { loaded_ = false; }

void ObsFilter::update(FilterSettings settings) {
  settings_ = std::move(settings);
}

core::AudioFrame ObsFilter::process(
    const core::AudioFrame& input,
    const std::vector<core::CensorInterval>& intervals,
    const core::AudioBuffer& replacement) const {
  if (!loaded_ || !settings_.enabled || intervals.empty()) {
    return input;
  }

  core::AudioPipeline pipeline;
  return pipeline.process(input, intervals, replacement);
}

}  // namespace obs_whisperbleep::obs
