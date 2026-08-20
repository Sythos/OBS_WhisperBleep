// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace obs_whisperbleep::diagnostics {

/**
 * All latency timestamps must come from one monotonic clock.
 *
 * The real-time audio callback can capture these timestamps and pass them to
 * the evaluator without performing I/O, logging, waiting, or model work.
 * Tests can inject deterministic time points by constructing a
 * LatencyTimestamps value directly.
 */
using MonotonicClock = std::chrono::steady_clock;
using MonotonicTimePoint = MonotonicClock::time_point;
using LatencyDuration = std::chrono::microseconds;

enum class LatencyDecision {
  invalid_timestamps,
  within_budget,
  reassess_video_delay_to_two_seconds,
  reassessment_budget_exceeded,
};

[[nodiscard]] const char* latency_decision_name(
    LatencyDecision decision) noexcept;

/**
 * M6 timing policy for Whisper transcription and blacklist replacement.
 *
 * The normal end-to-end budget is 1.5 seconds. Three independent 500 ms
 * delays are the recommended audio arrangement and add up to that budget.
 * If the budget is not sufficient, the explicit next decision is to
 * reassess a 2.0-second video delay; this is not applied automatically.
 */
struct LatencyPolicy {
  static constexpr LatencyDuration kProcessingBudget{1'500'000};
  static constexpr LatencyDuration kReassessmentBudget{2'000'000};
  static constexpr LatencyDuration kRecommendedDelay{500'000};
  static constexpr std::size_t kRecommendedDelayCount = 3;

  LatencyDuration processing_budget{kProcessingBudget};
  LatencyDuration reassessment_budget{kReassessmentBudget};
  std::array<LatencyDuration, kRecommendedDelayCount> recommended_delays{
      kRecommendedDelay, kRecommendedDelay, kRecommendedDelay};

  [[nodiscard]] LatencyDuration recommended_delay_total() const noexcept;
};

/**
 * Monotonic timestamps for one audio path observation.
 *
 * The processing interval covers Whisper inference and blacklist replacement.
 * A sample is valid only when the stages are ordered from ingress to output.
 */
struct LatencyTimestamps {
  MonotonicTimePoint audio_ingress{};
  MonotonicTimePoint processing_start{};
  MonotonicTimePoint replacement_ready{};
  MonotonicTimePoint audio_output{};
};

struct LatencyMeasurement {
  LatencyDecision decision = LatencyDecision::invalid_timestamps;
  bool valid = false;
  LatencyDuration ingress_to_processing{};
  LatencyDuration processing{};
  LatencyDuration processing_to_output{};
  LatencyDuration total{};
};

/**
 * Pure, allocation-free latency evaluation for one observation.
 *
 * This class intentionally has no logger, filesystem, or clock side effect.
 * Capture timestamps at the integration boundary using MonotonicClock, then
 * evaluate them off the real-time callback.
 */
class LatencyEvaluator {
 public:
  explicit LatencyEvaluator(LatencyPolicy policy = {});

  [[nodiscard]] const LatencyPolicy& policy() const noexcept;
  [[nodiscard]] LatencyMeasurement measure(
      const LatencyTimestamps& timestamps) const noexcept;

 private:
  LatencyPolicy policy_;
};

}  // namespace obs_whisperbleep::diagnostics
