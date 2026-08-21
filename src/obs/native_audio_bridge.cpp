// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/native_audio_bridge.hpp"

#include <obs-module.h>
#include <obs.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
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
constexpr std::size_t kIngressQueueCapacity = 32;
constexpr std::size_t kMaximumOutputFrames = 4096;

using AudioPlane = std::array<float, kMaximumOutputFrames>;

/*
 * RawPacketSlot belongs exclusively to the OBS callback. It is deliberately
 * fixed-size: capture_raw() must not resize, allocate or free memory.
 */
struct RawPacketSlot {
  bool valid = false;
  std::int64_t first_frame = 0;
  std::uint64_t timestamp = 0;
  std::uint32_t frames = 0;
  std::uint16_t channels = 0;
  std::array<AudioPlane, MAX_AV_PLANES> planes{};
};

/*
 * IngressPacketSlot is a second preallocated pool. The callback copies a
 * packet here and publishes its index; submit_loop() later constructs the
 * vector-backed core AudioFrame away from the realtime thread.
 */
struct IngressPacketSlot {
  std::int64_t first_frame = 0;
  std::uint32_t frames = 0;
  std::uint16_t channels = 0;
  bool discontinuity = false;
  std::array<AudioPlane, MAX_AV_PLANES> planes{};
};

struct ResultSlot {
  /*
   * This optional is only emplaced/reset by the pipeline worker. The OBS
   * callback only reads it and advances result_read_index_ after copying the
   * samples into fixed output storage, so vector destruction never occurs on
   * the realtime thread.
   */
  std::optional<core::EndToEndAudioResult> result;
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

[[nodiscard]] std::uint64_t add_timestamp(
    const std::uint64_t timestamp, const std::uint64_t duration) noexcept {
  if (duration > std::numeric_limits<std::uint64_t>::max() - timestamp) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return timestamp + duration;
}

[[nodiscard]] std::size_t next_index(const std::size_t index,
                                     const std::size_t capacity) noexcept {
  return index + 1U == capacity ? 0U : index + 1U;
}

/*
 * OBS normalizes source audio before filter_audio() and exposes it as planar
 * float data only when the global audio output format is
 * AUDIO_FORMAT_FLOAT_PLANAR. obs_audio_data itself carries no format field,
 * so accepting an unknown format would reinterpret arbitrary bytes as float.
 */
[[nodiscard]] bool host_audio_matches(const std::uint32_t sample_rate,
                                      const std::uint16_t channels) noexcept {
  auto* output = obs_get_audio();
  if (output == nullptr) {
    return false;
  }
  const auto* info = audio_output_get_info(output);
  if (info == nullptr || info->format != AUDIO_FORMAT_FLOAT_PLANAR ||
      info->samples_per_sec != sample_rate) {
    return false;
  }
  const auto speaker_channels = get_audio_channels(info->speakers);
  const auto actual_channels =
      speaker_channels == 0 ? audio_output_get_channels(output)
                             : speaker_channels;
  return actual_channels == channels;
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
    pipeline_ = build_pipeline(settings_);
  }

  ~Impl() {
    stop();
    pipeline_.reset();
  }

  [[nodiscard]] bool start() {
    if (pipeline_ == nullptr || pipeline_->running()) {
      return false;
    }

    reset_ingress_queue();
    reset_stream();
    runtime_ready_.store(false, std::memory_order_release);
    ingress_stop_.store(false, std::memory_order_release);
    if (!pipeline_->start()) {
      ingress_stop_.store(true, std::memory_order_release);
      return false;
    }

    try {
      ingress_thread_ = std::thread([this] { submit_loop(); });
    } catch (...) {
      ingress_stop_.store(true, std::memory_order_release);
      pipeline_->stop();
      return false;
    }
    accepting_.store(true, std::memory_order_release);

    try {
      runtime_thread_ = std::thread([this] { initialize_runtime(); });
    } catch (...) {
      accepting_.store(false, std::memory_order_release);
      stop_ingress_thread();
      pipeline_->stop();
      return false;
    }
    return true;
  }

  void stop() noexcept {
    accepting_.store(false, std::memory_order_release);
    runtime_ready_.store(false, std::memory_order_release);
    stop_ingress_thread();

    /*
     * The submit adapter is joined before the core worker is stopped. This
     * guarantees that no adapter thread can call submit() after pipeline_ has
     * begun shutdown. EndToEndAudioPipeline::stop() may still wait for an
     * already accepted Whisper operation; that lifecycle limitation is
     * outside the OBS realtime callback and is documented for M7.
     */
    if (pipeline_ != nullptr) {
      pipeline_->stop();
    }
    if (runtime_thread_.joinable()) {
      runtime_thread_.join();
    }
    clear_results_waiting();
  }

