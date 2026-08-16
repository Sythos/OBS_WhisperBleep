// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>

#include "obs_whisperbleep/core/audio_pipeline.hpp"
#include "obs_whisperbleep/platform/platform_info.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "integration test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep;

  const auto platform = platform::current_platform();
  expect(!platform.name().empty(), "reports a platform name");

  runtime::StubWhisperRuntime runtime;
  expect(runtime.initialize("not-loaded-in-M0") == runtime::RuntimeStatus::unavailable,
         "keeps runtime explicitly unavailable in M0");
  const auto transcript = runtime.transcribe(nullptr, 0, 48000);
  expect(transcript.empty(), "stub runtime does not invent transcription");

  core::AudioBuffer input{48000, 1, {1.F, 1.F, 1.F, 1.F}};
  core::AudioBuffer replacement{48000, 1, {0.F}};
  core::AudioPipeline pipeline;
  const auto output = pipeline.process(core::AudioFrame{0, input}, {{1, 3}},
                                       replacement);
  expect(output.audio.samples[0] == 1.F && output.audio.samples[1] == 0.F &&
             output.audio.samples[2] == 0.F && output.audio.samples[3] == 1.F,
         "connects deterministic scheduling and rendering");

  return EXIT_SUCCESS;
}
