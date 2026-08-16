// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>

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

}  // namespace obs_whisperbleep::platform
