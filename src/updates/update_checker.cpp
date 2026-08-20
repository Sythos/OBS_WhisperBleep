// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#include "obs_whisperbleep/updates/update_checker.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct VersionIdentifier {
  bool numeric = false;
  std::uint64_t number = 0;
  std::string text;
};

struct SemanticVersion {
  std::uint64_t major = 0;
  std::uint64_t minor = 0;
  std::uint64_t patch = 0;
  std::vector<VersionIdentifier> prerelease;
};

[[nodiscard]] bool is_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] bool parse_number(std::string_view value, std::size_t& offset,
                                std::uint64_t& result) {
  const auto start = offset;
  if (start >= value.size() || !is_digit(value[start])) {
    return false;
  }
  if (value[start] == '0' && start + 1 < value.size() &&
      is_digit(value[start + 1])) {
    return false;
  }

  result = 0;
  while (offset < value.size() && is_digit(value[offset])) {
    const auto digit = static_cast<std::uint64_t>(value[offset] - '0');
    if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
    ++offset;
  }
  return true;
}

[[nodiscard]] std::optional<SemanticVersion> parse_version(
    std::string_view input) {
  std::string value = trim(input);
  if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
    value.erase(value.begin());
  }
  if (value.empty()) {
    return std::nullopt;
  }

  SemanticVersion version;
  std::size_t offset = 0;
  if (!parse_number(value, offset, version.major) || offset >= value.size() ||
      value[offset++] != '.' || !parse_number(value, offset, version.minor) ||
      offset >= value.size() || value[offset++] != '.' ||
      !parse_number(value, offset, version.patch)) {
    return std::nullopt;
  }

  if (offset == value.size()) {
    return version;
  }
  if (value[offset] != '-' && value[offset] != '+') {
    return std::nullopt;
  }

  // Build metadata does not affect precedence. It is still validated so a
  // malformed GitHub tag cannot be silently treated as a valid release.
  const bool has_prerelease = value[offset] == '-';
  ++offset;
  const auto metadata_start = offset;
  while (offset < value.size()) {
    const char character = value[offset];
    const bool valid = is_digit(character) ||
                       (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       character == '.' || character == '-';
    if (!valid) {
      return std::nullopt;
    }
    ++offset;
  }
  if (offset == metadata_start || value.back() == '.' || value.back() == '-') {
    return std::nullopt;
  }

  if (!has_prerelease) {
    return version;
  }

  std::size_t identifier_start = metadata_start;
  while (identifier_start < value.size()) {
    const auto separator = value.find('.', identifier_start);
    const auto identifier_end = separator == std::string::npos
                                    ? value.size()
                                    : separator;
    const auto identifier = std::string_view(
        value).substr(identifier_start, identifier_end - identifier_start);
    if (identifier.empty()) {
      return std::nullopt;
    }

    VersionIdentifier parsed;
    parsed.text = std::string(identifier);
    std::size_t number_offset = 0;
    if (parse_number(identifier, number_offset, parsed.number) &&
        number_offset == identifier.size()) {
      parsed.numeric = true;
    } else {
      for (const char character : identifier) {
        if (!(is_digit(character) ||
              (character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') || character == '-')) {
          return std::nullopt;
        }
      }
      if (identifier.front() == '0' && identifier.size() > 1 &&
          is_digit(identifier[1])) {
        return std::nullopt;
      }
    }
    version.prerelease.push_back(std::move(parsed));
    if (separator == std::string::npos) {
      break;
    }
    identifier_start = separator + 1;
  }
  return version;
}

[[nodiscard]] int compare_versions(const SemanticVersion& left,
                                   const SemanticVersion& right) noexcept {
  if (left.major != right.major) {
    return left.major < right.major ? -1 : 1;
  }
  if (left.minor != right.minor) {
    return left.minor < right.minor ? -1 : 1;
  }
  if (left.patch != right.patch) {
    return left.patch < right.patch ? -1 : 1;
  }

  if (left.prerelease.empty() && right.prerelease.empty()) {
    return 0;
  }
  if (left.prerelease.empty()) {
    return 1;
  }
  if (right.prerelease.empty()) {
    return -1;
  }

  const auto count = std::min(left.prerelease.size(), right.prerelease.size());
  for (std::size_t index = 0; index < count; ++index) {
    const auto& left_identifier = left.prerelease[index];
    const auto& right_identifier = right.prerelease[index];
    if (left_identifier.numeric && right_identifier.numeric) {
      if (left_identifier.number != right_identifier.number) {
        return left_identifier.number < right_identifier.number ? -1 : 1;
      }
    } else if (left_identifier.numeric != right_identifier.numeric) {
      return left_identifier.numeric ? -1 : 1;
    } else if (left_identifier.text != right_identifier.text) {
      return left_identifier.text < right_identifier.text ? -1 : 1;
    }
  }
  if (left.prerelease.size() != right.prerelease.size()) {
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
  }
  return 0;
}

void skip_whitespace(std::string_view body, std::size_t& offset) noexcept {
  while (offset < body.size() &&
         std::isspace(static_cast<unsigned char>(body[offset])) != 0) {
    ++offset;
  }
}

[[nodiscard]] std::optional<std::string> json_string_value(
    std::string_view body, std::string_view key) {
  const std::string key_token = "\"" + std::string(key) + "\"";
  std::size_t search_offset = 0;
  while (true) {
    const auto key_offset = body.find(key_token, search_offset);
    if (key_offset == std::string_view::npos) {
      return std::nullopt;
    }
    std::size_t offset = key_offset + key_token.size();
    skip_whitespace(body, offset);
    if (offset >= body.size() || body[offset++] != ':') {
      search_offset = key_offset + 1;
      continue;
    }
    skip_whitespace(body, offset);
    if (offset >= body.size() || body[offset++] != '"') {
      return std::nullopt;
    }

    std::string result;
    while (offset < body.size()) {
      const char character = body[offset++];
      if (character == '"') {
        return result;
      }
      if (character != '\\') {
        if (static_cast<unsigned char>(character) < 0x20) {
          return std::nullopt;
        }
        result.push_back(character);
        continue;
      }
      if (offset >= body.size()) {
        return std::nullopt;
      }
      const char escaped = body[offset++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }
}

[[nodiscard]] obs_whisperbleep::updates::UpdateCheckResult invalid_result(
    std::string_view installed_version, std::string message) {
  obs_whisperbleep::updates::UpdateCheckResult result;
  result.status = obs_whisperbleep::updates::UpdateStatus::invalid_response;
  result.installed_version = std::string(installed_version);
  result.release_url = std::string(
      obs_whisperbleep::updates::UpdateChecker::releases_url());
  result.message = std::move(message);
  return result;
}

}  // namespace

namespace obs_whisperbleep::updates {

const char* update_status_name(const UpdateStatus status) noexcept {
  switch (status) {
    case UpdateStatus::up_to_date:
      return "up-to-date";
    case UpdateStatus::update_available:
      return "update-available";
    case UpdateStatus::network_error:
      return "network-error";
    case UpdateStatus::invalid_response:
      return "invalid-response";
  }
  return "invalid-response";
}

UpdateChecker::UpdateChecker(ReleaseTransport transport)
    : transport_(std::move(transport)) {}

UpdateCheckResult UpdateChecker::check(
    const std::string_view installed_version,
    const std::string_view locale) const {
  const auto installed = parse_version(installed_version);
  if (!installed.has_value()) {
    return invalid_result(
        installed_version,
        std::string(ui::translate(locale, ui::keys::update_invalid_installed)));
  }

  UpdateCheckResult result;
  result.installed_version = std::string(installed_version);
  result.release_url = std::string(releases_url());

  if (!transport_) {
    result.status = UpdateStatus::network_error;
    result.message = std::string(
        ui::translate(locale, ui::keys::update_network_error));
    return result;
  }

  std::string response_body;
  std::string error_message;
  bool request_succeeded = false;
  try {
    request_succeeded = transport_(latest_release_api_url(), response_body,
                                   error_message);
  } catch (const std::exception& exception) {
    error_message = exception.what();
  } catch (...) {
    error_message = "The release transport failed unexpectedly";
  }
  if (!request_succeeded) {
    result.status = UpdateStatus::network_error;
    result.message = error_message.empty()
                         ? std::string(ui::translate(
                               locale, ui::keys::update_network_error))
                         : std::move(error_message);
    return result;
  }

  const auto tag = json_string_value(response_body, "tag_name");
  if (!tag.has_value()) {
    return invalid_result(
        installed_version,
        std::string(ui::translate(locale, ui::keys::update_invalid_response)));
  }
  const auto latest = parse_version(*tag);
  if (!latest.has_value()) {
    return invalid_result(
        installed_version,
        std::string(ui::translate(locale, ui::keys::update_invalid_response)));
  }

  result.latest_tag = *tag;
  result.latest_version = trim(*tag);
  if (!result.latest_version.empty() &&
      (result.latest_version.front() == 'v' ||
       result.latest_version.front() == 'V')) {
    result.latest_version.erase(result.latest_version.begin());
  }

  const auto comparison = compare_versions(*installed, *latest);
  if (comparison < 0) {
    result.status = UpdateStatus::update_available;
    result.message =
        std::string(ui::translate(locale, ui::keys::update_available));
  } else {
    result.status = UpdateStatus::up_to_date;
    result.message =
        std::string(ui::translate(locale, ui::keys::update_up_to_date));
  }
  return result;
}

}  // namespace obs_whisperbleep::updates
