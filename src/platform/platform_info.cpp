// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/platform/platform_info.hpp"

namespace obs_whisperbleep::platform {

std::string PlatformInfo::name() const {
  std::string result;
  switch (operating_system) {
    case OperatingSystem::windows:
      result = "Windows";
      break;
    case OperatingSystem::linux:
      result = "Linux";
      break;
    case OperatingSystem::macos:
      result = "macOS";
      break;
    case OperatingSystem::unknown:
      result = "Unknown";
      break;
  }
  result += " ";
  switch (architecture) {
    case Architecture::x86_64:
      result += "x86_64";
      break;
    case Architecture::arm64:
      result += "arm64";
      break;
    case Architecture::unknown:
      result += "unknown";
      break;
  }
  if (universal) {
    result += " universal";
  }
  return result;
}

PlatformInfo current_platform() noexcept {
  PlatformInfo info;
#if defined(_WIN32)
  info.operating_system = OperatingSystem::windows;
#elif defined(__APPLE__)
  info.operating_system = OperatingSystem::macos;
#elif defined(__linux__)
  info.operating_system = OperatingSystem::linux;
#endif

#if defined(_M_X64) || defined(__x86_64__)
  info.architecture = Architecture::x86_64;
#elif defined(_M_ARM64) || defined(__aarch64__)
  info.architecture = Architecture::arm64;
#endif
  return info;
}

}  // namespace obs_whisperbleep::platform
