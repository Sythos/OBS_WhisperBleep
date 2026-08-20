// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"
#include "obs_whisperbleep/platform/platform_info.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "integration test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class MatchingRuntime final : public obs_whisperbleep::runtime::IWhisperRuntime {
 public:
  [[nodiscard]] obs_whisperbleep::runtime::RuntimeStatus initialize(
      std::string_view) override {
    return obs_whisperbleep::runtime::RuntimeStatus::ready;
  }

  [[nodiscard]] std::vector<obs_whisperbleep::runtime::TranscriptSegment>
  transcribe(const float* samples, std::size_t sample_count,
             std::uint32_t sample_rate) override {
    called.store(true);
    worker_thread = std::this_thread::get_id();
    if (samples == nullptr || sample_count != 100 || sample_rate != 100) {
      return {};
    }
    return {{10, 20, "bad"}};
  }

  std::atomic<bool> called{false};
  std::thread::id worker_thread{};
};

}  // namespace

int main() {
  using namespace obs_whisperbleep;

  const auto platform = platform::current_platform();
  expect(!platform.name().empty(), "reports a platform name");

  runtime::StubWhisperRuntime runtime;
  expect(runtime.initialize("not-loaded-in-M0") == runtime::RuntimeStatus::unavailable,
         "keeps runtime explicitly unavailable in M0");
  const auto transcript = runtime.transcribe(nullptr, 0, 48000);
  expect(transcript.empty(), "stub runtime does not invent transcription");

  core::AudioBuffer input{48000, 1, {1.F, 1.F, 1.F, 1.F}};
  core::AudioBuffer replacement{48000, 1, {0.F}};
  core::AudioPipeline pipeline;
  const auto output = pipeline.process(core::AudioFrame{0, input}, {{1, 3}},
                                       replacement);
  expect(output.audio.samples[0] == 1.F && output.audio.samples[1] == 0.F &&
             output.audio.samples[2] == 0.F && output.audio.samples[3] == 1.F,
         "connects deterministic scheduling and rendering");

  MatchingRuntime matching_runtime;
  core::EndToEndAudioResult asynchronous_result;
  std::mutex result_mutex;
  std::condition_variable result_ready;
  bool has_result = false;
  core::EndToEndAudioPipelineConfig asynchronous_config;
  asynchronous_config.sample_rate = 100;
  asynchronous_config.phrases = {"bad"};
  asynchronous_config.queue_capacity = 2;
  asynchronous_config.default_beep.frequency_hz = 25.0;
  core::EndToEndAudioPipeline asynchronous_pipeline(
      matching_runtime, asynchronous_config,
      [&](core::EndToEndAudioResult result) {
        std::lock_guard<std::mutex> lock(result_mutex);
        asynchronous_result = std::move(result);
        has_result = true;
        result_ready.notify_one();
      });
  expect(asynchronous_pipeline.start(), "starts the end-to-end worker");
  const auto callback_thread = std::this_thread::get_id();
  expect(asynchronous_pipeline.submit(
             core::AudioFrame{1000, core::AudioBuffer{100, 2,
                                                        std::vector<float>(200, 1.F)}}),
         "accepts an audio chunk without invoking the runtime inline");
  {
    std::unique_lock<std::mutex> lock(result_mutex);
    expect(result_ready.wait_for(lock, std::chrono::seconds(1),
                                 [&] { return has_result; }),
           "emits an asynchronous end-to-end result");
  }
  asynchronous_pipeline.stop();
  expect(matching_runtime.called.load() &&
             matching_runtime.worker_thread != callback_thread,
         "runs runtime transcription outside the submitting thread");
  expect(asynchronous_result.status == core::EndToEndStatus::processed &&
             asynchronous_result.timeline_status == core::TimelineStatus::anchored,
         "processes a valid chunk through the timeline");
  expect(asynchronous_result.transcript.size() == 1 &&
             asynchronous_result.transcript[0].start_frame == 1010 &&
             asynchronous_result.intervals.size() == 1 &&
             asynchronous_result.intervals[0].start_frame == 1010 &&
             asynchronous_result.intervals[0].end_frame == 1020,
         "maps matching runtime timestamps to absolute censor intervals");
  expect(asynchronous_result.output.audio.samples[0] == 1.F &&
             asynchronous_result.output.audio.samples[11] == 1.F &&
             asynchronous_result.output.audio.samples[20] != 1.F,
         "uses the default beep to render only the matched interval");
  expect(asynchronous_result.latency.valid,
         "attaches monotonic end-to-end latency metadata");

  return EXIT_SUCCESS;
}
