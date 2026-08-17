// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/backend_selector.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <utility>

namespace obs_whisperbleep::runtime {

namespace {

[[nodiscard]] bool cuda_available(
    const BackendCapabilities& capabilities) noexcept {
  return capabilities.cuda || capabilities.cuda_13_2;
}

[[nodiscard]] BackendCapabilityDetail make_detail(
    Backend backend, bool available, std::string reason) {
  BackendCapabilityDetail detail;
  detail.backend = backend;
  detail.status = available ? BackendProbeStatus::available
                            : BackendProbeStatus::unavailable;
  detail.reason = std::move(reason);
  return detail;
}

[[nodiscard]] std::string default_reason(Backend backend,
                                         bool available) {
  if (backend == Backend::cpu) {
    return available ? "CPU backend is available"
                     : "CPU backend is unavailable";
  }
  return available ? "CUDA 13.2 backend is available"
                   : "CUDA 13.2 backend is unavailable";
}

[[nodiscard]] bool platform_allows_cuda(std::string_view platform) {
  if (platform.empty()) {
    return true;
  }

  std::string normalized(platform);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return normalized.find("windows") != std::string::npos ||
         normalized.find("linux") != std::string::npos;
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

const char* backend_probe_status_name(BackendProbeStatus status) noexcept {
  switch (status) {
    case BackendProbeStatus::available:
      return "available";
    case BackendProbeStatus::unavailable:
      return "unavailable";
    case BackendProbeStatus::not_probed:
      return "not_probed";
  }
  return "unknown";
}

BackendCapabilityDetail backend_capability_detail(
    const BackendProbeResult& probe_result, Backend backend) {
  for (const auto& detail : probe_result.details) {
    if (detail.backend == backend) {
      return detail;
    }
  }

  if (backend == Backend::cpu) {
    return make_detail(backend, probe_result.capabilities.cpu,
                       default_reason(backend, probe_result.capabilities.cpu));
  }
  if (backend == Backend::cuda_13_2) {
    const bool available = cuda_available(probe_result.capabilities);
    return make_detail(backend, available, default_reason(backend, available));
  }

  BackendCapabilityDetail detail;
  detail.backend = backend;
  detail.reason = "Backend has no concrete capability probe";
  return detail;
}

BackendProbeResult probe_backend_capabilities(
    const BackendCapabilityProbe& probe, std::string_view platform) {
  BackendProbeResult result;
  result.platform = std::string(platform);
  result.details.reserve(2);

  if (!probe) {
    result.capabilities.cpu = true;
    result.capabilities.cuda = false;
    result.capabilities.cuda_13_2 = false;
    result.details.push_back(BackendCapabilityDetail{
        Backend::cpu,
        BackendProbeStatus::available,
        "portable-core",
        {},
        {},
        "CPU fallback is available without an accelerator probe"});
    result.details.push_back(BackendCapabilityDetail{
        Backend::cuda_13_2,
        BackendProbeStatus::not_probed,
        {},
        {},
        {},
        "CUDA 13.2 capability probe was not injected"});
    return result;
  }

  for (const Backend backend : {Backend::cpu, Backend::cuda_13_2}) {
    BackendCapabilityDetail detail;
    try {
      detail = probe(backend);
    } catch (const std::exception& error) {
      detail.backend = backend;
      detail.status = BackendProbeStatus::unavailable;
      detail.reason = std::string("Capability probe failed: ") + error.what();
    } catch (...) {
      detail.backend = backend;
      detail.status = BackendProbeStatus::unavailable;
      detail.reason = "Capability probe failed with an unknown error";
    }

    // The requested backend is authoritative; a faulty adapter cannot make a
    // CPU result look like CUDA (or vice versa) by returning the wrong enum.
    detail.backend = backend;
    if (detail.reason.empty()) {
      detail.reason = detail.status == BackendProbeStatus::available
                           ? "Capability probe reported backend available"
                           : "Capability probe reported backend unavailable";
    }
    if (backend == Backend::cpu) {
      result.capabilities.cpu =
          detail.status == BackendProbeStatus::available;
    } else {
      result.capabilities.cuda_13_2 =
          detail.status == BackendProbeStatus::available;
      result.capabilities.cuda = result.capabilities.cuda_13_2;
    }
    result.details.push_back(std::move(detail));
  }

  if (!platform_allows_cuda(result.platform)) {
    result.capabilities.cuda = false;
    result.capabilities.cuda_13_2 = false;
    for (auto& detail : result.details) {
      if (detail.backend == Backend::cuda_13_2) {
        detail.status = BackendProbeStatus::unavailable;
        detail.reason =
            "CUDA 13.2 is not supported on the selected platform";
      }
    }
  }

  return result;
}

BackendSelection select_backend(Backend requested,
                                const BackendCapabilities& capabilities) {
  BackendProbeResult probe_result;
  probe_result.capabilities = capabilities;
  probe_result.details = {
      make_detail(Backend::cpu, capabilities.cpu,
                  default_reason(Backend::cpu, capabilities.cpu)),
      make_detail(Backend::cuda_13_2, cuda_available(capabilities),
                  default_reason(Backend::cuda_13_2,
                                 cuda_available(capabilities)))};
  return select_backend(requested, probe_result);
}

BackendSelection select_backend(Backend requested,
                                const BackendProbeResult& probe_result) {
  BackendProbeResult effective_probe = probe_result;
  // Prefer concrete detail records over aggregate flags when both are
  // supplied. This keeps manually assembled probe results from claiming a
  // backend that its evidence explicitly marks unavailable or unprobed.
  for (const auto& detail : effective_probe.details) {
    if (detail.backend == Backend::cpu) {
      effective_probe.capabilities.cpu =
          detail.status == BackendProbeStatus::available;
    } else if (detail.backend == Backend::cuda_13_2) {
      effective_probe.capabilities.cuda_13_2 =
          detail.status == BackendProbeStatus::available;
      effective_probe.capabilities.cuda =
          effective_probe.capabilities.cuda_13_2;
    }
  }
  if (!platform_allows_cuda(effective_probe.platform)) {
    effective_probe.capabilities.cuda = false;
    effective_probe.capabilities.cuda_13_2 = false;
    for (auto& detail : effective_probe.details) {
      if (detail.backend == Backend::cuda_13_2) {
        detail.status = BackendProbeStatus::unavailable;
        detail.reason =
            "CUDA 13.2 is not supported on the selected platform";
      }
    }
  }

  BackendSelection result;
  result.requested = requested;
  const auto finish = [&]() {
    result.requested_detail =
        backend_capability_detail(effective_probe, requested);
    result.selected_detail =
        backend_capability_detail(effective_probe, result.selected);
    return result;
  };
  const bool cuda_available_now = cuda_available(effective_probe.capabilities);

  if (requested == Backend::auto_select) {
    if (cuda_available_now) {
      result.selected = Backend::cuda;
      result.requested_available = false;
      result.selected_available = true;
      result.message = "Auto selected CUDA 13.2";
    } else if (effective_probe.capabilities.cpu) {
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
    return finish();
  }

  if ((requested == Backend::cpu && effective_probe.capabilities.cpu) ||
      (requested == Backend::cuda_13_2 && cuda_available_now)) {
    result.selected = requested;
    result.requested_available = true;
    result.selected_available = true;
    result.message = "Backend is available";
    return finish();
  }

  result.used_fallback = true;
  if (effective_probe.capabilities.cpu) {
    result.selected = Backend::cpu;
    result.selected_available = true;
    result.fallback_reason = "Requested backend is unavailable";
    result.message = "Requested backend is unavailable: falling back to CPU";
  } else if (cuda_available_now) {
    result.selected = Backend::cuda;
    result.selected_available = true;
    result.fallback_reason = "CPU is unavailable";
    result.message = "CPU is unavailable: falling back to CUDA 13.2";
  } else {
    result.selected = Backend::cpu;
    result.fallback_reason = "No backend is available";
    result.message = "No backend is available";
  }
  return finish();
}

}  // namespace obs_whisperbleep::runtime
