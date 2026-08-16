// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>

namespace obs_whisperbleep::runtime {

enum class Backend { auto_select, cpu, cuda };

struct BackendCapabilities {
  bool cpu = true;
  bool cuda = false;
};

struct BackendSelection {
  Backend requested = Backend::auto_select;
  Backend selected = Backend::cpu;
  bool used_fallback = false;
  std::string message;
};

[[nodiscard]] BackendSelection select_backend(
    Backend requested, const BackendCapabilities& capabilities);
[[nodiscard]] const char* backend_name(Backend backend) noexcept;

}  // namespace obs_whisperbleep::runtime
