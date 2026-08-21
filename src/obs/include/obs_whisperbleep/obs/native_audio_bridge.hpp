// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "obs_whisperbleep/obs/obs_filter.hpp"

struct obs_audio_data;

namespace obs_whisperbleep::obs {

/**
 * Native OBS adapter for the bounded asynchronous audio pipeline.
 *
 * The OBS callback only copies the incoming planar float block, attempts a
 * non-blocking queue submission and consumes a result that is old enough for
 * the configured 1.5 second audio budget. Whisper, matching and rendering run
 * on the pipeline worker. If any boundary is unavailable, the caller keeps
 * the original OBS packet as pass-through audio.
 */
class NativeAudioBridge final {
 public:
  NativeAudioBridge(
      FilterSettings settings, std::uint32_t sample_rate,
      std::uint16_t channels,
      std::unique_ptr<runtime::IWhisperRuntime> runtime = {});
  ~NativeAudioBridge();

  NativeAudioBridge(const NativeAudioBridge&) = delete;
  NativeAudioBridge& operator=(const NativeAudioBridge&) = delete;

  /** Starts the worker. This method is intended for OBS lifecycle threads. */
  [[nodiscard]] bool start();
  /** Stops and joins the worker. Never call this from filter_audio(). */
  void stop() noexcept;

  /** Rebuilds the pipeline with new settings off the realtime callback. */
  void update(FilterSettings settings);

  /**
   * Captures and submits one OBS block without waiting. A non-null return
   * points either to the original packet or to bridge-owned output storage
   * that remains valid until the next call.
   */
  [[nodiscard]] obs_audio_data* filter_audio(obs_audio_data* audio) noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::size_t queued_frames() const noexcept;
  [[nodiscard]] std::size_t dropped_frames() const noexcept;
  [[nodiscard]] std::size_t dropped_results() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Creates the dependency-free native runtime used when no host runtime is
 * supplied. A platform host can replace this provider with an initialized
 * OpenAIWhisperRuntime without changing the OBS audio boundary.
 */
[[nodiscard]] std::unique_ptr<runtime::IWhisperRuntime>
make_default_native_runtime();

}  // namespace obs_whisperbleep::obs
