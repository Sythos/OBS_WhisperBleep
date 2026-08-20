// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "obs_whisperbleep/replacement/replacement_catalog.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M4 replacement test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::filesystem::path test_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (std::string("obs-whisperbleep-m4-replacements-") +
          std::to_string(stamp));
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::core;
  using namespace obs_whisperbleep::replacement;

  const auto options = ReplacementCatalog::options();
  expect(options.size() == 4, "exposes beep, duck, bark, and custom choices");
  expect(options[0].kind == ReplacementKind::beep && options[0].id == "beep" &&
             options[0].synthetic && !options[0].requires_asset,
         "marks beep as the local default without an external asset");
  expect(options[1].kind == ReplacementKind::duck &&
             options[2].kind == ReplacementKind::bark &&
             options[3].kind == ReplacementKind::custom,
         "keeps stable replacement option ordering");
  expect(ReplacementCatalog::kind_name(ReplacementKind::custom) == "custom" &&
             ReplacementCatalog::kind_name(static_cast<ReplacementKind>(99)) ==
                 "unknown",
         "provides stable replacement identifiers");

  ReplacementCatalog catalog;
  const auto default_beep = catalog.build(ReplacementRequest{}, 48000, 2, 32);
  expect(default_beep.success() &&
             default_beep.audio.samples ==
                 SyntheticReplacement::beep(48000, 2, 32).samples,
         "builds the default replacement as a synthetic beep");

  const auto beep = catalog.build(
      ReplacementRequest{ReplacementKind::beep, BeepOptions{800.0, 0.4F}},
      48000, 2, 32);
  expect(beep.success() && beep.audio.sample_rate == 48000 &&
             beep.audio.channels == 2 && beep.audio.samples.size() == 64,
         "builds a deterministic stereo beep without an asset");
  for (const auto sample : beep.audio.samples) {
    expect(sample <= 0.4F && sample >= -0.4F,
           "keeps generated beep samples within amplitude");
  }

  const auto missing_duck =
      catalog.build({ReplacementKind::duck, {}}, 48000, 1, 32);
  expect(missing_duck.status == ReplacementBuildStatus::missing_asset &&
             !catalog.has_asset(ReplacementKind::duck),
         "does not synthesize or download a missing duck asset");
  const AudioBuffer source{48000, 1, {1.F, 1.F, 1.F, 1.F}};
  const auto missing_duck_render = ReplacementRenderer::render(
      source, {{1, 3}}, missing_duck.audio);
  expect(missing_duck_render.samples == source.samples,
         "keeps source audio on a missing asset");

  const AudioBuffer duck_audio{44100, 1, {0.1F, -0.1F, 0.2F}};
  const AudioBuffer bark_audio{48000, 2, {0.2F, 0.2F, -0.2F, -0.2F}};
  expect(catalog.register_asset(ReplacementKind::duck, duck_audio) &&
             catalog.register_asset(ReplacementKind::bark, bark_audio),
         "registers explicitly supplied decoded assets");
  const auto duck = catalog.build({ReplacementKind::duck, {}}, 48000, 1, 32);
  expect(duck.success() && duck.audio.samples == duck_audio.samples &&
             duck.audio.sample_rate == duck_audio.sample_rate,
         "returns the registered duck buffer without hidden I/O");
  expect(catalog.has_asset(ReplacementKind::bark),
         "tracks the registered bark asset");

  expect(!catalog.register_asset(ReplacementKind::beep, duck_audio),
         "does not replace generated beep with an asset");
  expect(!catalog.register_asset(ReplacementKind::custom,
                                 AudioBuffer{48000, 2, {0.1F}}),
         "rejects an asset whose sample count is not channel aligned");
  expect(catalog.register_asset(ReplacementKind::custom, duck_audio),
         "registers a valid custom decoded buffer");
  expect(catalog.clear_asset(ReplacementKind::custom) &&
             !catalog.has_asset(ReplacementKind::custom),
         "clears a custom asset explicitly");

  const auto root = test_root();
  std::filesystem::create_directories(root);
  const auto valid_path = root / "replacement.WAV";
  {
    std::ofstream output(valid_path, std::ios::binary);
    output << "audio";
  }
  const auto valid = validate_custom_audio(valid_path);
  expect(valid.valid() && valid.size_bytes == 5,
         "accepts a readable local audio file case-insensitively");
  expect(validate_custom_audio({}).status ==
             CustomAudioValidationStatus::empty_path,
         "rejects an empty custom audio path");
  expect(validate_custom_audio("https://example.invalid/audio.wav").status ==
             CustomAudioValidationStatus::remote_source,
         "rejects remote audio sources without downloading");
  expect(validate_custom_audio(root / "missing.wav").status ==
             CustomAudioValidationStatus::missing,
         "reports a missing custom audio file");
  const auto unsupported_path = root / "replacement.txt";
  {
    std::ofstream output(unsupported_path, std::ios::binary);
    output << "not audio";
  }
  expect(validate_custom_audio(unsupported_path).status ==
             CustomAudioValidationStatus::unsupported_format,
         "rejects unsupported custom audio extensions");
  const auto empty_path = root / "empty.wav";
  std::ofstream(empty_path, std::ios::binary).close();
  expect(validate_custom_audio(empty_path).status ==
             CustomAudioValidationStatus::empty_file,
         "rejects empty custom audio files");

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  return EXIT_SUCCESS;
}
