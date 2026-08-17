// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/replacement/replacement_catalog.hpp"
#include "obs_whisperbleep/replacement/wav_audio_loader.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M5 WAV loader test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void put_id(std::vector<std::uint8_t>& bytes, std::size_t offset,
            const char* id) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(id[index]);
  }
}

std::vector<std::uint8_t> make_pcm_wav(const std::vector<std::int16_t>& samples,
                                       std::uint32_t sample_rate = 48000,
                                       std::uint16_t channels = 1) {
  const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2U);
  const auto riff_size = 36U + data_bytes;
  std::vector<std::uint8_t> bytes(44U + data_bytes, 0);
  put_id(bytes, 0, "RIFF");
  put_u32(bytes, 4, riff_size);
  put_id(bytes, 8, "WAVE");
  put_id(bytes, 12, "fmt ");
  put_u32(bytes, 16, 16U);
  put_u16(bytes, 20, 1U);
  put_u16(bytes, 22, channels);
  put_u32(bytes, 24, sample_rate);
  put_u32(bytes, 28, sample_rate * channels * 2U);
  put_u16(bytes, 32, static_cast<std::uint16_t>(channels * 2U));
  put_u16(bytes, 34, 16U);
  put_id(bytes, 36, "data");
  put_u32(bytes, 40, data_bytes);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    put_u16(bytes, 44U + index * 2U,
            static_cast<std::uint16_t>(samples[index]));
  }
  return bytes;
}

std::filesystem::path test_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (std::string("obs-whisperbleep-m5-wav-") + std::to_string(stamp));
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary);
  expect(output.good(), "opens deterministic WAV fixture for writing");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  expect(output.good(), "writes deterministic WAV fixture");
}

void expect_near(float actual, float expected, float tolerance,
                 const char* message) {
  expect(std::fabs(actual - expected) <= tolerance, message);
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::replacement;

  const auto root = test_root();
  std::filesystem::create_directories(root);
  const auto valid_path = root / "voice.WAV";
  write_bytes(valid_path, make_pcm_wav({-32768, 0, 32767}));

  const auto validation = validate_wav_pcm(valid_path);
  expect(validation.valid(), "validates an actual RIFF/WAVE PCM header");
  expect(validation.info.channels == 1 && validation.info.sample_rate == 48000 &&
             validation.info.bits_per_sample == 16 &&
             validation.info.frame_count == 3,
         "reports PCM format metadata and frame count");

  const auto loaded = load_wav_pcm(valid_path);
  expect(loaded.success() && loaded.audio.sample_rate == 48000 &&
             loaded.audio.channels == 1 && loaded.audio.samples.size() == 3,
         "decodes PCM samples into an AudioBuffer");
  expect_near(loaded.audio.samples[0], -1.0F, 0.0001F,
              "decodes the minimum signed 16-bit sample");
  expect_near(loaded.audio.samples[1], 0.0F, 0.0001F,
              "decodes a zero signed 16-bit sample");
  expect(loaded.audio.samples[2] > 0.99F && loaded.audio.samples[2] < 1.0F,
         "normalizes the maximum signed 16-bit sample");

  // Registration remains an explicit caller action; loading never mutates a
  // catalog or performs hidden replacement selection.
  ReplacementCatalog catalog;
  expect(catalog.register_asset(ReplacementKind::custom, loaded.audio),
         "explicitly registers a decoded asset in the replacement catalog");
  const auto custom = catalog.build({ReplacementKind::custom, {}}, 48000, 1, 3);
  expect(custom.success() && custom.audio.samples == loaded.audio.samples,
         "catalog returns the explicitly registered custom asset");

  const auto missing = validate_wav_pcm(root / "missing.wav");
  expect(missing.status == WavAudioLoadStatus::missing,
         "reports a missing local WAV file");
  expect(validate_wav_pcm("https://example.invalid/audio.wav").status ==
             WavAudioLoadStatus::remote_source,
         "rejects URI-like paths without network access");
  expect(validate_wav_pcm({}).status == WavAudioLoadStatus::empty_path,
         "reports an empty path");

  const auto fake_path = root / "fake.wav";
  write_bytes(fake_path, {'n', 'o', 't', ' ', 'w', 'a', 'v'});
  expect(validate_wav_pcm(fake_path).status ==
             WavAudioLoadStatus::truncated_header,
         "rejects an extension-only fake WAV");

  auto unsupported = make_pcm_wav({0, 0});
  put_u16(unsupported, 34, 20U);
  write_bytes(root / "unsupported.wav", unsupported);
  expect(validate_wav_pcm(root / "unsupported.wav").status ==
             WavAudioLoadStatus::unsupported_format,
         "rejects unsupported sample widths");

  auto truncated = make_pcm_wav({0, 0});
  truncated.resize(truncated.size() - 1U);
  put_u32(truncated, 4, 39U);
  put_u32(truncated, 40, 3U);
  write_bytes(root / "truncated.wav", truncated);
  expect(validate_wav_pcm(root / "truncated.wav").status ==
             WavAudioLoadStatus::truncated_data,
         "rejects data chunks that do not contain complete frames");

  expect(validate_wav_pcm(valid_path, WavAudioLoaderOptions{8U}).status ==
             WavAudioLoadStatus::too_large,
         "enforces the worker-side file size limit");
  expect(load_wav_pcm(valid_path, WavAudioLoaderOptions{64U * 1024U, 2U})
                 .status == WavAudioLoadStatus::too_large,
         "enforces the worker-side decoded sample limit");
  expect(std::string(wav_audio_load_status_name(WavAudioLoadStatus::success)) ==
             "success",
         "exposes stable diagnostic status names");

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  return EXIT_SUCCESS;
}
