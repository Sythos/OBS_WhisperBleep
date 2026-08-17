// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "obs_whisperbleep/runtime/backend_selector.hpp"

namespace obs_whisperbleep::runtime {

struct TranscriptSegment {
  std::int64_t start_frame = 0;
  std::int64_t end_frame = 0;
  std::string text;
};

enum class RuntimeStatus { ready, unavailable, error };

[[nodiscard]] const char* runtime_status_name(RuntimeStatus status) noexcept;

class IWhisperRuntime {
 public:
  virtual ~IWhisperRuntime() = default;
  [[nodiscard]] virtual RuntimeStatus initialize(
      std::string_view model_path) = 0;
  [[nodiscard]] virtual std::vector<TranscriptSegment> transcribe(
      const float* samples, std::size_t sample_count,
      std::uint32_t sample_rate) = 0;
};

/**
 * Inputs passed to the injected Whisper runtime adapter factory.
 *
 * The portable project deliberately does not select or link a concrete
 * Whisper/PyTorch implementation here. A host integration supplies the
 * adapter and a capability probe result when those dependencies are present.
 */
struct RuntimeRequest {
  std::string model_path;
  Backend requested_backend = Backend::auto_select;
  BackendProbeResult backend_probe;
};

struct RuntimeFactoryResult {
  RuntimeStatus status = RuntimeStatus::unavailable;
  std::unique_ptr<IWhisperRuntime> runtime;
  BackendSelection backend;
  std::string message;
};

using RuntimeAdapterFactory = std::function<std::unique_ptr<IWhisperRuntime>(
    const RuntimeRequest& request, const BackendSelection& backend)>;

/**
 * Dependency-free factory boundary for a concrete Whisper adapter.
 *
 * Without an injected factory, create() remains explicitly unavailable. This
 * prevents the portable build from claiming that Whisper, Python, PyTorch or
 * CUDA are present merely because a model file exists.
 */
class WhisperRuntimeFactory final {
 public:
  explicit WhisperRuntimeFactory(RuntimeAdapterFactory factory = {});

  [[nodiscard]] RuntimeFactoryResult create(
      const RuntimeRequest& request) const;

 private:
  RuntimeAdapterFactory factory_;
};

/** Explicit M0 placeholder: no model is loaded and transcription is empty. */
class StubWhisperRuntime final : public IWhisperRuntime {
 public:
  [[nodiscard]] RuntimeStatus initialize(std::string_view model_path) override;
  [[nodiscard]] std::vector<TranscriptSegment> transcribe(
      const float* samples, std::size_t sample_count,
      std::uint32_t sample_rate) override;
};

}  // namespace obs_whisperbleep::runtime
