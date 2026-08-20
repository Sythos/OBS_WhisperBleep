// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/diagnostics/latency.hpp"

#include <utility>

namespace obs_whisperbleep::diagnostics {

const char* latency_decision_name(const LatencyDecision decision) noexcept {
  switch (decision) {
    case LatencyDecision::invalid_timestamps:
      return "invalid-timestamps";
    case LatencyDecision::within_budget:
      return "within-budget";
    case LatencyDecision::reassess_video_delay_to_two_seconds:
      return "reassess-video-delay-to-two-seconds";
    case LatencyDecision::reassessment_budget_exceeded:
      return "reassessment-budget-exceeded";
  }
  return "invalid-timestamps";
}

LatencyDuration LatencyPolicy::recommended_delay_total() const noexcept {
  LatencyDuration total{};
  for (const auto delay : recommended_delays) {
    total += delay;
  }
  return total;
}

LatencyEvaluator::LatencyEvaluator(LatencyPolicy policy)
    : policy_(std::move(policy)) {
  if (policy_.processing_budget <= LatencyDuration::zero()) {
    policy_.processing_budget = LatencyPolicy::kProcessingBudget;
  }
  if (policy_.reassessment_budget < policy_.processing_budget) {
    policy_.reassessment_budget = LatencyPolicy::kReassessmentBudget;
  }
}

const LatencyPolicy& LatencyEvaluator::policy() const noexcept {
  return policy_;
}

LatencyMeasurement LatencyEvaluator::measure(
    const LatencyTimestamps& timestamps) const noexcept {
  LatencyMeasurement result;
  if (timestamps.processing_start < timestamps.audio_ingress ||
      timestamps.replacement_ready < timestamps.processing_start ||
      timestamps.audio_output < timestamps.replacement_ready) {
    return result;
  }

  result.valid = true;
  result.ingress_to_processing = std::chrono::duration_cast<LatencyDuration>(
      timestamps.processing_start - timestamps.audio_ingress);
  result.processing = std::chrono::duration_cast<LatencyDuration>(
      timestamps.replacement_ready - timestamps.processing_start);
  result.processing_to_output = std::chrono::duration_cast<LatencyDuration>(
      timestamps.audio_output - timestamps.replacement_ready);
  result.total = std::chrono::duration_cast<LatencyDuration>(
      timestamps.audio_output - timestamps.audio_ingress);

  if (result.total <= policy_.processing_budget) {
    result.decision = LatencyDecision::within_budget;
  } else if (result.total <= policy_.reassessment_budget) {
    result.decision = LatencyDecision::reassess_video_delay_to_two_seconds;
  } else {
    result.decision = LatencyDecision::reassessment_budget_exceeded;
  }
  return result;
}

}  // namespace obs_whisperbleep::diagnostics
