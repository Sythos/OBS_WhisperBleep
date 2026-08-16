// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/model/model_downloader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace obs_whisperbleep::model {
namespace {

class Sha256 final {
 public:
  Sha256() noexcept
      : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
               0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

  void update(const std::uint8_t* data, std::size_t length) noexcept {
    bit_length_ += static_cast<std::uint64_t>(length) * 8U;
    while (length != 0) {
      const auto available = block_.size() - block_size_;
      const auto count = std::min(available, length);
      std::copy_n(data, count, block_.begin() + block_size_);
      block_size_ += count;
      data += count;
      length -= count;
      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0;
      }
    }
  }

  [[nodiscard]] std::string final_hex() {
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56) {
      std::fill(block_.begin() + block_size_, block_.end(), 0U);
      transform(block_.data());
      block_size_ = 0;
    }
    std::fill(block_.begin() + block_size_, block_.begin() + 56, 0U);
    for (std::size_t index = 0; index < sizeof(bit_length_); ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_length_ >> (index * 8U));
    }
    transform(block_.data());

    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4) {
        result.push_back(digits[(word >> shift) & 0x0fU]);
      }
    }
    return result;
  }

 private:
  static constexpr std::array<std::uint32_t, 64> kRoundConstants{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
      0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
      0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
      0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
      0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
      0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
      0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  static constexpr std::uint32_t rotate_right(std::uint32_t value,
                                               unsigned count) noexcept {
    return (value >> count) | (value << (32U - count));
  }

  void transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      words[index] = (static_cast<std::uint32_t>(block[index * 4U]) << 24U) |
                     (static_cast<std::uint32_t>(block[index * 4U + 1U])
                      << 16U) |
                     (static_cast<std::uint32_t>(block[index * 4U + 2U])
                      << 8U) |
                     static_cast<std::uint32_t>(block[index * 4U + 3U]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const auto s0 = rotate_right(words[index - 15U], 7U) ^
                      rotate_right(words[index - 15U], 18U) ^
                      (words[index - 15U] >> 3U);
      const auto s1 = rotate_right(words[index - 2U], 17U) ^
                      rotate_right(words[index - 2U], 19U) ^
                      (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto s1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                      rotate_right(e, 25U);
      const auto choice = (e & f) ^ (~e & g);
      const auto temporary1 = h + s1 + choice + kRoundConstants[index] +
                              words[index];
      const auto s0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                      rotate_right(a, 22U);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_ = 0;
  std::uint64_t bit_length_ = 0;
};

[[nodiscard]] bool cancelled(const DownloadCancellation& callback) {
  return callback && callback();
}

[[nodiscard]] bool valid_sha256(std::string_view value) noexcept {
  if (value.size() != 64) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](const char character) {
    return std::isxdigit(static_cast<unsigned char>(character)) != 0;
  });
}

[[nodiscard]] std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] std::string url_decode(std::string value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '%' || index + 2 >= value.size()) {
      decoded.push_back(value[index]);
      continue;
    }
    const auto hex = [](const char character) -> int {
      if (character >= '0' && character <= '9') {
        return character - '0';
      }
      if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
      }
      if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
      }
      return -1;
    };
    const auto high = hex(value[index + 1]);
    const auto low = hex(value[index + 2]);
    if (high < 0 || low < 0) {
      decoded.push_back(value[index]);
      continue;
    }
    decoded.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }
  return decoded;
}

[[nodiscard]] std::optional<std::filesystem::path> local_source_path(
    std::string_view source_url) {
  std::string source(source_url);
  if (source.rfind("file://", 0) == 0) {
    source = url_decode(source.substr(7));
#if defined(_WIN32)
    if (source.size() >= 3 && source[0] == '/' && source[2] == ':') {
      source.erase(source.begin());
    }
#endif
    return std::filesystem::path(source);
  }
  if (source.find("://") == std::string::npos) {
    return std::filesystem::path(source);
  }
  return std::nullopt;
}

[[nodiscard]] bool hash_file(const std::filesystem::path& path,
                             const std::optional<std::uint64_t>& expected_size,
                             std::string& digest, std::string& error) {
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(path, status_error) || status_error) {
    error = "Model source is not a regular file: " + path.string();
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Unable to open model file: " + path.string();
    return false;
  }

  Sha256 sha;
  std::array<std::uint8_t, 64U * 1024U> buffer{};
  std::uint64_t bytes = 0;
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      sha.update(buffer.data(), static_cast<std::size_t>(count));
      bytes += static_cast<std::uint64_t>(count);
    }
  }
  if (!input.eof()) {
    error = "Unable to read model file: " + path.string();
    return false;
  }
  if (expected_size.has_value() && bytes != *expected_size) {
    error = "Model size does not match the catalog";
    return false;
  }
  digest = sha.final_hex();
  return true;
}

[[nodiscard]] bool copy_local_file(const std::filesystem::path& source,
                                   const std::filesystem::path& destination,
                                   const DownloadCancellation& is_cancelled,
                                   std::string& error) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    error = "Unable to open model source: " + source.string();
    return false;
  }
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Unable to create temporary model file: " + destination.string();
    return false;
  }

  std::array<char, 64U * 1024U> buffer{};
  while (input) {
    if (cancelled(is_cancelled)) {
      error = "Model download cancelled";
      return false;
    }
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      output.write(buffer.data(), count);
      if (!output) {
        error = "Unable to write temporary model file";
        return false;
      }
    }
  }
  if (!input.eof()) {
    error = "Unable to read model source";
    return false;
  }
  output.flush();
  return static_cast<bool>(output);
}

