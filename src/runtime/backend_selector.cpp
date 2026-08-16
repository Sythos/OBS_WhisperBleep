// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/backend_selector.hpp"

namespace obs_whisperbleep::runtime {

namespace {

[[nodiscard]] bool cuda_available(
    const BackendCapabilities& capabilities) noexcept {
  return capabilities.cuda || capabilities.cuda_13_2;
}

}  // namespace

const char* backend_name(Backend backend) noexcept {
  switch (backend) {
    case Backend::auto_select:
      return "Auto";
    case Backend::cpu:
      return "CPU";
    case Backend::cuda_13_2:
      return "CUDA 13.2";
  }
  return "Unknown";
}

BackendSelection select_backend(Backend requested,
                                const BackendCapabilities& capabilities) {
  BackendSelection result;
  result.requested = requested;
  if (requested == Backend::auto_select) {
    if (cuda_available(capabilities)) {
      result.selected = Backend::cuda;
      result.requested_available = false;
      result.selected_available = true;
      result.message = "Auto selected CUDA 13.2";
    } else if (capabilities.cpu) {
      result.selected = Backend::cpu;
      result.requested_available = false;
      result.selected_available = true;
      result.message = "Auto selected CPU";
    } else {
      result.selected = Backend::cpu;
      result.used_fallback = true;
      result.fallback_reason = "No backend is available";
      result.message = "No backend is available";
    }
    return result;
  }

  if ((requested == Backend::cpu && capabilities.cpu) ||
      (requested == Backend::cuda_13_2 && cuda_available(capabilities))) {
    result.selected = requested;
    result.requested_available = true;
    result.selected_available = true;
    result.message = "Backend is available";
    return result;
  }

  result.used_fallback = true;
  if (capabilities.cpu) {
    result.selected = Backend::cpu;
    result.selected_available = true;
    result.fallback_reason = "Requested backend is unavailable";
    result.message = "Requested backend is unavailable: falling back to CPU";
  } else if (cuda_available(capabilities)) {
    result.selected = Backend::cuda;
    result.selected_available = true;
    result.fallback_reason = "CPU is unavailable";
    result.message = "CPU is unavailable: falling back to CUDA 13.2";
  } else {
    result.selected = Backend::cpu;
    result.fallback_reason = "No backend is available";
    result.message = "No backend is available";
  }
  return result;
}

}  // namespace obs_whisperbleep::runtime
