// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/replacement/wav_audio_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace obs_whisperbleep::replacement {
namespace {

using ByteVector = std::vector<std::uint8_t>;

struct FileReadResult {
  WavAudioLoadStatus status = WavAudioLoadStatus::unreadable;
  ByteVector bytes;
  std::string message;
};

struct ParsedWav {
  WavAudioInfo info{};
  WavAudioLoadStatus status = WavAudioLoadStatus::invalid_header;
  std::string message;
};

[[nodiscard]] bool has_remote_scheme(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  auto separator = text.find("://");
  if (separator == std::string::npos) {
    // Filesystem implementations can collapse a URI's double slash. Do not
    // classify a Windows drive root such as C:/audio.wav as a URI.
    separator = text.find(":/");
  }
  if (separator == std::string::npos || separator == 0 ||
      (separator == 1 &&
       std::isalpha(static_cast<unsigned char>(text.front())) != 0)) {
    return false;
  }

  return std::all_of(text.begin(), text.begin() + separator,
                     [](const unsigned char character) {
                       return std::isalnum(character) != 0 || character == '+' ||
                              character == '-' || character == '.';
                     });
}

[[nodiscard]] FileReadResult read_local_file(
    const std::filesystem::path& path, const WavAudioLoaderOptions& options) {
  FileReadResult result;
  if (path.empty()) {
    result.status = WavAudioLoadStatus::empty_path;
    result.message = "WAV audio path is empty";
    return result;
  }
  if (has_remote_scheme(path)) {
    result.status = WavAudioLoadStatus::remote_source;
    result.message = "Remote WAV sources are not accepted";
    return result;
  }

  std::error_code error;
  if (!std::filesystem::exists(path, error) || error) {
    result.status = WavAudioLoadStatus::missing;
    result.message = "WAV audio file does not exist";
    return result;
  }
  if (!std::filesystem::is_regular_file(path, error) || error) {
    result.status = WavAudioLoadStatus::not_regular_file;
    result.message = "WAV audio path is not a regular file";
    return result;
  }

  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    result.status = WavAudioLoadStatus::unreadable;
    result.message = "WAV audio file size cannot be read";
    return result;
  }
  if (file_size == 0) {
    result.status = WavAudioLoadStatus::truncated_header;
    result.message = "WAV audio file is empty";
    return result;
  }
  if (options.max_file_bytes == 0 || file_size > options.max_file_bytes ||
      file_size > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::size_t>::max()) ||
      file_size > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::streamsize>::max())) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV audio file exceeds the configured read limit";
    return result;
  }

  try {
    result.bytes.resize(static_cast<std::size_t>(file_size));
  } catch (const std::bad_alloc&) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV audio file cannot be allocated for worker decoding";
    return result;
  } catch (const std::length_error&) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV audio file is too large for worker decoding";
    return result;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    result.status = WavAudioLoadStatus::unreadable;
    result.message = "WAV audio file cannot be opened for reading";
    return result;
  }
  input.read(reinterpret_cast<char*>(result.bytes.data()),
             static_cast<std::streamsize>(file_size));
  if (input.gcount() != static_cast<std::streamsize>(file_size)) {
    result.status = WavAudioLoadStatus::unreadable;
    result.message = "WAV audio file could not be read completely";
    result.bytes.clear();
    return result;
  }

  result.status = WavAudioLoadStatus::success;
  return result;
}

[[nodiscard]] std::uint16_t read_u16(const ByteVector& bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1]) << 8U;
}

[[nodiscard]] std::uint32_t read_u32(const ByteVector& bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
}

[[nodiscard]] bool has_id(const ByteVector& bytes, std::size_t offset,
                          std::string_view id) noexcept {
  return id.size() == 4 && offset <= bytes.size() &&
         bytes.size() - offset >= id.size() &&
         std::equal(id.begin(), id.end(), bytes.begin() + offset);
}

