// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_processing_worker.hpp"

#include <utility>

namespace obs_whisperbleep::core {

AudioProcessingWorker::AudioProcessingWorker(std::size_t queue_capacity,
                                             AudioFrameProcessor processor)
    : queue_(queue_capacity), processor_(std::move(processor)) {}

AudioProcessingWorker::~AudioProcessingWorker() { stop(); }

bool AudioProcessingWorker::start() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);

  // A naturally completed worker still owns a joinable std::thread. Reap it
  // before creating a replacement so that restart remains well-defined.
  if (running_.load(std::memory_order_acquire)) {
    return false;
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
    return false;
  }

  accepting_submissions_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);

  try {
    thread_ = std::thread(&AudioProcessingWorker::run, this);
  } catch (...) {
    // std::thread construction can fail before the worker owns a joinable
    // thread. Restore the stopped state so callers may retry start().
    stop_requested_.store(true, std::memory_order_release);
    accepting_submissions_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    return false;
  }

  accepting_submissions_.store(true, std::memory_order_release);
  return true;
}

void AudioProcessingWorker::stop() noexcept {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  accepting_submissions_.store(false, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  while (in_flight_submissions_.load(std::memory_order_acquire) != 0) {
    std::this_thread::yield();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool AudioProcessingWorker::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

bool AudioProcessingWorker::submit(AudioFrame frame) noexcept {
  if (!running_.load(std::memory_order_acquire) ||
      !accepting_submissions_.load(std::memory_order_acquire)) {
    return false;
  }

  in_flight_submissions_.fetch_add(1, std::memory_order_acq_rel);
  const bool accepting =
      running_.load(std::memory_order_acquire) &&
      accepting_submissions_.load(std::memory_order_acquire) &&
      !stop_requested_.load(std::memory_order_acquire);
  if (!accepting) {
    in_flight_submissions_.fetch_sub(1, std::memory_order_release);
    return false;
  }

  const bool accepted = queue_.try_push(std::move(frame));
  in_flight_submissions_.fetch_sub(1, std::memory_order_release);
  return accepted;
}

std::size_t AudioProcessingWorker::queued_frames() const noexcept {
  return queue_.size();
}

std::size_t AudioProcessingWorker::dropped_frames() const noexcept {
  return queue_.dropped_frames();
}

void AudioProcessingWorker::run() noexcept {
  while (true) {
    auto frame = queue_.try_pop();
    if (frame.has_value() && processor_) {
      try {
        processor_(std::move(*frame));
      } catch (...) {
        // A callback failure must not terminate the host process or worker.
        accepting_submissions_.store(false, std::memory_order_release);
        stop_requested_.store(true, std::memory_order_release);
      }
    }

    const bool stopping = stop_requested_.load(std::memory_order_acquire);
    if (stopping &&
        in_flight_submissions_.load(std::memory_order_acquire) == 0 &&
        queue_.size() == 0) {
      break;
    }
    if (!frame.has_value()) {
      std::this_thread::yield();
    }
  }
  accepting_submissions_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
}

}  // namespace obs_whisperbleep::core