[[nodiscard]] std::filesystem::path temporary_path_for(
    const std::filesystem::path& destination) {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  auto temporary = destination;
  temporary += ".part-" + std::to_string(tick);
  return temporary;
}

[[nodiscard]] DownloadResult failure(DownloadStatus status,
                                     std::string message) {
  return {status, {}, std::move(message)};
}

}  // namespace

ModelDownloader::ModelDownloader(DownloadOptions options)
    : options_(std::move(options)) {}

DownloadVerificationResult ModelDownloader::verify_file(
    const ModelDescriptor& model, const std::filesystem::path& path) {
  if (!valid_sha256(model.sha256)) {
    return {false, "Model catalog does not contain a valid SHA-256 checksum"};
  }
  std::string digest;
  std::string error;
  if (!hash_file(path, model.expected_size_bytes, digest, error)) {
    return {false, error};
  }
  if (lower_ascii(digest) != lower_ascii(model.sha256)) {
    return {false, "Model SHA-256 checksum does not match the catalog"};
  }
  return {true, {}};
}

DownloadResult ModelDownloader::download(
    const ModelDescriptor& model, const std::filesystem::path& destination) {
  return download(model, destination, options_);
}

DownloadResult ModelDownloader::download(
    const ModelDescriptor& model, const std::filesystem::path& destination,
    const DownloadOptions& options) {
  if (model.source_url.empty()) {
    return failure(DownloadStatus::unsupported,
                   "Model has no approved source URL");
  }
  if (!valid_sha256(model.sha256)) {
    return failure(DownloadStatus::verification_failed,
                   "Model catalog does not contain a valid SHA-256 checksum");
  }
  if (destination.empty() || !destination.is_absolute() ||
      destination.filename().empty() ||
      destination.filename() == "." || destination.filename() == "..") {
    return failure(DownloadStatus::failed,
                   "Model destination must be an absolute path with a file "
                   "name");
  }
  std::filesystem::path temporary;
  try {
    if (cancelled(options.is_cancelled)) {
      return failure(DownloadStatus::cancelled, "Model download cancelled");
    }

    const auto parent = destination.parent_path().empty()
                            ? std::filesystem::path(".")
                            : destination.parent_path();
    std::error_code directory_error;
    std::filesystem::create_directories(parent, directory_error);
    if (directory_error) {
      return failure(DownloadStatus::failed,
                     "Unable to create model cache directory: " +
                         directory_error.message());
    }

    std::string existing_digest;
    std::string existing_error;
    if (std::filesystem::exists(destination) &&
        hash_file(destination, model.expected_size_bytes, existing_digest,
                  existing_error) &&
        lower_ascii(existing_digest) == lower_ascii(model.sha256)) {
      return {DownloadStatus::success, destination, "Model already verified"};
    }

    temporary = temporary_path_for(destination);
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);

    std::string transfer_error;
    bool transferred = false;
    const auto local_path = local_source_path(model.source_url);
    if (local_path.has_value()) {
      transferred = copy_local_file(*local_path, temporary,
                                    options.is_cancelled, transfer_error);
    } else if (options.transport) {
      transferred = options.transport(model.source_url, temporary,
                                      options.is_cancelled, transfer_error);
    } else {
      return failure(DownloadStatus::unsupported,
                     "HTTP(S) model download requires an injected transport");
    }

    if (!transferred) {
      std::filesystem::remove(temporary, cleanup_error);
      if (cancelled(options.is_cancelled) ||
          transfer_error == "Model download cancelled") {
        return failure(DownloadStatus::cancelled,
                       transfer_error.empty() ? "Model download cancelled"
                                              : transfer_error);
      }
      return failure(DownloadStatus::failed,
                     transfer_error.empty() ? "Model transfer failed"
                                            : transfer_error);
    }

    std::string digest;
    std::string verification_error;
    if (!hash_file(temporary, model.expected_size_bytes, digest,
                   verification_error) ||
        lower_ascii(digest) != lower_ascii(model.sha256)) {
      std::filesystem::remove(temporary, cleanup_error);
      return failure(DownloadStatus::verification_failed,
                     verification_error.empty()
                         ? "Model SHA-256 checksum does not match the catalog"
                         : verification_error);
    }

    auto backup = destination;
    backup += ".backup";
    std::error_code move_error;
    const bool had_destination = std::filesystem::exists(destination);
    if (had_destination) {
      std::filesystem::remove(backup, cleanup_error);
      std::filesystem::rename(destination, backup, move_error);
      if (move_error) {
        std::filesystem::remove(temporary, cleanup_error);
        return failure(DownloadStatus::failed,
                       "Unable to stage the previous model: " +
                           move_error.message());
      }
    }
    std::filesystem::rename(temporary, destination, move_error);
    if (move_error) {
      std::string message = "Unable to activate the downloaded model: " +
                            move_error.message();
      if (had_destination) {
        std::error_code restore_error;
        std::filesystem::rename(backup, destination, restore_error);
        if (restore_error) {
          message += "; unable to restore the previous model: " +
                     restore_error.message();
        }
      }
      std::filesystem::remove(temporary, cleanup_error);
      return failure(DownloadStatus::failed, std::move(message));
    }
    if (had_destination) {
      std::filesystem::remove(backup, cleanup_error);
    }
    return {DownloadStatus::success, destination, "Model verified and cached"};
  } catch (const std::exception& error) {
    if (!temporary.empty()) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary, cleanup_error);
    }
    return failure(DownloadStatus::failed,
                   std::string("Model download failed: ") + error.what());
  } catch (...) {
    if (!temporary.empty()) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary, cleanup_error);
    }
    return failure(DownloadStatus::failed,
                   "Model download failed with an unknown error");
  }
}

}  // namespace obs_whisperbleep::model
