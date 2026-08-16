// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/censor_scheduler.hpp"

#include <algorithm>

namespace obs_whisperbleep::core {

std::vector<CensorInterval> CensorScheduler::merge(
    const std::vector<CensorInterval>& intervals) {
  std::vector<CensorInterval> sorted;
  sorted.reserve(intervals.size());
  for (const auto interval : intervals) {
    if (interval.start_frame < interval.end_frame) {
      sorted.push_back(interval);
    }
  }
  std::sort(sorted.begin(), sorted.end(), [](const CensorInterval& left,
                                             const CensorInterval& right) {
    if (left.start_frame != right.start_frame) {
      return left.start_frame < right.start_frame;
    }
    return left.end_frame < right.end_frame;
  });

  std::vector<CensorInterval> merged;
  for (const auto interval : sorted) {
    if (merged.empty() || interval.start_frame > merged.back().end_frame) {
      merged.push_back(interval);
      continue;
    }
    merged.back().end_frame = std::max(merged.back().end_frame,
                                       interval.end_frame);
  }
  return merged;
}

}  // namespace obs_whisperbleep::core
