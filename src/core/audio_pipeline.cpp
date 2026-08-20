// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_pipeline.hpp"

#include "obs_whisperbleep/core/audio_processing_worker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace obs_whisperbleep::core {

AudioPipeline::AudioPipeline(AudioPipelineConfig config) : config_(config) {
  if (config_.max_buffered_frames == 0) {
    config_.max_buffered_frames = 1;
  }
}

AudioFrame AudioPipeline::process(
    const AudioFrame& input, const std::vector<CensorInterval>& intervals,
    const AudioBuffer& replacement) const {
  AudioFrame output = input;
  output.audio = ReplacementRenderer::render(
      input.audio, intervals, replacement,
      RenderOptions{config_.replacement_fade_frames});
  return output;
}

const AudioPipelineConfig& AudioPipeline::config() const noexcept {
  return config_;
}

namespace {

[[nodiscard]] bool valid_audio(const AudioBuffer& audio,
                               const std::uint32_t expected_sample_rate) {
  if (audio.sample_rate != expected_sample_rate || audio.channels == 0 ||
      audio.frame_count() == 0 ||
      audio.samples.size() % audio.channels != 0) {
    return false;
  }
  return std::all_of(audio.samples.begin(), audio.samples.end(),
                     [](const float sample) { return std::isfinite(sample); });
}

[[nodiscard]] bool usable_replacement(const AudioBuffer& replacement,
                                      const AudioBuffer& input) {
  return replacement.sample_rate == input.sample_rate &&
         replacement.channels != 0 && replacement.frame_count() != 0 &&
         replacement.samples.size() % replacement.channels == 0;
}

[[nodiscard]] AudioBuffer to_mono(const AudioBuffer& input) {
  if (input.channels == 1) {
    AudioBuffer mono = input;
    mono.samples.resize(input.frame_count());
    return mono;
  }

  AudioBuffer mono{input.sample_rate, 1, {}};
  const auto frames = input.frame_count();
  mono.samples.resize(frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float sum = 0.0F;
    for (std::size_t channel = 0; channel < input.channels; ++channel) {
      sum += input.samples[frame * input.channels + channel];
    }
    mono.samples[frame] = sum / static_cast<float>(input.channels);
  }
  return mono;
}

[[nodiscard]] AudioBuffer default_beep_for(const AudioBuffer& input,
                                           const BeepOptions options) {
  const auto quarter_second = std::max<std::size_t>(
      1, static_cast<std::size_t>(input.sample_rate) / 4);
  return SyntheticReplacement::beep(
      input.sample_rate, input.channels,
      std::min(input.frame_count(), quarter_second), options);
}

[[nodiscard]] EndToEndAudioPipelineConfig normalize_config(
    EndToEndAudioPipelineConfig config) {
  if (config.queue_capacity == 0) {
    config.queue_capacity = 1;
  }
  if (config.sample_rate == 0) {
    config.sample_rate = 48000;
  }
  config.matching.timestamp.sample_rate = config.sample_rate;
  return config;
}

}  // namespace

class EndToEndAudioPipeline::Impl {
 public:
  Impl(runtime::IWhisperRuntime& runtime, EndToEndAudioPipelineConfig config,
       EndToEndResultCallback callback)
      : runtime_(runtime), config_(normalize_config(std::move(config))),
        callback_(std::move(callback)),
        renderer_(config_.rendering), planner_(config_.phrases, config_.matching),
        timeline_(AudioTimelineConfig{config_.sample_rate}),
        worker_(config_.queue_capacity,
                [this](AudioFrame frame) { process(std::move(frame)); }) {}

  [[nodiscard]] bool start() {
    if (worker_.running()) {
      return false;
    }
    timeline_.reset();
    return worker_.start();
  }

  void stop() noexcept { worker_.stop(); }

  [[nodiscard]] bool running() const noexcept { return worker_.running(); }

  [[nodiscard]] std::size_t queued_frames() const noexcept {
    return worker_.queued_frames();
  }

  [[nodiscard]] std::size_t dropped_frames() const noexcept {
    return worker_.dropped_frames();
  }

  [[nodiscard]] bool submit(AudioFrame frame) noexcept {
    if (frame.ingress_time == diagnostics::MonotonicTimePoint{}) {
      frame.ingress_time = diagnostics::MonotonicClock::now();
    }
    return worker_.submit(std::move(frame));
  }

