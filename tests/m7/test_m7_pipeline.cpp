// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <atomic>
#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"
#include "obs_whisperbleep/core/audio_processing_worker.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace {

using namespace std::chrono_literals;
using obs_whisperbleep::core::AudioBuffer;
using obs_whisperbleep::core::AudioFrame;
using obs_whisperbleep::core::EndToEndAudioPipeline;
using obs_whisperbleep::core::EndToEndAudioPipelineConfig;
using obs_whisperbleep::core::EndToEndAudioResult;
using obs_whisperbleep::core::EndToEndStatus;
using obs_whisperbleep::core::AudioProcessingWorker;
using obs_whisperbleep::runtime::IWhisperRuntime;
using obs_whisperbleep::runtime::RuntimeStatus;
using obs_whisperbleep::runtime::TranscriptSegment;

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M7 pipeline test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class FakeRuntime final : public IWhisperRuntime {
 public:
  using TranscriptFactory =
      std::function<std::vector<TranscriptSegment>(const float*, std::size_t,
                                                    std::uint32_t)>;

  explicit FakeRuntime(TranscriptFactory factory,
                       const bool throw_on_transcribe = false)
      : factory_(std::move(factory)), throw_on_transcribe_(throw_on_transcribe) {
  }

  [[nodiscard]] RuntimeStatus initialize(std::string_view) override {
    return RuntimeStatus::ready;
  }

  [[nodiscard]] std::vector<TranscriptSegment> transcribe(
      const float* samples, const std::size_t sample_count,
      const std::uint32_t sample_rate) override {
    ++call_count;
    worker_thread = std::this_thread::get_id();
    if (throw_on_transcribe_) {
      throw std::runtime_error("deterministic runtime failure");
    }
    return factory_ ? factory_(samples, sample_count, sample_rate)
                    : std::vector<TranscriptSegment>{};
  }

  std::atomic<std::size_t> call_count{0};
  std::thread::id worker_thread{};

 private:
  TranscriptFactory factory_;
  bool throw_on_transcribe_ = false;
};

struct ResultCollector {
  void add(EndToEndAudioResult result) {
    std::lock_guard<std::mutex> lock(mutex);
    results.push_back(std::move(result));
    ready.notify_all();
  }

  bool wait_for(const std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return ready.wait_for(lock, 2s,
                          [&] { return results.size() >= count; });
  }

  std::vector<EndToEndAudioResult> snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return results;
  }

  std::mutex mutex;
  std::condition_variable ready;
  std::vector<EndToEndAudioResult> results;
};

AudioFrame frame(const std::int64_t first_frame, const std::size_t frame_count,
                 const std::uint32_t sample_rate = 48'000,
                 const float value = 1.0F) {
  return AudioFrame{first_frame,
                    AudioBuffer{sample_rate, 1,
                                std::vector<float>(frame_count, value)}};
}

bool submit_eventually(EndToEndAudioPipeline& pipeline, AudioFrame input) {
  for (std::size_t attempt = 0; attempt < 10'000; ++attempt) {
    if (pipeline.submit(input)) {
      return true;
    }
    if (!pipeline.running()) {
      return false;
    }
    std::this_thread::yield();
  }
  return false;
}

bool submit_eventually(AudioProcessingWorker& worker, AudioFrame input) {
  for (std::size_t attempt = 0; attempt < 10'000; ++attempt) {
    if (worker.submit(input)) {
      return true;
    }
    if (!worker.running()) {
      return false;
    }
    std::this_thread::yield();
  }
  return false;
}

