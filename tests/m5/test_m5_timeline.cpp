// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "obs_whisperbleep/core/audio_timeline.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M5 timeline test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::core;
  using obs_whisperbleep::runtime::TranscriptSegment;

  AudioTimelineCoordinator timeline(AudioTimelineConfig{48000});
  const auto first = timeline.accept(TimelineChunk{1000, 100, 48000, false});
  expect(first.status == TimelineStatus::anchored && first.anchor.has_value(),
         "anchors the first valid chunk");
  expect(first.generation == 1 && timeline.anchored(),
         "starts the first timeline generation");

  const auto mapped_segment = timeline.to_absolute(
      *first.anchor, TranscriptSegment{5, 20, "bad word"});
  expect(mapped_segment.has_value() && mapped_segment->start_frame == 1005 &&
             mapped_segment->end_frame == 1020 &&
             mapped_segment->text == "bad word",
         "maps relative transcript frames to absolute frames");

  const auto mapped_interval =
      timeline.to_absolute(*first.anchor, CensorInterval{20, 50});
  expect(mapped_interval.has_value() && mapped_interval->start_frame == 1020 &&
             mapped_interval->end_frame == 1050,
         "maps relative censor intervals to absolute frames");

  const auto second = timeline.accept(TimelineChunk{1100, 50, 48000, false});
  expect(second.status == TimelineStatus::advanced &&
             second.anchor.has_value() &&
             second.anchor->first_frame == 1100 &&
             second.generation == first.generation,
         "advances contiguous chunks without changing the epoch");
  expect(timeline.to_absolute(*first.anchor, CensorInterval{0, 1})
                 ->start_frame == 1000,
         "keeps earlier chunk anchors valid within the active epoch");

  const auto explicit_reset =
      timeline.accept(TimelineChunk{2000, 30, 48000, true});
  expect(explicit_reset.status == TimelineStatus::reset &&
             explicit_reset.anchor.has_value() &&
             explicit_reset.generation > first.generation,
         "starts a new epoch for an explicit discontinuity");
  expect(!timeline.to_absolute(*first.anchor, CensorInterval{0, 1}),
         "does not allow stale results after explicit reset");

  const auto non_contiguous =
      timeline.accept(TimelineChunk{2035, 20, 48000, false});
  expect(non_contiguous.status == TimelineStatus::non_contiguous &&
             !non_contiguous.anchor.has_value() && !timeline.anchored(),
         "rejects an unmarked gap and clears the active anchor");

  const auto still_non_contiguous =
      timeline.accept(TimelineChunk{2035, 20, 48000, false});
  expect(still_non_contiguous.status == TimelineStatus::non_contiguous &&
             !still_non_contiguous.anchor.has_value() &&
             !timeline.anchored(),
         "requires an explicit discontinuity marker after an unmarked gap");

  const auto reset = timeline.accept(TimelineChunk{3000, 30, 48000, true});
  expect(reset.status == TimelineStatus::anchored && reset.anchor.has_value(),
         "anchors a new epoch after an unmarked gap");
  // The preceding rejection cleared the state; a marked chunk is therefore a
  // safe new anchor even though it follows a stream discontinuity.
  expect(reset.generation > explicit_reset.generation &&
             reset.anchor->first_frame == 3000,
         "increments the generation after recovery");

  const auto mismatched_rate =
      timeline.accept(TimelineChunk{3030, 10, 44100, false});
  expect(mismatched_rate.status == TimelineStatus::sample_rate_mismatch &&
             mismatched_rate.anchor == std::nullopt && timeline.anchored(),
         "rejects a sample-rate change without mutating the timeline");

  const auto out_of_window =
      timeline.to_absolute(*reset.anchor, CensorInterval{0, 31});
  expect(!out_of_window.has_value(), "rejects intervals outside their chunk");
  expect(!timeline.to_absolute(*reset.anchor, TranscriptSegment{-1, 2, "bad"}),
         "rejects negative relative transcript frames");

  const auto invalid_chunk = timeline.accept(
      TimelineChunk{std::numeric_limits<std::int64_t>::max(), 2, 48000, false});
  expect(invalid_chunk.status == TimelineStatus::invalid_chunk &&
             timeline.generation() == reset.generation,
         "rejects frame-count overflow without changing state");

  timeline.reset();
  expect(!timeline.anchored() && timeline.generation() > reset.generation,
         "supports explicit state reset");
  const auto after_reset =
      timeline.accept(TimelineChunk{3000, 10, 48000, false});
  expect(after_reset.status == TimelineStatus::anchored,
         "accepts a new first chunk after explicit reset");

  AudioTimelineCoordinator default_rate(AudioTimelineConfig{0});
  expect(default_rate.config().sample_rate == 48000,
         "normalizes an invalid zero configuration safely");
  expect(std::string(timeline_status_name(TimelineStatus::advanced)) ==
             "advanced",
         "exposes stable status names");

  const std::vector<TranscriptSegment> segments{
      {0, 2, "first"}, {8, 9, "second"}, {-1, 3, "invalid"}};
  const auto mapped_segments =
      timeline.to_absolute(*after_reset.anchor, segments);
  expect(mapped_segments.size() == 2 && mapped_segments[0].start_frame == 3000 &&
             mapped_segments[1].end_frame == 3009,
         "maps valid transcript vectors and drops invalid entries");

  return EXIT_SUCCESS;
}
