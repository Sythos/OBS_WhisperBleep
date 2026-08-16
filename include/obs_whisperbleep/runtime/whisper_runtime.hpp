// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

/** Explicit M0 placeholder: no model is loaded and transcription is empty. */
class StubWhisperRuntime final : public IWhisperRuntime {
 public:
  [[nodiscard]] RuntimeStatus initialize(std::string_view model_path) override;
  [[nodiscard]] std::vector<TranscriptSegment> transcribe(
      const float* samples, std::size_t sample_count,
      std::uint32_t sample_rate) override;
};

}  // namespace obs_whisperbleep::runtime
