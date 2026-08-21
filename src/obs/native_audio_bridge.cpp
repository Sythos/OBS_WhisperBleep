// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/native_audio_bridge.hpp"

#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"
#include "obs_whisperbleep/platform/platform_info.hpp"
#include "obs_whisperbleep/runtime/persistent_process_runner.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::obs {
namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
constexpr std::uint64_t kProcessingDelayNanoseconds = 1'500'000'000ULL;
constexpr std::uint64_t kTimestampJumpNanoseconds = 1'000'000'000ULL;
constexpr std::size_t kResultQueueCapacity = 96;
constexpr std::size_t kMaximumOutputFrames = 4096;

struct RawPacketSlot {
  bool valid = false;
  std::int64_t first_frame = 0;
  std::uint64_t timestamp = 0;
  std::uint32_t frames = 0;
  std::uint16_t channels = 0;
  std::array<std::vector<float>, MAX_AV_PLANES> planes;
};

[[nodiscard]] std::vector<std::string> split_phrases(
    const std::string_view value) {
  std::vector<std::string> phrases;
  std::string current;
  const auto append = [&phrases](std::string phrase) {
    const auto first = phrase.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      return;
    }
    const auto last = phrase.find_last_not_of(" \t\r\n");
    phrase = phrase.substr(first, last - first + 1);
    if (!phrase.empty()) {
      phrases.push_back(std::move(phrase));
    }
  };

  for (const char character : value) {
    if (character == ',' || character == ';' || character == '\n' ||
        character == '\r') {
      append(std::move(current));
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  append(std::move(current));
  return phrases;
}

[[nodiscard]] std::uint64_t duration_nanoseconds(
    const std::uint32_t frames, const std::uint32_t sample_rate) noexcept {
  if (sample_rate == 0) {
    return 0;
  }
  const auto frame_count = static_cast<std::uint64_t>(frames);
  if (frame_count > std::numeric_limits<std::uint64_t>::max() /
                         kNanosecondsPerSecond) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return frame_count * kNanosecondsPerSecond / sample_rate;
}

[[nodiscard]] std::uint16_t channel_count(
    const obs_audio_data& audio) noexcept {
  std::uint16_t channels = 0;
  for (; channels < MAX_AV_PLANES; ++channels) {
    if (audio.data[channels] == nullptr) {
      break;
    }
  }
  return channels;
}

[[nodiscard]] core::EndToEndAudioPipelineConfig make_pipeline_config(
    const FilterSettings& settings, const std::uint32_t sample_rate) {
  core::EndToEndAudioPipelineConfig config;
  config.queue_capacity = kResultQueueCapacity;
  config.sample_rate = sample_rate;
  config.phrases = split_phrases(settings.phrases);
  // An empty replacement asks the core to generate its bounded synthetic beep.
  config.replacement = {};
  return config;
}

[[nodiscard]] std::string bridge_script_path() {
  if (const char* configured =
          std::getenv("OBS_WHISPERBLEEP_BRIDGE_SCRIPT");
      configured != nullptr && configured[0] != '\0') {
    return configured;
  }

  if (char* module_file = obs_module_file("runtime/openai_whisper_bridge.py");
      module_file != nullptr) {
    std::string result(module_file);
    bfree(module_file);
    return result;
  }

  return "runtime/openai_whisper_bridge.py";
}

}  // namespace

class NativeAudioBridge::Impl final {
 public:
  Impl(FilterSettings settings, const std::uint32_t sample_rate,
       const std::uint16_t channels,
       std::unique_ptr<runtime::IWhisperRuntime> runtime)
      : settings_(std::move(settings)),
        sample_rate_(sample_rate == 0 ? 48000 : sample_rate),
        channels_(channels == 0 ? 1 : channels),
        runtime_(runtime == nullptr ? make_default_native_runtime()
                                     : std::move(runtime)),
        enabled_(settings_.enabled) {
    for (auto& plane : output_planes_) {
      plane.reserve(kMaximumOutputFrames);
    }
    for (auto& slot : raw_slots_) {
      for (auto& plane : slot.planes) {
        plane.resize(kMaximumOutputFrames);
      }
    }
    pipeline_ = build_pipeline(settings_);
  }