[[nodiscard]] ParsedWav parse_wav(const ByteVector& bytes) {
  ParsedWav result;
  if (bytes.size() < 12) {
    result.status = WavAudioLoadStatus::truncated_header;
    result.message = "WAV header is truncated";
    return result;
  }
  if (!has_id(bytes, 0, "RIFF") || !has_id(bytes, 8, "WAVE")) {
    result.status = WavAudioLoadStatus::invalid_container;
    result.message = "WAV file must use a RIFF/WAVE container";
    return result;
  }

  const auto riff_size = static_cast<std::uint64_t>(read_u32(bytes, 4));
  const auto riff_end = riff_size + 8U;
  if (riff_size < 4U || riff_end < 12U || riff_end > bytes.size()) {
    result.status = WavAudioLoadStatus::truncated_header;
    result.message = "RIFF size does not describe the complete WAV file";
    return result;
  }

  bool have_format = false;
  bool have_data = false;
  std::size_t data_offset = 0;
  std::uint32_t data_bytes = 0;
  std::size_t chunk_position = 12;
  while (chunk_position < riff_end) {
    if (riff_end - chunk_position < 8U) {
      result.status = WavAudioLoadStatus::truncated_header;
      result.message = "WAV chunk header is truncated";
      return result;
    }

    const auto chunk_size =
        static_cast<std::uint64_t>(read_u32(bytes, chunk_position + 4));
    const auto chunk_data = chunk_position + 8U;
    const auto chunk_end = chunk_data + chunk_size;
    const auto padded_end = chunk_end + (chunk_size & 1U);
    if (chunk_end < chunk_data || padded_end < chunk_end ||
        padded_end > riff_end) {
      result.status = has_id(bytes, chunk_position, "data")
                          ? WavAudioLoadStatus::truncated_data
                          : WavAudioLoadStatus::truncated_header;
      result.message = "WAV chunk extends beyond the RIFF container";
      return result;
    }

    if (has_id(bytes, chunk_position, "fmt ")) {
      if (have_format || chunk_size < 16U) {
        result.status = WavAudioLoadStatus::invalid_header;
        result.message = "WAV format chunk is missing or duplicated";
        return result;
      }
      result.info.audio_format = read_u16(bytes, chunk_data);
      result.info.channels = read_u16(bytes, chunk_data + 2U);
      result.info.sample_rate = read_u32(bytes, chunk_data + 4U);
      result.info.byte_rate = read_u32(bytes, chunk_data + 8U);
      result.info.block_align = read_u16(bytes, chunk_data + 12U);
      result.info.bits_per_sample = read_u16(bytes, chunk_data + 14U);
      have_format = true;
    } else if (has_id(bytes, chunk_position, "data")) {
      if (have_data) {
        result.status = WavAudioLoadStatus::invalid_header;
        result.message = "WAV data chunk is duplicated";
        return result;
      }
      if (chunk_size == 0U) {
        result.status = WavAudioLoadStatus::empty_data;
        result.message = "WAV data chunk is empty";
        return result;
      }
      data_offset = chunk_data;
      data_bytes = static_cast<std::uint32_t>(chunk_size);
      have_data = true;
    }

    chunk_position = static_cast<std::size_t>(padded_end);
  }

  if (!have_format) {
    result.status = WavAudioLoadStatus::missing_format_chunk;
    result.message = "WAV format chunk is missing";
    return result;
  }
  if (!have_data) {
    result.status = WavAudioLoadStatus::missing_data_chunk;
    result.message = "WAV data chunk is missing";
    return result;
  }

  if (result.info.audio_format != 1U) {
    result.status = WavAudioLoadStatus::unsupported_format;
    result.message = "Only uncompressed integer PCM WAV is supported";
    return result;
  }
  if (result.info.channels == 0U || result.info.channels > 32U ||
      result.info.sample_rate == 0U || result.info.sample_rate > 384000U ||
      (result.info.bits_per_sample != 8U &&
       result.info.bits_per_sample != 16U &&
       result.info.bits_per_sample != 24U &&
       result.info.bits_per_sample != 32U)) {
    result.status = WavAudioLoadStatus::unsupported_format;
    result.message =
        "WAV PCM must use 1-32 channels, a 1-384 kHz rate and 8/16/24/32 bits";
    return result;
  }

  const auto bytes_per_sample = result.info.bits_per_sample / 8U;
  const auto expected_block_align =
      static_cast<std::uint32_t>(result.info.channels) * bytes_per_sample;
  const auto expected_byte_rate =
      static_cast<std::uint64_t>(result.info.sample_rate) * expected_block_align;
  if (expected_block_align == 0U ||
      expected_block_align > std::numeric_limits<std::uint16_t>::max() ||
      result.info.block_align != expected_block_align ||
      expected_byte_rate > std::numeric_limits<std::uint32_t>::max() ||
      result.info.byte_rate != static_cast<std::uint32_t>(expected_byte_rate)) {
    result.status = WavAudioLoadStatus::invalid_header;
    result.message = "WAV PCM rate and block alignment fields are inconsistent";
    return result;
  }
  if (data_bytes % result.info.block_align != 0U) {
    result.status = WavAudioLoadStatus::truncated_data;
    result.message = "WAV PCM data is not aligned to complete audio frames";
    return result;
  }

  result.info.data_offset = data_offset;
  result.info.data_bytes = data_bytes;
  result.info.frame_count = data_bytes / result.info.block_align;
  result.status = WavAudioLoadStatus::success;
  result.message = "WAV PCM header and data are valid";
  return result;
}