  void process(AudioFrame frame) noexcept {
    const auto processing_start = diagnostics::MonotonicClock::now();
    EndToEndAudioResult result;
    result.output = frame;

    if (!valid_audio(frame.audio, config_.sample_rate) ||
        frame.first_frame < 0 ||
        frame.audio.frame_count() >
            static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
      complete(std::move(result), frame.ingress_time, processing_start,
               diagnostics::MonotonicClock::now());
      return;
    }

    const auto update = timeline_.accept(TimelineChunk{
        frame.first_frame, static_cast<std::int64_t>(frame.audio.frame_count()),
        frame.audio.sample_rate, frame.discontinuity});
    result.timeline_status = update.status;
    if (!update.anchor.has_value()) {
      result.status = EndToEndStatus::pass_through_timeline_rejected;
      complete(std::move(result), frame.ingress_time, processing_start,
               diagnostics::MonotonicClock::now());
      return;
    }

    try {
      const auto mono = to_mono(frame.audio);
      const auto relative_transcript = runtime_.transcribe(
          mono.samples.data(), mono.samples.size(), mono.sample_rate);
      const auto relative_intervals = planner_.plan(relative_transcript);
      result.transcript = timeline_.to_absolute(*update.anchor,
                                                relative_transcript);
      result.intervals = timeline_.to_absolute(*update.anchor,
                                               relative_intervals);

      const auto replacement = usable_replacement(config_.replacement,
                                                  frame.audio)
                                   ? config_.replacement
                                   : default_beep_for(frame.audio,
                                                      config_.default_beep);
      result.output = renderer_.process(frame, relative_intervals, replacement);
      result.status = EndToEndStatus::processed;
    } catch (...) {
      result.status = EndToEndStatus::pass_through_runtime_failure;
      result.output = frame;
      result.transcript.clear();
      result.intervals.clear();
    }

    complete(std::move(result), frame.ingress_time, processing_start,
             diagnostics::MonotonicClock::now());
  }

 private:
  void complete(EndToEndAudioResult result,
                const diagnostics::MonotonicTimePoint ingress,
                const diagnostics::MonotonicTimePoint processing_start,
                const diagnostics::MonotonicTimePoint replacement_ready)
      noexcept {
    const auto output_time = diagnostics::MonotonicClock::now();
    result.latency_timestamps = diagnostics::LatencyTimestamps{
        ingress, processing_start, replacement_ready, output_time};
    result.latency = latency_evaluator_.measure(result.latency_timestamps);
    if (!callback_) {
      return;
    }
    try {
      callback_(std::move(result));
    } catch (...) {
      // The host callback is an integration boundary and must not kill the
      // worker. Subsequent chunks remain safe pass-through candidates.
    }
  }

  runtime::IWhisperRuntime& runtime_;
  EndToEndAudioPipelineConfig config_;
  EndToEndResultCallback callback_;
  AudioPipeline renderer_;
  MatchPlanner planner_;
  AudioTimelineCoordinator timeline_;
  diagnostics::LatencyEvaluator latency_evaluator_;
  AudioProcessingWorker worker_;
};

EndToEndAudioPipeline::EndToEndAudioPipeline(
    runtime::IWhisperRuntime& runtime, EndToEndAudioPipelineConfig config,
    EndToEndResultCallback callback)
    : impl_(std::make_unique<Impl>(runtime, std::move(config),
                                   std::move(callback))) {}

EndToEndAudioPipeline::~EndToEndAudioPipeline() = default;

bool EndToEndAudioPipeline::start() { return impl_->start(); }

void EndToEndAudioPipeline::stop() noexcept { impl_->stop(); }

bool EndToEndAudioPipeline::running() const noexcept {
  return impl_->running();
}

bool EndToEndAudioPipeline::submit(AudioFrame frame) noexcept {
  return impl_->submit(std::move(frame));
}

std::size_t EndToEndAudioPipeline::queued_frames() const noexcept {
  return impl_->queued_frames();
}

std::size_t EndToEndAudioPipeline::dropped_frames() const noexcept {
  return impl_->dropped_frames();
}

}  // namespace obs_whisperbleep::core