  ~Impl() {
    stop();
    pipeline_.reset();
    clear_results_waiting();
  }

  [[nodiscard]] bool start() {
    if (pipeline_ == nullptr) {
      return false;
    }
    if (!pipeline_->start()) {
      return false;
    }
    runtime_ready_.store(false, std::memory_order_release);
    try {
      runtime_thread_ = std::thread([this] { initialize_runtime(); });
    } catch (...) {
      pipeline_->stop();
      return false;
    }
    return true;
  }

  void stop() noexcept {
    if (pipeline_ != nullptr) {
      runtime_ready_.store(false, std::memory_order_release);
      pipeline_->stop();
    }
    if (runtime_thread_.joinable()) {
      runtime_thread_.join();
    }
  }

  void update(FilterSettings settings) {
    // OBS serializes settings updates with source lifecycle callbacks. Stop
    // before rebuilding so no worker callback can outlive the old pipeline.
    stop();
    clear_results_waiting();
    settings_ = std::move(settings);
    enabled_.store(settings_.enabled, std::memory_order_release);
    reset_stream();
    pipeline_.reset();
    pipeline_ = build_pipeline(settings_);
    if (enabled_.load(std::memory_order_acquire)) {
      (void)start();
    }
  }

  [[nodiscard]] obs_audio_data* filter_audio(obs_audio_data* audio) noexcept {
    if (audio == nullptr || !enabled_.load(std::memory_order_acquire) ||
        pipeline_ == nullptr || !pipeline_->running() || audio->frames == 0) {
      return audio;
    }

    try {
      const auto channels = channel_count(*audio);
      if (channels == 0 || channels != channels_ ||
          channels > MAX_AV_PLANES || audio->frames > kMaximumOutputFrames) {
        return audio;
      }

      const auto frame_index = begin_stream_block(*audio);
      if (stream_discontinuity_) {
        for (auto& slot : raw_slots_) {
          slot.valid = false;
        }
        raw_read_index_ = 0;
        raw_write_index_ = 0;
        raw_count_ = 0;
      }
      if (!capture_raw(*audio, frame_index, channels)) {
        return audio;
      }
      core::AudioFrame frame;
      frame.first_frame = frame_index;
      frame.discontinuity = stream_discontinuity_;
      frame.audio.sample_rate = sample_rate_;
      frame.audio.channels = channels;
      const auto sample_count = static_cast<std::size_t>(audio->frames) *
                                static_cast<std::size_t>(channels);
      frame.audio.samples.resize(sample_count);
      for (std::size_t frame_offset = 0; frame_offset < audio->frames;
           ++frame_offset) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
          const auto* samples = reinterpret_cast<const float*>(
              audio->data[channel]);
          frame.audio.samples[frame_offset * channels + channel] =
              samples[frame_offset];
        }
      }

      // submit() is bounded and non-blocking. A rejected frame remains safe
      // pass-through audio and is counted by the core queue.
      (void)pipeline_->submit(std::move(frame));