void test_end_to_end_replacement_and_timing() {
  FakeRuntime runtime([](const float* samples, const std::size_t sample_count,
                         const std::uint32_t sample_rate) {
    expect(samples != nullptr && sample_count == 8 && sample_rate == 48'000,
           "passes mono audio and sample rate to the runtime");
    return std::vector<TranscriptSegment>{{2, 5, "bad word"}};
  });

  ResultCollector collector;
  EndToEndAudioPipelineConfig config;
  config.queue_capacity = 2;
  config.sample_rate = 48'000;
  config.phrases = {"bad"};
  config.replacement = AudioBuffer{48'000, 1, {0.75F}};

  EndToEndAudioPipeline pipeline(runtime, config,
                                 [&](EndToEndAudioResult result) {
                                   collector.add(std::move(result));
                                 });
  expect(pipeline.start(), "starts the M7 asynchronous audio path");
  const auto submit_thread = std::this_thread::get_id();
  expect(submit_eventually(pipeline, frame(1'000, 8)),
         "accepts a valid frame at the realtime boundary");
  expect(collector.wait_for(1), "emits one asynchronous result");
  pipeline.stop();

  const auto results = collector.snapshot();
  expect(results.size() == 1, "does not duplicate an audio result");
  const auto& result = results.front();
  expect(result.status == EndToEndStatus::processed,
         "marks a successfully transcribed frame as processed");
  expect(result.timeline_status == obs_whisperbleep::core::TimelineStatus::anchored,
         "anchors the first frame in the absolute timeline");
  expect(result.transcript.size() == 1 &&
             result.transcript.front().start_frame == 1'002 &&
             result.transcript.front().end_frame == 1'005,
         "maps the relative transcript to absolute frames");
  expect(result.intervals.size() == 1 && result.intervals.front().start_frame == 1'002 &&
             result.intervals.front().end_frame == 1'005,
         "maps the matching interval to the same absolute frames");
  expect(result.output.first_frame == 1'000 &&
             result.output.audio.samples[0] == 1.0F &&
             result.output.audio.samples[1] == 1.0F &&
             result.output.audio.samples[2] == 0.75F &&
             result.output.audio.samples[4] == 0.75F &&
             result.output.audio.samples[5] == 1.0F,
         "replaces only the matched interval and preserves the rest");
  expect(result.latency_timestamps.audio_ingress !=
             obs_whisperbleep::diagnostics::MonotonicTimePoint{} &&
             result.latency_timestamps.processing_start >=
                 result.latency_timestamps.audio_ingress &&
             result.latency_timestamps.replacement_ready >=
                 result.latency_timestamps.processing_start &&
             result.latency_timestamps.audio_output >=
                 result.latency_timestamps.replacement_ready,
         "records ordered monotonic timestamps for the complete path");
  expect(result.latency.valid && result.latency.total >= 0us &&
             result.latency.decision ==
                 obs_whisperbleep::diagnostics::LatencyDecision::within_budget,
         "reports a deterministic result within the M6 processing budget");
  expect(runtime.call_count == 1 && runtime.worker_thread != submit_thread,
         "keeps runtime work off the submitting thread");
  expect(!pipeline.running() && !pipeline.submit(frame(2'000, 8)),
         "rejects new frames after shutdown");
}

void test_safe_pass_through_contracts() {
  FakeRuntime invalid_runtime([](const float*, std::size_t, std::uint32_t) {
    return std::vector<TranscriptSegment>{{0, 1, "bad"}};
  });
  ResultCollector invalid_collector;
  EndToEndAudioPipelineConfig invalid_config;
  invalid_config.sample_rate = 48'000;
  invalid_config.phrases = {"bad"};
  EndToEndAudioPipeline invalid_pipeline(
      invalid_runtime, invalid_config,
      [&](EndToEndAudioResult result) {
        invalid_collector.add(std::move(result));
      });
  expect(invalid_pipeline.start(), "starts the invalid-input contract path");
  expect(submit_eventually(invalid_pipeline, frame(0, 4, 44'100)),
         "accepts malformed input for safe asynchronous handling");
  expect(invalid_collector.wait_for(1), "reports malformed input promptly");
  invalid_pipeline.stop();
  const auto invalid_result = invalid_collector.snapshot().front();
  expect(invalid_result.status == EndToEndStatus::pass_through_invalid_audio &&
             invalid_result.output.audio.sample_rate == 44'100 &&
             invalid_result.output.audio.samples ==
                 std::vector<float>(4, 1.0F) &&
             invalid_runtime.call_count == 0,
         "passes malformed audio through without invoking the runtime");

  FakeRuntime throwing_runtime(
      [](const float*, std::size_t, std::uint32_t) {
        return std::vector<TranscriptSegment>{};
      },
      true);
  ResultCollector throwing_collector;
  EndToEndAudioPipeline throwing_pipeline(
      throwing_runtime, invalid_config,
      [&](EndToEndAudioResult result) {
        throwing_collector.add(std::move(result));
      });
  expect(throwing_pipeline.start(), "starts the runtime-failure contract path");
  const auto original = frame(100, 4);
  expect(submit_eventually(throwing_pipeline, original),
         "accepts a frame before a deterministic runtime failure");
  expect(throwing_collector.wait_for(1), "reports a runtime failure promptly");
  throwing_pipeline.stop();
  const auto throwing_result = throwing_collector.snapshot().front();
  expect(throwing_result.status == EndToEndStatus::pass_through_runtime_failure &&
             throwing_result.timeline_status ==
                 obs_whisperbleep::core::TimelineStatus::anchored &&
             throwing_result.output.audio.samples == original.audio.samples &&
             throwing_result.transcript.empty() && throwing_result.intervals.empty(),
         "passes the original frame through when runtime processing fails");
}

void test_timeline_rejection_is_pass_through() {
  FakeRuntime runtime([](const float*, std::size_t, std::uint32_t) {
    return std::vector<TranscriptSegment>{{0, 2, "bad"}};
  });
  ResultCollector collector;
  EndToEndAudioPipelineConfig config;
  config.sample_rate = 48'000;
  config.phrases = {"bad"};
  config.replacement = AudioBuffer{48'000, 1, {0.25F}};
  EndToEndAudioPipeline pipeline(
      runtime, config,
      [&](EndToEndAudioResult result) { collector.add(std::move(result)); });
  expect(pipeline.start(), "starts the timeline rejection path");
  expect(submit_eventually(pipeline, frame(1'000, 4)),
         "accepts the first contiguous timeline chunk");
  expect(submit_eventually(pipeline, frame(2'000, 4)),
         "accepts the non-contiguous chunk for validation");
  expect(collector.wait_for(2), "emits both timeline decisions");
  pipeline.stop();

  const auto results = collector.snapshot();
  expect(results.size() == 2 && results[0].status == EndToEndStatus::processed &&
             results[1].status == EndToEndStatus::pass_through_timeline_rejected,
         "rejects an unmarked gap instead of applying stale censor data");
  expect(results[1].output.first_frame == 2'000 &&
             results[1].output.audio.samples == std::vector<float>(4, 1.0F),
         "returns the untouched frame for a rejected timeline chunk");
  expect(runtime.call_count == 1,
         "does not invoke the runtime for a rejected timeline chunk");
}

void test_bounded_queue_and_draining_shutdown() {
  std::mutex gate_mutex;
  std::condition_variable gate_changed;
  bool first_started = false;
  bool release_first = false;
  std::vector<std::int64_t> processed;

  AudioProcessingWorker worker(
      1, [&](AudioFrame input) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        processed.push_back(input.first_frame);
        if (input.first_frame == 1) {
          first_started = true;
          gate_changed.notify_all();
          gate_changed.wait(lock, [&] { return release_first; });
        }
      });
  expect(worker.start(), "starts the bounded worker");
  for (std::size_t attempt = 0; attempt < 10'000 && !worker.running(); ++attempt) {
    std::this_thread::yield();
  }
  expect(worker.running(), "publishes the worker running state");
  expect(submit_eventually(worker, frame(1, 1)), "accepts the first frame");
  {
    std::unique_lock<std::mutex> lock(gate_mutex);
    expect(gate_changed.wait_for(lock, 2s, [&] { return first_started; }),
           "worker begins processing the first frame");
  }
  expect(worker.submit(frame(2, 1)), "retains one frame up to queue capacity");
  expect(!worker.submit(frame(3, 1)), "drops the newest frame when full");
  expect(worker.dropped_frames() == 1 && worker.queued_frames() == 1,
         "exposes bounded queue accounting");

  {
    std::lock_guard<std::mutex> lock(gate_mutex);
    release_first = true;
  }
  gate_changed.notify_all();
  worker.stop();
  expect(!worker.running() && processed == std::vector<std::int64_t>{1, 2},
         "shutdown drains every accepted frame before joining");
  expect(!worker.submit(frame(4, 1)), "rejects submissions after shutdown");
}

}  // namespace

int main() {
  test_end_to_end_replacement_and_timing();
  test_safe_pass_through_contracts();
  test_timeline_rejection_is_pass_through();
  test_bounded_queue_and_draining_shutdown();
  return EXIT_SUCCESS;
}
