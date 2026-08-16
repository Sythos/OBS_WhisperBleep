// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/replacement/replacement_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

namespace obs_whisperbleep::replacement {
namespace {

[[nodiscard]] bool has_remote_scheme(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  auto separator = text.find("://");
  if (separator == std::string::npos) {
    // Some standard-library path implementations collapse a URI's double
    // slash while constructing a filesystem path (for example, https:/...).
    // Recognize that representation too, while excluding Windows drive roots.
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

[[nodiscard]] std::string lower_ascii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return text;
}

[[nodiscard]] bool supported_extension(const std::filesystem::path& path) {
  static constexpr std::array<std::string_view, 8> extensions{
      ".aac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wave"};
  const auto extension = lower_ascii(path.extension().string());
  return std::find(extensions.begin(), extensions.end(), extension) !=
         extensions.end();
}

[[nodiscard]] core::AudioBuffer empty_audio(std::uint32_t sample_rate,
                                             std::uint16_t channels) {
  return core::AudioBuffer{sample_rate, channels, {}};
}

}  // namespace

CustomAudioValidation validate_custom_audio(
    const std::filesystem::path& path) {
  CustomAudioValidation result;
  result.path = path;

  if (path.empty()) {
    result.status = CustomAudioValidationStatus::empty_path;
    result.message = "Custom audio path is empty";
    return result;
  }
  if (has_remote_scheme(path)) {
    result.status = CustomAudioValidationStatus::remote_source;
    result.message =
        "Remote audio sources are not accepted; select a local file";
    return result;
  }

  std::error_code error;
  if (!std::filesystem::exists(path, error) || error) {
    result.status = CustomAudioValidationStatus::missing;
    result.message = "Custom audio file does not exist";
    return result;
  }
  if (!std::filesystem::is_regular_file(path, error) || error) {
    result.status = CustomAudioValidationStatus::not_regular_file;
    result.message = "Custom audio path is not a regular file";
    return result;
  }
  if (!supported_extension(path)) {
    result.status = CustomAudioValidationStatus::unsupported_format;
    result.message =
        "Custom audio extension is not one of the supported audio formats";
    return result;
  }

  result.size_bytes = std::filesystem::file_size(path, error);
  if (error) {
    result.status = CustomAudioValidationStatus::unreadable;
    result.message = "Custom audio file size cannot be read";
    return result;
  }
  if (result.size_bytes == 0) {
    result.status = CustomAudioValidationStatus::empty_file;
    result.message = "Custom audio file is empty";
    return result;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    result.status = CustomAudioValidationStatus::unreadable;
    result.message = "Custom audio file cannot be opened for reading";
    return result;
  }

  result.status = CustomAudioValidationStatus::valid;
  result.message = "Custom audio file is a readable local asset";
  return result;
}

std::vector<ReplacementOption> ReplacementCatalog::options() {
  return {{ReplacementKind::beep, "beep", "Beep", true, false},
          {ReplacementKind::duck, "duck", "Duck", false, true},
          {ReplacementKind::bark, "bark", "Bark", false, true},
          {ReplacementKind::custom, "custom", "Custom audio", false, true}};
}

std::string_view ReplacementCatalog::kind_name(ReplacementKind kind) noexcept {
  switch (kind) {
    case ReplacementKind::beep:
      return "beep";
    case ReplacementKind::duck:
      return "duck";
    case ReplacementKind::bark:
      return "bark";
    case ReplacementKind::custom:
      return "custom";
  }
  return "unknown";
}

bool ReplacementCatalog::register_asset(ReplacementKind kind,
                                        core::AudioBuffer asset) {
  if (kind == ReplacementKind::beep || !valid_asset(asset)) {
    return false;
  }

  switch (kind) {
    case ReplacementKind::duck:
      duck_asset_ = std::move(asset);
      return true;
    case ReplacementKind::bark:
      bark_asset_ = std::move(asset);
      return true;
    case ReplacementKind::custom:
      custom_asset_ = std::move(asset);
      return true;
    case ReplacementKind::beep:
      return false;
  }
  return false;
}

bool ReplacementCatalog::clear_asset(ReplacementKind kind) noexcept {
  switch (kind) {
    case ReplacementKind::duck:
      duck_asset_.reset();
      return true;
    case ReplacementKind::bark:
      bark_asset_.reset();
      return true;
    case ReplacementKind::custom:
      custom_asset_.reset();
      return true;
    case ReplacementKind::beep:
      return false;
  }
  return false;
}

bool ReplacementCatalog::has_asset(ReplacementKind kind) const noexcept {
  switch (kind) {
    case ReplacementKind::duck:
      return duck_asset_.has_value();
    case ReplacementKind::bark:
      return bark_asset_.has_value();
    case ReplacementKind::custom:
      return custom_asset_.has_value();
    case ReplacementKind::beep:
      return true;
  }
  return false;
}

ReplacementBuildResult ReplacementCatalog::build(
    const ReplacementRequest& request, std::uint32_t sample_rate,
    std::uint16_t channels, std::size_t frame_count) const {
  if (request.kind == ReplacementKind::beep) {
    return {ReplacementBuildStatus::success,
            core::SyntheticReplacement::beep(sample_rate, channels, frame_count,
                                              request.beep_options),
            "Generated deterministic beep"};
  }

  const std::optional<core::AudioBuffer>* asset = nullptr;
  switch (request.kind) {
    case ReplacementKind::duck:
      asset = &duck_asset_;
      break;
    case ReplacementKind::bark:
      asset = &bark_asset_;
      break;
    case ReplacementKind::custom:
      asset = &custom_asset_;
      break;
    case ReplacementKind::beep:
      break;
  }

  if (asset == nullptr || !asset->has_value()) {
    return {ReplacementBuildStatus::missing_asset,
            empty_audio(sample_rate, channels),
            "No decoded local asset has been registered for " +
                std::string(kind_name(request.kind))};
  }
  if (!valid_asset(asset->value())) {
    return {ReplacementBuildStatus::invalid_asset,
            empty_audio(sample_rate, channels),
            "Registered replacement asset is invalid"};
  }

  return {ReplacementBuildStatus::success, asset->value(),
          "Using registered local replacement asset"};
}

bool ReplacementCatalog::valid_asset(const core::AudioBuffer& asset) noexcept {
  return asset.sample_rate != 0 && asset.channels != 0 &&
         !asset.samples.empty() &&
         asset.samples.size() % static_cast<std::size_t>(asset.channels) == 0;
}

}  // namespace obs_whisperbleep::replacement