[[nodiscard]] float decode_sample(const ByteVector& bytes, std::size_t offset,
                                  std::uint16_t bits_per_sample) noexcept {
  switch (bits_per_sample) {
    case 8:
      return (static_cast<float>(bytes[offset]) - 128.0F) / 128.0F;
    case 16: {
      const auto value = static_cast<std::int16_t>(read_u16(bytes, offset));
      return static_cast<float>(value) / 32768.0F;
    }
    case 24: {
      std::int32_t value = static_cast<std::int32_t>(bytes[offset]) |
                           static_cast<std::int32_t>(bytes[offset + 1]) << 8U |
                           static_cast<std::int32_t>(bytes[offset + 2]) << 16U;
      if ((value & 0x00800000) != 0) {
        value |= ~0x00FFFFFF;
      }
      return static_cast<float>(value) / 8388608.0F;
    }
    case 32: {
      const auto value = static_cast<std::int32_t>(read_u32(bytes, offset));
      return static_cast<float>(value) / 2147483648.0F;
    }
    default:
      return 0.0F;
  }
}

[[nodiscard]] WavAudioLoadResult result_from_file_error(
    const std::filesystem::path& path, const FileReadResult& file) {
  WavAudioLoadResult result;
  result.status = file.status;
  result.path = path;
  result.message = file.message;
  return result;
}

}  // namespace

const char* wav_audio_load_status_name(WavAudioLoadStatus status) noexcept {
  switch (status) {
    case WavAudioLoadStatus::success:
      return "success";
    case WavAudioLoadStatus::empty_path:
      return "empty_path";
    case WavAudioLoadStatus::remote_source:
      return "remote_source";
    case WavAudioLoadStatus::missing:
      return "missing";
    case WavAudioLoadStatus::not_regular_file:
      return "not_regular_file";
    case WavAudioLoadStatus::unreadable:
      return "unreadable";
    case WavAudioLoadStatus::too_large:
      return "too_large";
    case WavAudioLoadStatus::truncated_header:
      return "truncated_header";
    case WavAudioLoadStatus::invalid_container:
      return "invalid_container";
    case WavAudioLoadStatus::unsupported_format:
      return "unsupported_format";
    case WavAudioLoadStatus::invalid_header:
      return "invalid_header";
    case WavAudioLoadStatus::missing_format_chunk:
      return "missing_format_chunk";
    case WavAudioLoadStatus::missing_data_chunk:
      return "missing_data_chunk";
    case WavAudioLoadStatus::empty_data:
      return "empty_data";
    case WavAudioLoadStatus::truncated_data:
      return "truncated_data";
  }
  return "unknown";
}

WavAudioValidation validate_wav_pcm(const std::filesystem::path& path,
                                    WavAudioLoaderOptions options) {
  WavAudioValidation result;
  result.path = path;
  const auto file = read_local_file(path, options);
  if (file.status != WavAudioLoadStatus::success) {
    result.status = file.status;
    result.message = file.message;
    return result;
  }

  const auto parsed = parse_wav(file.bytes);
  result.status = parsed.status;
  result.info = parsed.info;
  result.message = parsed.message;
  return result;
}

WavAudioLoadResult load_wav_pcm(const std::filesystem::path& path,
                                WavAudioLoaderOptions options) {
  WavAudioLoadResult result;
  result.path = path;
  const auto file = read_local_file(path, options);
  if (file.status != WavAudioLoadStatus::success) {
    return result_from_file_error(path, file);
  }

  const auto parsed = parse_wav(file.bytes);
  result.status = parsed.status;
  result.info = parsed.info;
  result.message = parsed.message;
  if (parsed.status != WavAudioLoadStatus::success) {
    return result;
  }

  const auto channels = static_cast<std::size_t>(parsed.info.channels);
  if (options.max_decoded_samples == 0 ||
      parsed.info.frame_count >
          std::numeric_limits<std::size_t>::max() / channels) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV PCM sample count exceeds the safe decode limit";
    return result;
  }
  const auto sample_count = parsed.info.frame_count * channels;
  if (sample_count > options.max_decoded_samples) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV PCM decoded sample count exceeds the configured limit";
    return result;
  }
  try {
    result.audio.sample_rate = parsed.info.sample_rate;
    result.audio.channels = parsed.info.channels;
    result.audio.samples.resize(sample_count);
  } catch (const std::bad_alloc&) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV PCM samples cannot be allocated for decoding";
    result.audio = {};
    return result;
  } catch (const std::length_error&) {
    result.status = WavAudioLoadStatus::too_large;
    result.message = "WAV PCM samples exceed the container allocation limit";
    result.audio = {};
    return result;
  }

  const auto bytes_per_sample = parsed.info.bits_per_sample / 8U;
  const auto sample_bytes = static_cast<std::size_t>(parsed.info.data_offset);
  for (std::size_t index = 0; index < sample_count; ++index) {
    result.audio.samples[index] = decode_sample(
        file.bytes, sample_bytes + index * bytes_per_sample,
        parsed.info.bits_per_sample);
  }

  result.status = WavAudioLoadStatus::success;
  result.message = "Decoded local WAV PCM samples into normalized floats";
  return result;
}

}  // namespace obs_whisperbleep::replacement
