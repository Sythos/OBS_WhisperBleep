// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/obs_filter.hpp"

namespace obs_whisperbleep::obs {

core::AudioFrame ObsFilter::process(
    const core::AudioFrame& input,
    const std::vector<core::CensorInterval>& intervals,
    const core::AudioBuffer& replacement) const {
  core::AudioPipeline pipeline;
  return pipeline.process(input, intervals, replacement);
}

}  // namespace obs_whisperbleep::obs