      const auto current_end = audio->timestamp +
                               duration_nanoseconds(audio->frames, sample_rate_);
      return consume_ready(audio, current_end);
    } catch (...) {
      // No exception may cross the OBS realtime boundary. Returning the host
      // packet is the deterministic failure policy for allocation or format
      // errors.
      return audio;
    }
  }

  [[nodiscard]] bool running() const noexcept {
    return pipeline_ != nullptr && pipeline_->running();
  }

  [[nodiscard]] std::size_t queued_frames() const noexcept {
    return pipeline_ == nullptr ? 0 : pipeline_->queued_frames();
  }

  [[nodiscard]] std::size_t dropped_frames() const noexcept {
    return pipeline_ == nullptr ? 0 : pipeline_->dropped_frames();
  }

  [[nodiscard]] std::size_t dropped_results() const noexcept {
    return dropped_results_.load(std::memory_order_relaxed);
  }

 private:
  [[nodiscard]] std::unique_ptr<core::EndToEndAudioPipeline> build_pipeline(
      const FilterSettings& settings) {
    auto config = make_pipeline_config(settings, sample_rate_);
    return std::make_unique<core::EndToEndAudioPipeline>(
        *runtime_, std::move(config),
        [this](core::EndToEndAudioResult result) noexcept {
          on_result(std::move(result));
        });
  }

  void initialize_runtime() noexcept {
    try {
      const auto cache_root =
          platform::user_cache_directory("Sythos/OBS-WhisperBleep");
      if (cache_root.empty() || settings_.model.empty()) {
        return;
      }
      const auto model_path =
          cache_root / (settings_.model + std::string(".model"));
      const auto status = runtime_->initialize(model_path.string(),
                                               settings_.language);
      runtime_ready_.store(status == runtime::RuntimeStatus::ready,
                            std::memory_order_release);
    } catch (...) {
      runtime_ready_.store(false, std::memory_order_release);
    }
  }

  void on_result(core::EndToEndAudioResult result) noexcept {
    // The worker must never wait behind the realtime consumer. If the
    // consumer currently owns the exchange, drop this result and let OBS keep
    // the original block.
    if (result_lock_.test_and_set(std::memory_order_acquire)) {
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    try {
      if (result_reset_pending_.exchange(false, std::memory_order_acq_rel)) {
        results_.clear();
      }
      if (results_.size() >= kResultQueueCapacity) {
        results_.pop_front();
        dropped_results_.fetch_add(1, std::memory_order_relaxed);
      }
      results_.push_back(std::move(result));
    } catch (...) {
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
    }
    result_lock_.clear(std::memory_order_release);
  }

  [[nodiscard]] obs_audio_data* consume_ready(
      obs_audio_data* original, const std::uint64_t current_end) noexcept {
    // test_and_set is deliberately not a spinlock: the callback immediately
    // falls back to the original OBS packet if the worker is publishing.
    if (result_lock_.test_and_set(std::memory_order_acquire)) {
      return original;
    }

    obs_audio_data* output = original;
    try {
      if (result_reset_pending_.exchange(false, std::memory_order_acq_rel)) {
        results_.clear();
      }
      if (raw_count_ == 0) {
        result_lock_.clear(std::memory_order_release);
        return original;
      }

      RawPacketSlot& slot = raw_slots_[raw_read_index_];
      const auto slot_end = slot.timestamp +
                            duration_nanoseconds(slot.frames, sample_rate_);
      if (current_end < slot_end ||
          current_end - slot_end < kProcessingDelayNanoseconds) {
        // Keep the first 1.5 seconds silent while the delay ring fills. This
        // is the only point at which the bridge returns nullptr: afterwards
        // exactly one delayed block is returned for each incoming block.
        result_lock_.clear(std::memory_order_release);
        return nullptr;
      }

      while (!results_.empty() &&
             results_.front().output.first_frame < slot.first_frame) {
        results_.pop_front();
      }

      bool copied = false;
      if (!results_.empty() &&
          results_.front().output.first_frame == slot.first_frame) {
        auto result = std::move(results_.front());
        results_.pop_front();
        copied = copy_to_output(result, slot.timestamp);
      }
      if (!copied) {
        copied = copy_raw_to_output(slot);
      }

      slot.valid = false;
      raw_read_index_ = (raw_read_index_ + 1U) % kResultQueueCapacity;
      --raw_count_;
      if (copied) {
        output = &output_packet_;
      }
    } catch (...) {
      output = original;
    }
    result_lock_.clear(std::memory_order_release);
    return output;
  }

  [[nodiscard]] bool copy_to_output(
      const core::EndToEndAudioResult& result,
      const std::uint64_t timestamp) noexcept {
    const auto& audio = result.output.audio;
    if (audio.sample_rate != sample_rate_ || audio.channels != channels_ ||
        audio.channels == 0 || audio.frame_count() == 0 ||
        audio.frame_count() > kMaximumOutputFrames ||
        audio.samples.size() != audio.frame_count() * audio.channels) {
      return false;
    }

    for (std::size_t channel = 0; channel < channels_; ++channel) {
      output_planes_[channel].resize(audio.frame_count());
      for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
        output_planes_[channel][frame] =
            audio.samples[frame * audio.channels + channel];
      }
      output_packet_.data[channel] = reinterpret_cast<std::uint8_t*>(
          output_planes_[channel].data());
    }
    for (std::size_t channel = channels_; channel < MAX_AV_PLANES; ++channel) {
      output_packet_.data[channel] = nullptr;
    }
    output_packet_.frames = static_cast<std::uint32_t>(audio.frame_count());
    output_packet_.timestamp = timestamp;
    return true;
  }

  [[nodiscard]] bool copy_raw_to_output(
      const RawPacketSlot& slot) noexcept {
    if (!slot.valid || slot.channels != channels_ || slot.frames == 0 ||
        slot.frames > kMaximumOutputFrames) {
      return false;
    }
    for (std::size_t channel = 0; channel < channels_; ++channel) {
      if (slot.planes[channel].size() < slot.frames) {
        return false;
      }
      output_planes_[channel].resize(slot.frames);
      std::copy_n(slot.planes[channel].data(), slot.frames,
                  output_planes_[channel].data());
      output_packet_.data[channel] = reinterpret_cast<std::uint8_t*>(
          output_planes_[channel].data());
    }
    for (std::size_t channel = channels_; channel < MAX_AV_PLANES; ++channel) {
      output_packet_.data[channel] = nullptr;
    }
    output_packet_.frames = slot.frames;
    output_packet_.timestamp = slot.timestamp;
    return true;
  }

  [[nodiscard]] bool capture_raw(const obs_audio_data& audio,
                                 const std::int64_t first_frame,
                                 const std::uint16_t channels) noexcept {
    if (raw_count_ >= kResultQueueCapacity) {
      // Preserve bounded memory and recover deterministically. The current
      // packet remains pass-through; the next packet starts a fresh delay
      // generation rather than overwriting audio still awaiting output.
      for (auto& slot : raw_slots_) {
        slot.valid = false;
      }
      raw_read_index_ = 0;
      raw_write_index_ = 0;
      raw_count_ = 0;
      result_reset_pending_.store(true, std::memory_order_release);
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    RawPacketSlot& slot = raw_slots_[raw_write_index_];
    if (slot.valid) {
      return false;
    }
    slot.valid = true;
    slot.first_frame = first_frame;
    slot.timestamp = audio.timestamp;
    slot.frames = audio.frames;
    slot.channels = channels;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      auto& destination = slot.planes[channel];
      if (destination.size() < audio.frames) {
        return false;
      }
      const auto* source = reinterpret_cast<const float*>(audio.data[channel]);
      if (source == nullptr) {
        slot.valid = false;
        return false;
      }
      std::copy_n(source, audio.frames, destination.data());
    }
    for (std::size_t channel = channels; channel < MAX_AV_PLANES; ++channel) {
      slot.planes[channel].clear();
    }
    raw_write_index_ = (raw_write_index_ + 1U) % kResultQueueCapacity;
    ++raw_count_;
    return true;
  }

  [[nodiscard]] std::int64_t begin_stream_block(
      const obs_audio_data& audio) noexcept {
    const auto duration = duration_nanoseconds(audio.frames, sample_rate_);
    bool discontinuity = false;
    if (!has_stream_) {
      base_timestamp_ = audio.timestamp;
      has_stream_ = true;
    } else if (audio.timestamp < last_end_timestamp_ ||
               audio.timestamp - last_end_timestamp_ >
                   kTimestampJumpNanoseconds) {
      discontinuity = true;
      base_timestamp_ = audio.timestamp;
      next_frame_ = 0;
      result_reset_pending_.store(true, std::memory_order_release);
    }

    stream_discontinuity_ = discontinuity;
    const auto first_frame = next_frame_;
    if (audio.frames >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
            static_cast<std::uint64_t>(std::max<std::int64_t>(
                first_frame, 0))) {
      next_frame_ = 0;
      stream_discontinuity_ = true;
      result_reset_pending_.store(true, std::memory_order_release);
    } else {
      next_frame_ += static_cast<std::int64_t>(audio.frames);
    }
    last_end_timestamp_ = audio.timestamp + duration;
    return stream_discontinuity_ && first_frame != 0 ? 0 : first_frame;
  }

  void reset_stream() noexcept {
    has_stream_ = false;
    stream_discontinuity_ = false;
    base_timestamp_ = 0;
    last_end_timestamp_ = 0;
    next_frame_ = 0;
    for (auto& slot : raw_slots_) {
      slot.valid = false;
    }
    raw_read_index_ = 0;
    raw_write_index_ = 0;
    raw_count_ = 0;
    result_reset_pending_.store(true, std::memory_order_release);
  }

  void clear_results_waiting() noexcept {
    while (result_lock_.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    results_.clear();
    result_reset_pending_.store(false, std::memory_order_release);
    result_lock_.clear(std::memory_order_release);
  }

  FilterSettings settings_;
  std::uint32_t sample_rate_ = 48000;
  std::uint16_t channels_ = 1;
  std::unique_ptr<runtime::IWhisperRuntime> runtime_;
  std::unique_ptr<core::EndToEndAudioPipeline> pipeline_;
  std::atomic_bool enabled_{true};
  std::atomic_bool runtime_ready_{false};
  std::thread runtime_thread_;

  std::atomic_flag result_lock_ = ATOMIC_FLAG_INIT;
  std::atomic_bool result_reset_pending_{false};
  std::deque<core::EndToEndAudioResult> results_;
  std::atomic<std::size_t> dropped_results_{0};

  bool has_stream_ = false;
  bool stream_discontinuity_ = false;
  std::uint64_t base_timestamp_ = 0;
  std::uint64_t last_end_timestamp_ = 0;
  std::int64_t next_frame_ = 0;

  std::array<std::vector<float>, MAX_AV_PLANES> output_planes_;
  obs_audio_data output_packet_{};
  std::array<RawPacketSlot, kResultQueueCapacity> raw_slots_;
  std::size_t raw_read_index_ = 0;
  std::size_t raw_write_index_ = 0;
  std::size_t raw_count_ = 0;
};

