// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::runtime {

/**
 * Bounds for the host-owned JSON-lines bridge process.
 *
 * The runner starts the command on the first request and keeps it alive for
 * subsequent requests. It is intended to be called by the audio processing
 * worker, never from an OBS audio callback. The runner performs blocking
 * process I/O on its caller, so the host must keep that caller off the real-
 * time callback thread.
 */
struct PersistentWhisperProcessRunnerConfig {
  std::chrono::milliseconds response_timeout{30'000};
  std::chrono::milliseconds shutdown_timeout{500};
  std::size_t max_response_bytes{4U * 1024U * 1024U};
  std::size_t max_error_output_bytes{16U * 1024U};
};

/**
 * Host-owned persistent process transport for the Whisper JSON-lines bridge.
 *
 * The command is executed directly (without a shell), with stdin, stdout and
 * stderr connected to private pipes. One request line is written for each
 * call and one response line is read back. The process is stopped when this
 * object is destroyed or when stop() is called.
 *
 * POSIX hosts use fork/exec and Windows hosts use CreateProcessW. No platform
 * process API is used by the OBS callback itself; callers must invoke this
 * runner from a worker or other non-real-time host context.
 */
class PersistentWhisperProcessRunner final {
 public:
  explicit PersistentWhisperProcessRunner(
      PersistentWhisperProcessRunnerConfig config = {});
  ~PersistentWhisperProcessRunner();

  PersistentWhisperProcessRunner(const PersistentWhisperProcessRunner&) =
      delete;
  PersistentWhisperProcessRunner& operator=(
      const PersistentWhisperProcessRunner&) = delete;
  PersistentWhisperProcessRunner(PersistentWhisperProcessRunner&&) noexcept;
  PersistentWhisperProcessRunner& operator=(
      PersistentWhisperProcessRunner&&) noexcept;

  [[nodiscard]] WhisperProcessResult operator()(
      const WhisperProcessRequest& request);

  /** Stops the child process and releases all pipe handles. */
  void stop() noexcept;

  /** Returns whether the bridge child is currently alive. */
  [[nodiscard]] bool running() const noexcept;

  /** Returns the most recent transport/lifecycle error, if any. */
  [[nodiscard]] std::string last_error() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Creates the runner shape expected by OpenAIWhisperRuntimeConfig.
 *
 * The returned function owns one shared runner, so copies of the
 * std::function retain the same child process rather than starting one child
 * per audio request. Destruction of the last copy stops the child.
 */
[[nodiscard]] WhisperProcessRunner make_persistent_whisper_process_runner(
    PersistentWhisperProcessRunnerConfig config = {});

}  // namespace obs_whisperbleep::runtime
