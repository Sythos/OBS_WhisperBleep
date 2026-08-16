// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/audio_processing_worker.hpp"

#include <chrono>
#include <utility>

namespace obs_whisperbleep::core {

AudioProcessingWorker::AudioProcessingWorker(std::size_t queue_capacity,
                                             AudioFrameProcessor processor)
    : queue_(queue_capacity), processor_(std::move(processor)) {}

AudioProcessingWorker::~AudioProcessingWorker() { stop(); }

bool AudioProcessingWorker::start() {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  stop_requested_.store(false, std::memory_order_release);
  thread_ = std::thread(&AudioProcessingWorker::run, this);
  return true;
}

void AudioProcessingWorker::stop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool AudioProcessingWorker::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

bool AudioProcessingWorker::submit(AudioFrame frame) noexcept {
  if (!running()) {
    return false;
  }
  return queue_.try_push(std::move(frame));
}

std::size_t AudioProcessingWorker::queued_frames() const noexcept {
  return queue_.size();
}

std::size_t AudioProcessingWorker::dropped_frames() const noexcept {
  return queue_.dropped_frames();
}

void AudioProcessingWorker::run() noexcept {
  while (!stop_requested_.load(std::memory_order_acquire) ||
         queue_.size() != 0) {
    auto frame = queue_.try_pop();
    if (!frame.has_value()) {
      std::this_thread::yield();
      continue;
    }
    if (processor_) {
      try {
        processor_(std::move(*frame));
      } catch (...) {
        // A callback failure must not terminate the host process or worker.
        stop_requested_.store(true, std::memory_order_release);
      }
    }
  }
  running_.store(false, std::memory_order_release);
}

}  // namespace obs_whisperbleep::core
