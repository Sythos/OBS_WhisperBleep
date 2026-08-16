// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>

#include "obs_whisperbleep/core/audio_frame_queue.hpp"

namespace obs_whisperbleep::core {

using AudioFrameProcessor = std::function<void(AudioFrame)>;

/**
 * Dedicated worker that drains a bounded audio queue without blocking the
 * realtime producer. Stop drains frames already accepted by the queue before
 * joining the worker thread.
 */
class AudioProcessingWorker {
 public:
  AudioProcessingWorker(std::size_t queue_capacity,
                        AudioFrameProcessor processor);
  ~AudioProcessingWorker();

  AudioProcessingWorker(const AudioProcessingWorker&) = delete;
  AudioProcessingWorker& operator=(const AudioProcessingWorker&) = delete;

  [[nodiscard]] bool start();
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] bool submit(AudioFrame frame) noexcept;
  [[nodiscard]] std::size_t queued_frames() const noexcept;
  [[nodiscard]] std::size_t dropped_frames() const noexcept;

 private:
  void run() noexcept;

  AudioFrameQueue queue_;
  AudioFrameProcessor processor_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace obs_whisperbleep::core
