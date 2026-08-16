// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/core/phrase_matcher.hpp"
#include "obs_whisperbleep/core/timestamp_coordinator.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::core {

struct MatchPlannerConfig {
  PhraseMatchOptions matching{};
  TimestampConfig timestamp{};
};

/**
 * Converts timestamped runtime transcript segments into censor intervals.
 *
 * The current runtime boundary exposes segment-level timestamps rather than
 * word timestamps. A matching segment therefore contributes its complete
 * timestamp interval. This conservative policy keeps matching deterministic
 * until the real Whisper runtime can provide word-level timing.
 */
class MatchPlanner {
 public:
  explicit MatchPlanner(std::vector<std::string> phrases = {},
                        MatchPlannerConfig config = {});

  void set_phrases(std::vector<std::string> phrases);
  [[nodiscard]] const PhraseMatcher& matcher() const noexcept;
  [[nodiscard]] const MatchPlannerConfig& config() const noexcept;
  [[nodiscard]] std::vector<CensorInterval> plan(
      const std::vector<runtime::TranscriptSegment>& segments) const;

 private:
  MatchPlannerConfig config_;
  PhraseMatcher matcher_;
  TimestampCoordinator timestamps_;
};

}  // namespace obs_whisperbleep::core
