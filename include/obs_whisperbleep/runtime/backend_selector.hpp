// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>

namespace obs_whisperbleep::runtime {

enum class Backend {
  auto_select,
  cpu,
  cuda_13_2,
  // Compatibility alias for callers that used the pre-M3 CUDA name.
  cuda = cuda_13_2,
};

struct BackendCapabilities {
  bool cpu = true;
  // `cuda` is retained for source compatibility; `cuda_13_2` is the explicit
  // capability used by the M3 and future UI/backend contract.
  bool cuda = false;
  bool cuda_13_2 = false;
};

struct BackendSelection {
  Backend requested = Backend::auto_select;
  Backend selected = Backend::cpu;
  bool requested_available = false;
  bool selected_available = false;
  bool used_fallback = false;
  std::string fallback_reason;
  std::string message;
};

[[nodiscard]] BackendSelection select_backend(
    Backend requested, const BackendCapabilities& capabilities);
[[nodiscard]] const char* backend_name(Backend backend) noexcept;

}  // namespace obs_whisperbleep::runtime
