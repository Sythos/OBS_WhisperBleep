// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/match_planner.hpp"

#include <utility>

namespace obs_whisperbleep::core {

MatchPlanner::MatchPlanner(std::vector<std::string> phrases,
                           MatchPlannerConfig config)
    : config_(config), matcher_(std::move(phrases)),
      timestamps_(config_.timestamp) {
  // Expose the same validated timestamp settings used by the coordinator.
  config_.timestamp = timestamps_.config();
}

void MatchPlanner::set_phrases(std::vector<std::string> phrases) {
  matcher_.set_phrases(std::move(phrases));
}

const PhraseMatcher& MatchPlanner::matcher() const noexcept {
  return matcher_;
}

const MatchPlannerConfig& MatchPlanner::config() const noexcept {
  return config_;
}

std::vector<CensorInterval> MatchPlanner::plan(
    const std::vector<runtime::TranscriptSegment>& segments) const {
  std::vector<CensorInterval> intervals;
  intervals.reserve(segments.size());
  const auto sample_rate = timestamps_.config().sample_rate;
  if (sample_rate == 0) {
    return intervals;
  }

  for (const auto& segment : segments) {
    if (segment.start_frame < 0 || segment.end_frame <= segment.start_frame ||
        matcher_.find(segment.text, config_.matching).empty()) {
      continue;
    }

    const SpeechSegment timestamped_segment{
        static_cast<double>(segment.start_frame) / sample_rate,
        static_cast<double>(segment.end_frame) / sample_rate, segment.text,
        0.0F};
    const auto interval = timestamps_.to_interval(timestamped_segment);
    if (interval.has_value()) {
      intervals.push_back(*interval);
    }
  }
  return CensorScheduler::merge(intervals);
}

}  // namespace obs_whisperbleep::core
