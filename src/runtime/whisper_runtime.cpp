// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::runtime {

RuntimeStatus StubWhisperRuntime::initialize(std::string_view model_path) {
  (void)model_path;
  return RuntimeStatus::unavailable;
}

std::vector<TranscriptSegment> StubWhisperRuntime::transcribe(
    const float* samples, std::size_t sample_count, std::uint32_t sample_rate) {
  (void)samples;
  (void)sample_count;
  (void)sample_rate;
  return {};
}

}  // namespace obs_whisperbleep::runtime
