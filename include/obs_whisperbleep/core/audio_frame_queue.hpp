// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

#include "obs_whisperbleep/core/audio_pipeline.hpp"

namespace obs_whisperbleep::core {

/**
 * Bounded single-producer/single-consumer queue for audio frames.
 *
 * The queue never waits and drops the newest frame when full. The producer and
 * consumer must be distinct threads; callers that need another topology must
 * add an appropriate external synchronization layer.
 */
class AudioFrameQueue {
 public:
  explicit AudioFrameQueue(std::size_t capacity);

  AudioFrameQueue(const AudioFrameQueue&) = delete;
  AudioFrameQueue& operator=(const AudioFrameQueue&) = delete;

  [[nodiscard]] bool try_push(AudioFrame frame) noexcept;
  [[nodiscard]] std::optional<AudioFrame> try_pop();

  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t dropped_frames() const noexcept;

 private:
  [[nodiscard]] std::size_t next_index(std::size_t index) const noexcept;

  std::vector<AudioFrame> slots_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<std::size_t> dropped_{0};
};

}  // namespace obs_whisperbleep::core
