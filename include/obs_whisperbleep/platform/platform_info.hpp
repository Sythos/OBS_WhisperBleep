// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace obs_whisperbleep::platform {

enum class OperatingSystem { windows, linux, macos, unknown };
enum class Architecture { x86_64, arm64, unknown };

struct PlatformInfo {
  OperatingSystem operating_system = OperatingSystem::unknown;
  Architecture architecture = Architecture::unknown;
  bool universal = false;
  [[nodiscard]] std::string name() const;
};

[[nodiscard]] PlatformInfo current_platform() noexcept;

/** Returns the per-user cache directory for a project-specific data scope. */
[[nodiscard]] std::filesystem::path user_cache_directory(
    std::string_view application_name);

}  // namespace obs_whisperbleep::platform
