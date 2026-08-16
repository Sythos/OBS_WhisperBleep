// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

#include "obs_whisperbleep/core/audio_frame_queue.hpp"
#include "obs_whisperbleep/core/audio_processing_worker.hpp"
#include "obs_whisperbleep/core/replacement_renderer.hpp"
#include "obs_whisperbleep/core/synthetic_replacement.hpp"
#include "obs_whisperbleep/core/timestamp_coordinator.hpp"

namespace {

using obs_whisperbleep::core::AudioBuffer;
using obs_whisperbleep::core::AudioFrame;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M2 test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

AudioFrame frame(std::int64_t first_frame) {
  return AudioFrame{first_frame, AudioBuffer{48000, 1, {1.0F, 1.0F}}};
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::core;

  AudioFrameQueue queue(2);
  expect(queue.capacity() == 2, "reports bounded capacity");
  expect(queue.try_push(frame(1)) && queue.try_push(frame(2)),
         "accepts frames until full");
  expect(!queue.try_push(frame(3)), "rejects newest frame when full");
  expect(queue.dropped_frames() == 1, "counts dropped frames");
  expect(queue.try_pop()->first_frame == 1 &&
             queue.try_pop()->first_frame == 2,
         "preserves FIFO order");
  expect(!queue.try_pop().has_value(), "reports empty queue");

  bool rejected_unrepresentable_capacity = false;
  try {
    [[maybe_unused]] const AudioFrameQueue unrepresentable(
        std::numeric_limits<std::size_t>::max());
  } catch (const std::length_error&) {
    rejected_unrepresentable_capacity = true;
  }
  expect(rejected_unrepresentable_capacity,
         "rejects a capacity that would overflow the ring buffer");

  std::atomic<int> processed{0};
  AudioProcessingWorker worker(
      4, [&processed](AudioFrame) { processed.fetch_add(1); });
  expect(worker.start(), "starts worker");
  expect(!worker.start(), "rejects duplicate start");
  expect(worker.submit(frame(10)) && worker.submit(frame(11)),
         "submits frames without blocking");
  worker.stop();
  expect(processed.load() == 2 && !worker.running(),
         "drains accepted frames before stopping the worker");
  expect(!worker.submit(frame(12)), "rejects submissions after stop");

  std::atomic<int> callback_calls{0};
  AudioProcessingWorker restartable(
      2, [&callback_calls](AudioFrame) {
        if (callback_calls.fetch_add(1) == 0) {
          throw std::runtime_error("synthetic processor failure");
        }
      });
  expect(restartable.start() && restartable.submit(frame(20)),
         "starts a worker with a throwing callback");
  for (int attempt = 0;
       attempt < 100 && restartable.running(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  restartable.stop();
  expect(!restartable.running() && restartable.start(),
         "restarts after a callback exception");
  expect(restartable.submit(frame(21)), "accepts frames after restart");
  restartable.stop();
  expect(callback_calls.load() == 2,
         "contains callback failure without terminating the worker");

  std::atomic<bool> producer_stop{false};
  std::atomic<int> concurrently_accepted{0};
  std::atomic<int> concurrently_processed{0};
  AudioProcessingWorker concurrent_worker(
      8, [&concurrently_processed](AudioFrame) {
        concurrently_processed.fetch_add(1);
      });
  expect(concurrent_worker.start(), "starts concurrent shutdown worker");
  std::thread producer([&] {
    std::int64_t first_frame = 100;
    while (!producer_stop.load()) {
      if (concurrent_worker.submit(frame(first_frame++))) {
        concurrently_accepted.fetch_add(1);
      }
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  concurrent_worker.stop();
  producer_stop.store(true);
  producer.join();
  expect(concurrently_processed.load() == concurrently_accepted.load(),
         "does not leave accepted frames behind during concurrent shutdown");

  TimestampCoordinator coordinator(TimestampConfig{100, 10});
  const SpeechSegment first{0.10, 0.20, "bad", 0.9F};
  const auto first_interval = coordinator.to_interval(first);
  expect(first_interval.has_value() && first_interval->start_frame == 20 &&
             first_interval->end_frame == 30,
         "converts seconds and delay to frames");
  expect(!coordinator.to_interval(SpeechSegment{-0.1, 0.2, "invalid", 0.9F})
              .has_value(),
         "rejects negative timestamps");
  expect(!coordinator.to_interval(SpeechSegment{0.2, 0.2, "invalid", 0.9F})
              .has_value(),
         "rejects empty segments");
  expect(!coordinator
              .to_interval(SpeechSegment{
                  std::numeric_limits<double>::quiet_NaN(), 0.2, "invalid",
                  0.9F})
              .has_value(),
         "rejects non-finite timestamps");
  const auto merged = coordinator.plan(
      {SpeechSegment{0.0, 0.10, "one", 1.0F},
       SpeechSegment{0.05, 0.20, "two", 1.0F}});
  expect(merged.size() == 1 && merged[0].start_frame == 10 &&
             merged[0].end_frame == 30,
         "merges overlapping timestamp intervals");
  const auto touching = coordinator.plan(
      {SpeechSegment{0.0, 0.10, "one", 1.0F},
       SpeechSegment{0.10, 0.20, "two", 1.0F}});
  expect(touching.size() == 1 && touching[0].start_frame == 10 &&
             touching[0].end_frame == 30,
         "merges touching timestamp intervals");

  TimestampCoordinator overflowing_delay(
      TimestampConfig{1, std::numeric_limits<std::int64_t>::max()});
  expect(!overflowing_delay
              .to_interval(SpeechSegment{0.0, 1.0, "overflow", 1.0F})
              .has_value(),
         "rejects delay overflow");
  expect(!coordinator
              .to_interval(SpeechSegment{1.0e300, 1.0e301, "overflow", 1.0F})
              .has_value(),
         "rejects timestamps outside int64 frame range");

  const auto beep = SyntheticReplacement::beep(
      48000, 2, 8, BeepOptions{1000.0, 0.5F});
  expect(beep.sample_rate == 48000 && beep.channels == 2 &&
             beep.samples.size() == 16,
         "generates a bounded stereo beep");
  expect(std::fabs(beep.samples[0]) < 0.0001F &&
             std::fabs(beep.samples[1]) < 0.0001F,
         "starts beep at zero phase");
  for (const auto sample : beep.samples) {
    expect(std::isfinite(sample) && std::fabs(sample) <= 0.5F,
           "keeps beep samples within amplitude");
  }

  const AudioBuffer input{48000, 1,
                          {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
                           1.0F}};
  const AudioBuffer replacement{48000, 1, {0.25F, 0.5F}};
  const auto rendered = ReplacementRenderer::render(
      input, {{2, 5}, {4, 7}}, replacement, RenderOptions{1});
  expect(rendered.samples[0] == 1.0F && rendered.samples[1] == 1.0F &&
             rendered.samples[7] == 1.0F,
         "preserves audio outside merged interval");
  expect(rendered.samples[2] != 1.0F && rendered.samples[3] != 1.0F &&
             rendered.samples[4] != 1.0F && rendered.samples[5] != 1.0F &&
             rendered.samples[6] != 1.0F,
         "replaces overlapping interval once");
  expect(rendered.samples[2] < 0.25F && rendered.samples[6] < 0.5F,
         "applies deterministic fades at replacement edges");

  const AudioBuffer longer_replacement{48000, 1,
                                       {0.1F, 0.2F, 0.3F, 0.4F}};
  const auto trimmed = ReplacementRenderer::render(
      input, {{0, 3}}, longer_replacement);
  expect(trimmed.samples[0] == 0.1F && trimmed.samples[1] == 0.2F &&
             trimmed.samples[2] == 0.3F,
         "trims a replacement longer than the censored interval");

  const auto before_input = ReplacementRenderer::render(
      input, {{-4, -1}}, replacement);
  expect(before_input.samples == input.samples,
         "ignores intervals ending before the input buffer");

  const auto crossing_start = ReplacementRenderer::render(
      input, {{-2, 2}}, replacement);
  expect(crossing_start.samples[0] == 0.25F &&
             crossing_start.samples[1] == 0.5F &&
             crossing_start.samples[2] == 1.0F,
         "clips intervals that start before the input buffer");

  return EXIT_SUCCESS;
}
