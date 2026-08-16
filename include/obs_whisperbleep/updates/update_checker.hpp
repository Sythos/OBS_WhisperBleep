// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace obs_whisperbleep::updates {

enum class UpdateStatus {
  up_to_date,
  update_available,
  network_error,
  invalid_response,
};

[[nodiscard]] const char* update_status_name(UpdateStatus status) noexcept;

/**
 * Provides the response body for a GitHub release request.
 *
 * The transport owns all network policy, including timeouts and TLS. It must
 * return true only when the request completed and populate response_body.
 */
using ReleaseTransport = std::function<bool(std::string_view request_url,
                                            std::string& response_body,
                                            std::string& error_message)>;

struct UpdateCheckResult {
  UpdateStatus status = UpdateStatus::invalid_response;
  std::string installed_version;
  std::string latest_version;
  std::string latest_tag;
  std::string release_url;
  std::string message;
};

/**
 * Compares the installed project version with the latest GitHub release.
 *
 * This class deliberately has no network dependency. A platform integration
 * supplies a transport, while the checker only parses the release response,
 * compares semantic versions, and returns a browser URL. It never downloads
 * or installs an update.
 */
class UpdateChecker {
 public:
  static constexpr std::string_view latest_release_api_url() noexcept {
    return "https://api.github.com/repos/Sythos/OBS_WhisperBleep/releases/latest";
  }

  static constexpr std::string_view releases_url() noexcept {
    return "https://github.com/Sythos/OBS_WhisperBleep/releases";
  }

  explicit UpdateChecker(ReleaseTransport transport = {});

  [[nodiscard]] UpdateCheckResult check(
      std::string_view installed_version) const;

 private:
  ReleaseTransport transport_;
};

}  // namespace obs_whisperbleep::updates