  void update(FilterSettings settings) {
    // OBS serializes settings updates with source lifecycle callbacks. Stop
    // before rebuilding so no worker callback can outlive the old pipeline.
    stop();
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
    if (audio == nullptr || !accepting_.load(std::memory_order_acquire) ||
        !enabled_.load(std::memory_order_acquire) ||
        pipeline_ == nullptr || !pipeline_->running() || audio->frames == 0) {
      return audio;
    }

    /*
     * obs_audio_data has no format metadata. Pass through unless the current
     * OBS output explicitly reports planar float and every expected plane is
     * present. This prevents undefined reinterpretation of packed formats.
     */
    if (!host_audio_matches(sample_rate_, channels_) || channels_ == 0 ||
        channels_ > MAX_AV_PLANES || audio->frames > kMaximumOutputFrames) {
      return audio;
    }
    for (std::size_t channel = 0; channel < channels_; ++channel) {
      if (audio->data[channel] == nullptr) {
        return audio;
      }
    }

    try {
      const auto frame_index = begin_stream_block(*audio);
      if (stream_discontinuity_) {
        clear_raw_ring();
      }

      const bool raw_captured = capture_raw(*audio, frame_index, channels_);
      const bool ingress_captured =
          raw_captured && capture_ingress(*audio, frame_index, channels_);
      const auto current_end =
          add_timestamp(audio->timestamp,
                        duration_nanoseconds(audio->frames, sample_rate_));

      if (!raw_captured) {
        return audio;
      }

      /*
       * An ingress rejection is intentionally not a reason to return the
       * current packet immediately: the raw delay ring can still emit one
       * bounded delayed block and keeps audio/video timing stable.
       */
      (void)ingress_captured;
      return consume_ready(audio, current_end);
    } catch (...) {
      // No exception may cross the OBS realtime boundary.
      return audio;
    }
  }

  [[nodiscard]] bool running() const noexcept {
    return accepting_.load(std::memory_order_acquire) &&
           pipeline_ != nullptr && pipeline_->running();
  }

  [[nodiscard]] std::size_t queued_frames() const noexcept {
    return pipeline_ == nullptr ? 0 : pipeline_->queued_frames();
  }

