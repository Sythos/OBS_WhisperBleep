// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obs_whisperbleep/core/replacement_renderer.hpp"
#include "obs_whisperbleep/core/synthetic_replacement.hpp"

namespace obs_whisperbleep::replacement {

/** Replacement choices exposed by the future OBS properties UI. */
enum class ReplacementKind {
  beep,
  duck,
  bark,
  custom,
};

struct ReplacementOption {
  ReplacementKind kind = ReplacementKind::beep;
  std::string id;
  std::string label;
  bool synthetic = false;
  bool requires_asset = false;
};

enum class CustomAudioValidationStatus {
  valid,
  empty_path,
  remote_source,
  missing,
  not_regular_file,
  unsupported_format,
  unreadable,
  empty_file,
};

struct CustomAudioValidation {
  CustomAudioValidationStatus status = CustomAudioValidationStatus::empty_path;
  std::filesystem::path path;
  std::uintmax_t size_bytes = 0;
  std::string message;

  [[nodiscard]] bool valid() const noexcept {
    return status == CustomAudioValidationStatus::valid;
  }
};

/**
 * Validates a user-selected local audio file without downloading or decoding
 * it. Decoding is deliberately left to a later, explicitly injected audio
 * loader; this function is safe to call before an asset is registered.
 */
[[nodiscard]] CustomAudioValidation validate_custom_audio(
    const std::filesystem::path& path);

struct ReplacementRequest {
  ReplacementKind kind = ReplacementKind::beep;
  core::BeepOptions beep_options{};
};

enum class ReplacementBuildStatus {
  success,
  missing_asset,
  invalid_asset,
};

struct ReplacementBuildResult {
  ReplacementBuildStatus status = ReplacementBuildStatus::missing_asset;
  core::AudioBuffer audio;
  std::string message;

  [[nodiscard]] bool success() const noexcept {
    return status == ReplacementBuildStatus::success;
  }
};

/**
 * Dependency-free replacement catalog.
 *
 * The catalog generates the beep locally and accepts already-decoded audio
 * buffers for the duck, bark, and custom choices. It never performs network
 * access, file downloads, or audio decoding. Callers must validate and load
 * an asset explicitly, then register the resulting AudioBuffer.
 */
class ReplacementCatalog {
 public:
  ReplacementCatalog() = default;

  [[nodiscard]] static std::vector<ReplacementOption> options();
  [[nodiscard]] static std::string_view kind_name(ReplacementKind kind) noexcept;

  /** Registers a decoded asset for duck, bark, or custom replacement. */
  [[nodiscard]] bool register_asset(ReplacementKind kind,
                                    core::AudioBuffer asset);
  [[nodiscard]] bool clear_asset(ReplacementKind kind) noexcept;
  [[nodiscard]] bool has_asset(ReplacementKind kind) const noexcept;

  /**
   * Builds a replacement buffer for the requested choice. For an asset-backed
   * choice, the registered buffer is returned unchanged; channel and duration
   * adaptation remains the responsibility of ReplacementRenderer.
   */
  [[nodiscard]] ReplacementBuildResult build(
      const ReplacementRequest& request, std::uint32_t sample_rate,
      std::uint16_t channels, std::size_t frame_count) const;

 private:
  [[nodiscard]] static bool valid_asset(const core::AudioBuffer& asset) noexcept;

  std::optional<core::AudioBuffer> duck_asset_;
  std::optional<core::AudioBuffer> bark_asset_;
  std::optional<core::AudioBuffer> custom_asset_;
};

}  // namespace obs_whisperbleep::replacement
