// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/backend_selector.hpp"

namespace obs_whisperbleep::runtime {

const char* backend_name(Backend backend) noexcept {
  switch (backend) {
    case Backend::auto_select:
      return "Auto";
    case Backend::cpu:
      return "CPU";
    case Backend::cuda:
      return "CUDA";
  }
  return "Unknown";
}

BackendSelection select_backend(Backend requested,
                                const BackendCapabilities& capabilities) {
  BackendSelection result;
  result.requested = requested;
  if (requested == Backend::auto_select) {
    if (capabilities.cuda) {
      result.selected = Backend::cuda;
      result.message = "Auto selected CUDA";
    } else if (capabilities.cpu) {
      result.selected = Backend::cpu;
      result.message = "Auto selected CPU";
    } else {
      result.selected = Backend::cpu;
      result.used_fallback = true;
      result.message = "No backend is available";
    }
    return result;
  }

  if ((requested == Backend::cpu && capabilities.cpu) ||
      (requested == Backend::cuda && capabilities.cuda)) {
    result.selected = requested;
    result.message = "Backend is available";
    return result;
  }

  result.used_fallback = true;
  if (capabilities.cpu) {
    result.selected = Backend::cpu;
    result.message = "Requested backend is unavailable: falling back to CPU";
  } else if (capabilities.cuda) {
    result.selected = Backend::cuda;
    result.message = "CPU is unavailable: falling back to CUDA";
  } else {
    result.selected = Backend::cpu;
    result.message = "No backend is available";
  }
  return result;
}

}  // namespace obs_whisperbleep::runtime
