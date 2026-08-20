// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "obs_whisperbleep/core/censor_scheduler.hpp"
#include "obs_whisperbleep/core/match_planner.hpp"
#include "obs_whisperbleep/core/replacement_renderer.hpp"
#include "obs_whisperbleep/core/synthetic_replacement.hpp"
#include "obs_whisperbleep/core/audio_timeline.hpp"
#include "obs_whisperbleep/diagnostics/latency.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::core {

struct AudioFrame {
  std::int64_t first_frame = 0;
  AudioBuffer audio;
  diagnostics::MonotonicTimePoint ingress_time{};
  bool discontinuity = false;
};

struct AudioPipelineConfig {
  std::size_t max_buffered_frames = 48000;
  std::size_t replacement_fade_frames = 0;
};

/**
 * Synchronous M0 audio path. It deliberately has no model or thread
 * dependency; a later milestone can feed it intervals produced by a worker.
 */
class AudioPipeline {
 public:
  explicit AudioPipeline(AudioPipelineConfig config = {});

  [[nodiscard]] AudioFrame process(const AudioFrame& input,
                                   const std::vector<CensorInterval>& intervals,
                                   const AudioBuffer& replacement) const;
  [[nodiscard]] const AudioPipelineConfig& config() const noexcept;

 private:
  AudioPipelineConfig config_;
};

/** The outcome of one asynchronously processed audio chunk. */
enum class EndToEndStatus {
  processed,
  pass_through_invalid_audio,
  pass_through_timeline_rejected,
  pass_through_runtime_failure,
};

/**
 * Configures the dependency-free orchestration around an injected runtime.
 *
 * The configured sample rate is a strict contract. Chunks at any other rate
 * safely pass through, avoiding implicit resampling or timestamp conversion.
 */
struct EndToEndAudioPipelineConfig {
  std::size_t queue_capacity = 4;
  std::uint32_t sample_rate = 48000;
  std::vector<std::string> phrases;
  MatchPlannerConfig matching{};
  AudioPipelineConfig rendering{};
  AudioBuffer replacement;
  BeepOptions default_beep{};
};

struct EndToEndAudioResult {
  EndToEndStatus status = EndToEndStatus::pass_through_invalid_audio;
  TimelineStatus timeline_status = TimelineStatus::invalid_chunk;
  AudioFrame output;
  std::vector<runtime::TranscriptSegment> transcript;
  std::vector<CensorInterval> intervals;
  diagnostics::LatencyTimestamps latency_timestamps;
  diagnostics::LatencyMeasurement latency;
};

using EndToEndResultCallback = std::function<void(EndToEndAudioResult)>;

/**
 * Bounded asynchronous path from captured audio to rendered replacement.
 *
 * submit() is the realtime boundary: it timestamps and enqueues a frame but
 * never calls the runtime, matcher, renderer, or result callback. All costly
 * work runs on one dedicated worker, preserving chunk order and keeping the
 * timeline coordinator single-threaded. A rejected submission has no output;
 * the host must retain its original frame as the safe immediate pass-through.
 */
class EndToEndAudioPipeline {
 public:
  EndToEndAudioPipeline(runtime::IWhisperRuntime& runtime,
                         EndToEndAudioPipelineConfig config,
                         EndToEndResultCallback callback);
  ~EndToEndAudioPipeline();

  EndToEndAudioPipeline(const EndToEndAudioPipeline&) = delete;
  EndToEndAudioPipeline& operator=(const EndToEndAudioPipeline&) = delete;

  [[nodiscard]] bool start();
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept;

  /** Non-blocking realtime submission; false means retain the original audio. */
  [[nodiscard]] bool submit(AudioFrame frame) noexcept;
  [[nodiscard]] std::size_t queued_frames() const noexcept;
  [[nodiscard]] std::size_t dropped_frames() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace obs_whisperbleep::core
