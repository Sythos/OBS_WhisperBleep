// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"
#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/core/phrase_matcher.hpp"
#include "obs_whisperbleep/core/replacement_renderer.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "core test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool close(float left, float right) {
  return std::fabs(left - right) < 0.0001F;
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::core;

  PhraseMatcher matcher({"  Bad   Word  ", "LOUD"});
  expect(matcher.phrases().size() == 2, "normalizes and keeps phrases");
  const auto matches = matcher.find("A BAD word, then loud.");
  expect(matches.size() == 2, "finds case-insensitive phrases");
  expect(PhraseMatcher::normalize("A\t  B") == "a b", "normalizes spaces");

  const std::vector<CensorInterval> intervals{{8, 12}, {2, 4}, {4, 9}, {10, 10}};
  const auto merged = CensorScheduler::merge(intervals);
  expect(merged.size() == 1 && merged[0].start_frame == 2 &&
             merged[0].end_frame == 12,
         "merges overlapping and touching intervals");

  AudioBuffer input{48000, 1, {1.F, 1.F, 1.F, 1.F, 1.F, 1.F}};
  AudioBuffer beep{48000, 1, {0.25F, 0.5F}};
  const auto rendered = ReplacementRenderer::render(input, {{2, 5}}, beep);
  expect(rendered.samples.size() == input.samples.size(), "keeps buffer size");
  expect(close(rendered.samples[0], 1.F) && close(rendered.samples[1], 1.F),
         "keeps audio before interval");
  expect(close(rendered.samples[2], 0.25F) && close(rendered.samples[3], 0.5F) &&
             close(rendered.samples[4], 0.25F),
         "loops replacement over interval");
  expect(close(rendered.samples[5], 1.F), "keeps audio after interval");

  AudioPipeline pipeline(AudioPipelineConfig{100, 1});
  AudioFrame frame{123, input};
  const auto processed = pipeline.process(frame, {{0, 1}}, beep);
  expect(processed.first_frame == 123, "preserves frame timestamp");
  expect(close(processed.audio.samples[0], 0.25F), "pipeline renders interval");
  expect(pipeline.config().max_buffered_frames == 100, "exposes config");

  return EXIT_SUCCESS;
}