  [[nodiscard]] std::size_t dropped_frames() const noexcept {
    const auto pipeline_dropped =
        pipeline_ == nullptr ? 0 : pipeline_->dropped_frames();
    return pipeline_dropped +
           dropped_ingress_packets_.load(std::memory_order_relaxed);
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

  void submit_loop() noexcept {
    for (;;) {
      /*
       * Pending adapter slots have not entered the core queue yet. Dropping
       * them during lifecycle shutdown is safe because their matching raw
       * packets remain pass-through candidates and it keeps stop bounded.
       */
      if (ingress_stop_.load(std::memory_order_acquire)) {
        return;
      }
      const auto read = ingress_read_index_.load(std::memory_order_relaxed);
      const auto write = ingress_write_index_.load(std::memory_order_acquire);
      if (read != write) {
        submit_packet(ingress_slots_[read]);
        ingress_read_index_.store(next_index(read, kIngressQueueCapacity),
                                  std::memory_order_release);
        continue;
      }

      /*
       * The callback only calls notify_one(); it never takes this mutex or
       * waits. The predicate closes the enqueue-before-wait race.
       */
      std::unique_lock lock(ingress_wait_mutex_);
      ingress_wait_.wait(lock, [this] {
        return ingress_stop_.load(std::memory_order_acquire) ||
               ingress_read_index_.load(std::memory_order_relaxed) !=
                   ingress_write_index_.load(std::memory_order_acquire);
      });
    }
  }

  void submit_packet(const IngressPacketSlot& slot) noexcept {
    if (!runtime_ready_.load(std::memory_order_acquire)) {
      submit_gap_pending_.store(true, std::memory_order_release);
      dropped_ingress_packets_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    try {
      core::AudioFrame frame;
      frame.first_frame = slot.first_frame;
      frame.discontinuity = slot.discontinuity;
      frame.audio.sample_rate = sample_rate_;
      frame.audio.channels = slot.channels;
      const auto sample_count =
          static_cast<std::size_t>(slot.frames) * slot.channels;
      frame.audio.samples.resize(sample_count);
      for (std::size_t frame_offset = 0; frame_offset < slot.frames;
           ++frame_offset) {
        for (std::size_t channel = 0; channel < slot.channels; ++channel) {
          frame.audio.samples[frame_offset * slot.channels + channel] =
              slot.planes[channel][frame_offset];
        }
      }

      if (!pipeline_->submit(std::move(frame))) {
        submit_gap_pending_.store(true, std::memory_order_release);
      }
    } catch (...) {
      submit_gap_pending_.store(true, std::memory_order_release);
      dropped_ingress_packets_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void stop_ingress_thread() noexcept {
    ingress_stop_.store(true, std::memory_order_release);
    ingress_wait_.notify_one();
    if (ingress_thread_.joinable()) {
      ingress_thread_.join();
    }
    reset_ingress_queue();
  }

  void reset_ingress_queue() noexcept {
    ingress_read_index_.store(0, std::memory_order_relaxed);
    ingress_write_index_.store(0, std::memory_order_relaxed);
    submit_gap_pending_.store(false, std::memory_order_release);
  }

  void on_result(core::EndToEndAudioResult result) noexcept {
    /*
     * A fixed SPSC handoff avoids a deque allocation in the OBS callback.
     * The worker owns result construction/destruction; the callback only
     * reads a ready slot and copies its samples to output_planes_.
     */
    const auto write = result_write_index_.load(std::memory_order_relaxed);
    const auto next = next_index(write, kResultQueueCapacity);
    const auto read = result_read_index_.load(std::memory_order_acquire);
    if (next == read) {
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    auto& slot = result_slots_[write];
    try {
      slot.result.reset();
      slot.result.emplace(std::move(result));
      result_write_index_.store(next, std::memory_order_release);
    } catch (...) {
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] obs_audio_data* consume_ready(
      obs_audio_data* original, const std::uint64_t current_end) noexcept {
    obs_audio_data* output = original;
    try {
      if (result_reset_pending_.exchange(false, std::memory_order_acq_rel)) {
        discard_results();
      }
      if (raw_count_ == 0) {
        return original;
      }

      RawPacketSlot& slot = raw_slots_[raw_read_index_];
      const auto slot_end =
          add_timestamp(slot.timestamp,
                        duration_nanoseconds(slot.frames, sample_rate_));
      if (current_end < slot_end ||
          current_end - slot_end < kProcessingDelayNanoseconds) {
        // Keep the first 1.5 seconds silent while the delay ring fills.
        return nullptr;
      }

      auto result_index = result_read_index_.load(std::memory_order_relaxed);
      const auto result_end =
          result_write_index_.load(std::memory_order_acquire);
      while (result_index != result_end) {
        auto& result_slot = result_slots_[result_index];
        if (!result_slot.result.has_value()) {
          result_index = next_index(result_index, kResultQueueCapacity);
          result_read_index_.store(result_index, std::memory_order_release);
          continue;
        }
        const auto result_first = result_slot.result->output.first_frame;
        if (result_first < slot.first_frame) {
          result_index = next_index(result_index, kResultQueueCapacity);
          result_read_index_.store(result_index, std::memory_order_release);
          continue;
        }
        if (result_first == slot.first_frame) {
          const bool copied =
              copy_to_output(*result_slot.result, slot.timestamp);
          result_index = next_index(result_index, kResultQueueCapacity);
          result_read_index_.store(result_index, std::memory_order_release);
          if (copied) {
            output = &output_packet_;
          }
        }
        break;
      }

      if (output == original) {
        (void)copy_raw_to_output(slot);
      }

      slot.valid = false;
      raw_read_index_ = next_index(raw_read_index_, kResultQueueCapacity);
      --raw_count_;
      return output;
    } catch (...) {
      return original;
    }
  }

  void discard_results() noexcept {
    const auto write = result_write_index_.load(std::memory_order_acquire);
    result_read_index_.store(write, std::memory_order_release);
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
      clear_raw_ring();
      result_reset_pending_.store(true, std::memory_order_release);
      submit_gap_pending_.store(true, std::memory_order_release);
      dropped_results_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    RawPacketSlot& slot = raw_slots_[raw_write_index_];
    if (slot.valid) {
      return false;
    }
    slot.first_frame = first_frame;
    slot.timestamp = audio.timestamp;
    slot.frames = audio.frames;
    slot.channels = channels;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const auto* source =
          reinterpret_cast<const float*>(audio.data[channel]);
      if (source == nullptr) {
        slot.valid = false;
        return false;
      }
      std::copy_n(source, audio.frames, slot.planes[channel].data());
    }
    slot.valid = true;
    raw_write_index_ = next_index(raw_write_index_, kResultQueueCapacity);
    ++raw_count_;
    return true;
  }

  [[nodiscard]] bool capture_ingress(const obs_audio_data& audio,
                                     const std::int64_t first_frame,
                                     const std::uint16_t channels) noexcept {
    const auto write = ingress_write_index_.load(std::memory_order_relaxed);
    const auto next = next_index(write, kIngressQueueCapacity);
    const auto read = ingress_read_index_.load(std::memory_order_acquire);
    if (next == read) {
      submit_gap_pending_.store(true, std::memory_order_release);
      dropped_ingress_packets_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    auto& slot = ingress_slots_[write];
    slot.first_frame = first_frame;
    slot.frames = audio.frames;
    slot.channels = channels;
    slot.discontinuity =
        stream_discontinuity_ ||
        submit_gap_pending_.exchange(false, std::memory_order_acq_rel);
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const auto* source =
          reinterpret_cast<const float*>(audio.data[channel]);
      if (source == nullptr) {
        submit_gap_pending_.store(true, std::memory_order_release);
        dropped_ingress_packets_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      std::copy_n(source, audio.frames, slot.planes[channel].data());
    }
    ingress_write_index_.store(next, std::memory_order_release);
    ingress_wait_.notify_one();
    return true;
  }

  [[nodiscard]] std::int64_t begin_stream_block(
      const obs_audio_data& audio) noexcept {
    const auto duration = duration_nanoseconds(audio.frames, sample_rate_);
    bool discontinuity = false;
    if (!has_stream_) {
      has_stream_ = true;
    } else if (audio.timestamp < last_end_timestamp_ ||
               audio.timestamp - last_end_timestamp_ >
                   kTimestampJumpNanoseconds) {
      discontinuity = true;
      result_reset_pending_.store(true, std::memory_order_release);
    }

    stream_discontinuity_ = discontinuity;
    const auto first_frame = next_frame_;
    if (audio.frames >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
            static_cast<std::uint64_t>(
                std::max<std::int64_t>(first_frame, 0))) {
      next_frame_ = 0;
      stream_discontinuity_ = true;
      result_reset_pending_.store(true, std::memory_order_release);
    } else {
      next_frame_ += static_cast<std::int64_t>(audio.frames);
    }
    last_end_timestamp_ = add_timestamp(audio.timestamp, duration);
    /*
     * Keep the host-frame sequence monotonic across timestamp discontinuities.
     * The core timeline still receives discontinuity=true and advances its
     * generation, while unique frame ids prevent a late result from the old
     * epoch from matching a new packet after the 1.5 second delay.
     */
    return first_frame;
  }

  void clear_raw_ring() noexcept {
    for (auto& slot : raw_slots_) {
      slot.valid = false;
    }
    raw_read_index_ = 0;
    raw_write_index_ = 0;
    raw_count_ = 0;
  }

  void reset_stream() noexcept {
    has_stream_ = false;
    stream_discontinuity_ = false;
    last_end_timestamp_ = 0;
    next_frame_ = 0;
    clear_raw_ring();
    result_reset_pending_.store(true, std::memory_order_release);
  }

  void clear_results_waiting() noexcept {
    const auto write = result_write_index_.load(std::memory_order_acquire);
    result_read_index_.store(write, std::memory_order_release);
    result_reset_pending_.store(false, std::memory_order_release);
  }

  FilterSettings settings_;
  std::uint32_t sample_rate_ = 48000;
  std::uint16_t channels_ = 1;
  std::unique_ptr<runtime::IWhisperRuntime> runtime_;
  std::unique_ptr<core::EndToEndAudioPipeline> pipeline_;
  std::atomic_bool enabled_{true};
  std::atomic_bool accepting_{false};
  std::atomic_bool runtime_ready_{false};
  std::thread runtime_thread_;

  std::atomic_bool ingress_stop_{true};
  std::thread ingress_thread_;
  std::condition_variable ingress_wait_;
  std::mutex ingress_wait_mutex_;
  std::array<IngressPacketSlot, kIngressQueueCapacity> ingress_slots_{};
  std::atomic<std::size_t> ingress_read_index_{0};
  std::atomic<std::size_t> ingress_write_index_{0};
  std::atomic_bool submit_gap_pending_{false};
  std::atomic<std::size_t> dropped_ingress_packets_{0};

  std::array<ResultSlot, kResultQueueCapacity> result_slots_{};
  std::atomic<std::size_t> result_read_index_{0};
  std::atomic<std::size_t> result_write_index_{0};
  std::atomic_bool result_reset_pending_{false};
  std::atomic<std::size_t> dropped_results_{0};

  bool has_stream_ = false;
  bool stream_discontinuity_ = false;
  std::uint64_t last_end_timestamp_ = 0;
  std::int64_t next_frame_ = 0;

  std::array<AudioPlane, MAX_AV_PLANES> output_planes_{};
  obs_audio_data output_packet_{};
  std::array<RawPacketSlot, kResultQueueCapacity> raw_slots_{};
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
  if (const char* configured = std::getenv("OBS_WHISPERBLEEP_PYTHON");
      configured != nullptr && configured[0] != '\0') {
    config.python_executable = configured;
  }
  config.bridge_script_path = bridge_script_path();
  config.runner = runtime::make_persistent_whisper_process_runner();
  return std::make_unique<runtime::OpenAIWhisperRuntime>(std::move(config));
}

}  // namespace obs_whisperbleep::obs
