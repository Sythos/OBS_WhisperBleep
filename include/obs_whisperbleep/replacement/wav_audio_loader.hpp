// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "obs_whisperbleep/core/replacement_renderer.hpp"

namespace obs_whisperbleep::replacement {

/**
 * The deliberately small set of file conditions exposed by the dependency-
 * free WAV loader. The loader accepts local RIFF/WAVE little-endian PCM files
 * with integer samples at 8, 16, 24 or 32 bits per sample.
 */
enum class WavAudioLoadStatus {
  success,
  empty_path,
  remote_source,
  missing,
  not_regular_file,
  unreadable,
  too_large,
  truncated_header,
  invalid_container,
  unsupported_format,
  invalid_header,
  missing_format_chunk,
  missing_data_chunk,
  empty_data,
  truncated_data,
};

[[nodiscard]] const char* wav_audio_load_status_name(
    WavAudioLoadStatus status) noexcept;

struct WavAudioLoaderOptions {
  /** A bounded worker-side read limit for user-provided replacement assets. */
  std::uintmax_t max_file_bytes = 64U * 1024U * 1024U;
  /** A bounded worker-side limit for decoded interleaved float samples. */
  std::size_t max_decoded_samples = 32U * 1024U * 1024U;
};

struct WavAudioInfo {
  std::uint16_t audio_format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t byte_rate = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 0;
  std::uint64_t data_offset = 0;
  std::uint64_t data_bytes = 0;
  std::size_t frame_count = 0;
};

struct WavAudioValidation {
  WavAudioLoadStatus status = WavAudioLoadStatus::empty_path;
  std::filesystem::path path;
  WavAudioInfo info{};
  std::string message;

  [[nodiscard]] bool valid() const noexcept {
    return status == WavAudioLoadStatus::success;
  }
};

struct WavAudioLoadResult {
  WavAudioLoadStatus status = WavAudioLoadStatus::empty_path;
  std::filesystem::path path;
  WavAudioInfo info{};
  core::AudioBuffer audio{};
  std::string message;

  [[nodiscard]] bool success() const noexcept {
    return status == WavAudioLoadStatus::success;
  }
};

/**
 * Reads and validates a local WAV header without decoding its samples.
 *
 * This operation performs filesystem I/O and is intended for a worker or
 * setup path. It never downloads a file and does not accept a URI.
 */
[[nodiscard]] WavAudioValidation validate_wav_pcm(
    const std::filesystem::path& path,
    WavAudioLoaderOptions options = {});

/**
 * Reads and decodes a validated local WAV PCM file into normalized float
 * samples. This operation must be called off the OBS realtime callback.
 */
[[nodiscard]] WavAudioLoadResult load_wav_pcm(
    const std::filesystem::path& path,
    WavAudioLoaderOptions options = {});

}  // namespace obs_whisperbleep::replacement
