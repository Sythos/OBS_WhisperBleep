// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "obs_whisperbleep/diagnostics/latency.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M6 latency test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

obs_whisperbleep::diagnostics::MonotonicTimePoint at_milliseconds(
    const std::int64_t milliseconds) {
  return obs_whisperbleep::diagnostics::MonotonicTimePoint{} +
         std::chrono::milliseconds(milliseconds);
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::diagnostics;

  const LatencyPolicy policy;
  expect(policy.processing_budget == std::chrono::milliseconds(1500),
         "uses the 1.5 second processing budget");
  expect(policy.reassessment_budget == std::chrono::seconds(2),
         "keeps the explicit 2.0 second reassessment budget");
  expect(policy.recommended_delays.size() == 3,
         "recommends three independent audio delays");
  for (const auto delay : policy.recommended_delays) {
    expect(delay == std::chrono::milliseconds(500),
           "each recommended audio delay is 500 milliseconds");
  }
  expect(policy.recommended_delay_total() == policy.processing_budget,
         "the three recommended delays add up to 1.5 seconds");

  LatencyEvaluator evaluator(policy);
  const auto within_budget = evaluator.measure(LatencyTimestamps{
      at_milliseconds(0), at_milliseconds(100), at_milliseconds(1200),
      at_milliseconds(1500)});
  expect(within_budget.valid &&
             within_budget.decision == LatencyDecision::within_budget,
         "accepts an observation that completes at the 1.5 second boundary");
  expect(within_budget.ingress_to_processing == std::chrono::milliseconds(100) &&
             within_budget.processing == std::chrono::milliseconds(1100) &&
             within_budget.processing_to_output == std::chrono::milliseconds(300) &&
             within_budget.total == std::chrono::milliseconds(1500),
         "reports each monotonic stage duration and total latency");

  const auto requires_reassessment = evaluator.measure(LatencyTimestamps{
      at_milliseconds(1000), at_milliseconds(1100), at_milliseconds(2500),
      at_milliseconds(2750)});
  expect(requires_reassessment.valid &&
             requires_reassessment.decision ==
                 LatencyDecision::reassess_video_delay_to_two_seconds,
         "requests an explicit 2.0 second video-delay reassessment after 1.5 seconds");
  expect(requires_reassessment.total == std::chrono::milliseconds(1750),
         "preserves an over-budget but reassessable measurement");

  const auto reassessment_exceeded = evaluator.measure(LatencyTimestamps{
      at_milliseconds(0), at_milliseconds(500), at_milliseconds(2100),
      at_milliseconds(2300)});
  expect(reassessment_exceeded.decision ==
             LatencyDecision::reassessment_budget_exceeded,
         "flags measurements that also exceed the 2.0 second reassessment");

  const auto invalid = evaluator.measure(LatencyTimestamps{
      at_milliseconds(0), at_milliseconds(500), at_milliseconds(400),
      at_milliseconds(700)});
  expect(!invalid.valid &&
             invalid.decision == LatencyDecision::invalid_timestamps,
         "rejects out-of-order injected monotonic timestamps");

  expect(std::string(latency_decision_name(
             LatencyDecision::reassess_video_delay_to_two_seconds)) ==
             "reassess-video-delay-to-two-seconds",
         "exposes a stable decision name for diagnostics consumers");

  return EXIT_SUCCESS;
}
