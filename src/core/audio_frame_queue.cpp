// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_frame_queue.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace obs_whisperbleep::core {

namespace {

[[nodiscard]] std::size_t slot_count(const std::size_t capacity) {
  if (capacity == std::numeric_limits<std::size_t>::max()) {
    throw std::length_error(
        "AudioFrameQueue capacity cannot be represented by its ring buffer");
  }
  return capacity == 0 ? 1 : capacity + 1;
}

}  // namespace

AudioFrameQueue::AudioFrameQueue(std::size_t capacity)
    : slots_(slot_count(capacity)) {}

std::size_t AudioFrameQueue::next_index(std::size_t index) const noexcept {
  return (index + 1) % slots_.size();
}

bool AudioFrameQueue::try_push(AudioFrame frame) noexcept {
  const auto head = head_.load(std::memory_order_relaxed);
  const auto next = next_index(head);
  const auto tail = tail_.load(std::memory_order_acquire);
  if (next == tail) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  slots_[head] = std::move(frame);
  head_.store(next, std::memory_order_release);
  return true;
}

std::optional<AudioFrame> AudioFrameQueue::try_pop() {
  const auto tail = tail_.load(std::memory_order_relaxed);
  const auto head = head_.load(std::memory_order_acquire);
  if (tail == head) {
    return std::nullopt;
  }

  AudioFrame frame = std::move(slots_[tail]);
  tail_.store(next_index(tail), std::memory_order_release);
  return frame;
}

std::size_t AudioFrameQueue::capacity() const noexcept {
  return slots_.size() == 1 ? 0 : slots_.size() - 1;
}

std::size_t AudioFrameQueue::size() const noexcept {
  const auto head = head_.load(std::memory_order_acquire);
  const auto tail = tail_.load(std::memory_order_acquire);
  return head >= tail ? head - tail : slots_.size() - tail + head;
}

std::size_t AudioFrameQueue::dropped_frames() const noexcept {
  return dropped_.load(std::memory_order_relaxed);
}

}  // namespace obs_whisperbleep::core
