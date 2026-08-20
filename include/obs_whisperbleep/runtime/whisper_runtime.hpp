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

#include "obs_whisperbleep/model/model_catalog.hpp"
#include "obs_whisperbleep/runtime/backend_selector.hpp"

namespace obs_whisperbleep::runtime {

struct TranscriptSegment {
  std::int64_t start_frame = 0;
  std::int64_t end_frame = 0;
  std::string text;
};

enum class RuntimeStatus { ready, unavailable, error };

[[nodiscard]] const char* runtime_status_name(RuntimeStatus status) noexcept;

enum class RuntimeLanguageStatus { accepted, invalid, english_only_conflict };

struct RuntimeLanguagePolicyResult {
  RuntimeLanguageStatus status = RuntimeLanguageStatus::invalid;
  std::string normalized_language;
  std::string message;

  [[nodiscard]] bool accepted() const noexcept {
    return status == RuntimeLanguageStatus::accepted;
  }
};

/**
 * Checks a requested Whisper language against the selected model metadata.
 *
 * Multilingual descriptors accept an explicit language tag or `auto`.
 * English-only descriptors accept English tags and normalize them to `en` so
 * an injected adapter cannot accidentally run a different language policy.
 */
[[nodiscard]] RuntimeLanguagePolicyResult validate_language_policy(
    const model::ModelDescriptor& model, std::string_view language);

class IWhisperRuntime {
 public:
  virtual ~IWhisperRuntime() = default;
  [[nodiscard]] virtual RuntimeStatus initialize(
      std::string_view model_path) = 0;
  /**
   * Language-aware initialization for adapters that can enforce a model
   * language policy. The compatibility implementation delegates to the
   * original path-only method for existing adapters.
   */
  [[nodiscard]] virtual RuntimeStatus initialize(
      std::string_view model_path, std::string_view language);
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
  model::ModelId model_id = model::ModelId::tiny;
  std::string language = "auto";
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
 * One request/response exchange with a JSON-lines Whisper bridge process.
 *
 * The host owns process creation and lifetime. This keeps shell, Python and
 * platform process APIs outside the portable core and makes the adapter
 * deterministic to test with an injected runner.
 */
struct WhisperProcessRequest {
  std::vector<std::string> command;
  std::string input_line;
};

struct WhisperProcessResult {
  bool started = false;
  int exit_code = -1;
  std::vector<std::string> output_lines;
  std::string error_output;
};

using WhisperProcessRunner =
    std::function<WhisperProcessResult(const WhisperProcessRequest& request)>;

/** Configuration for the optional Python/OpenAI Whisper JSON-lines bridge. */
struct OpenAIWhisperRuntimeConfig {
  std::string python_executable = "python";
  std::string bridge_script_path = "runtime/openai_whisper_bridge.py";
  WhisperProcessRunner runner;
};

/**
 * Concrete adapter for runtime/openai_whisper_bridge.py.
 *
 * It has no Python or OpenAI Whisper link-time dependency. A production host
 * supplies a runner that maintains the bridge process; tests can supply a
 * deterministic function instead.
 */
class OpenAIWhisperRuntime final : public IWhisperRuntime {
 public:
  explicit OpenAIWhisperRuntime(OpenAIWhisperRuntimeConfig config);

  [[nodiscard]] RuntimeStatus initialize(std::string_view model_path) override;
  [[nodiscard]] RuntimeStatus initialize(std::string_view model_path,
                                         std::string_view language) override;
  [[nodiscard]] std::vector<TranscriptSegment> transcribe(
      const float* samples, std::size_t sample_count,
      std::uint32_t sample_rate) override;

  /** Returns the most recent bridge protocol or transport error. */
  [[nodiscard]] const std::string& last_error() const noexcept;

 private:
  [[nodiscard]] WhisperProcessResult run(std::string input_line) const;

  OpenAIWhisperRuntimeConfig config_;
  std::string language_ = "auto";
  std::string last_error_;
  bool initialized_ = false;
};

/** Creates a factory adapter backed by an injected OpenAI Whisper runner. */
[[nodiscard]] RuntimeAdapterFactory make_openai_whisper_adapter(
    OpenAIWhisperRuntimeConfig config);

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
  using IWhisperRuntime::initialize;
  [[nodiscard]] RuntimeStatus initialize(std::string_view model_path) override;
  [[nodiscard]] std::vector<TranscriptSegment> transcribe(
      const float* samples, std::size_t sample_count,
      std::uint32_t sample_rate) override;
};

}  // namespace obs_whisperbleep::runtime
