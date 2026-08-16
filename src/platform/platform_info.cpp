// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/platform/platform_info.hpp"

#include <cstdlib>

namespace {

[[nodiscard]] std::filesystem::path environment_path(const char* name) {
  const auto* value = std::getenv(name);
  return value == nullptr || *value == '\0' ? std::filesystem::path{}
                                             : std::filesystem::path(value);
}

}  // namespace

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

std::filesystem::path user_cache_directory(
    const std::string_view application_name) {
  if (application_name.empty()) {
    return {};
  }

  std::filesystem::path root;
#if defined(_WIN32)
  root = environment_path("LOCALAPPDATA");
  if (root.empty()) {
    root = environment_path("APPDATA");
  }
#elif defined(__APPLE__)
  root = environment_path("HOME");
  if (!root.empty()) {
    root /= "Library";
    root /= "Caches";
  }
#else
  root = environment_path("XDG_CACHE_HOME");
  if (root.empty()) {
    root = environment_path("HOME");
    if (!root.empty()) {
      root /= ".cache";
    }
  }
#endif

  if (root.empty()) {
    return {};
  }
  root /= std::string(application_name);
  root /= "models";
  return root;
}

}  // namespace obs_whisperbleep::platform