NativeAudioBridge::NativeAudioBridge(
    FilterSettings settings, const std::uint32_t sample_rate,
    const std::uint16_t channels,
    std::unique_ptr<runtime::IWhisperRuntime> runtime)
    : impl_(std::make_unique<Impl>(std::move(settings), sample_rate, channels,
                                   std::move(runtime))) {}

NativeAudioBridge::~NativeAudioBridge() = default;

bool NativeAudioBridge::start() { return impl_ != nullptr && impl_->start(); }

void NativeAudioBridge::stop() noexcept {
  if (impl_ != nullptr) {
    impl_->stop();
  }
}

void NativeAudioBridge::update(FilterSettings settings) {
  if (impl_ != nullptr) {
    impl_->update(std::move(settings));
  }
}

obs_audio_data* NativeAudioBridge::filter_audio(obs_audio_data* audio) noexcept {
  return impl_ == nullptr ? audio : impl_->filter_audio(audio);
}

bool NativeAudioBridge::running() const noexcept {
  return impl_ != nullptr && impl_->running();
}

std::size_t NativeAudioBridge::queued_frames() const noexcept {
  return impl_ == nullptr ? 0 : impl_->queued_frames();
}

std::size_t NativeAudioBridge::dropped_frames() const noexcept {
  return impl_ == nullptr ? 0 : impl_->dropped_frames();
}

std::size_t NativeAudioBridge::dropped_results() const noexcept {
  return impl_ == nullptr ? 0 : impl_->dropped_results();
}

std::unique_ptr<runtime::IWhisperRuntime> make_default_native_runtime() {
  runtime::OpenAIWhisperRuntimeConfig config;
  config.python_executable = "python";
  config.bridge_script_path = bridge_script_path();
  config.runner = runtime::make_persistent_whisper_process_runner();
  return std::make_unique<runtime::OpenAIWhisperRuntime>(std::move(config));
}

}  // namespace obs_whisperbleep::obs
