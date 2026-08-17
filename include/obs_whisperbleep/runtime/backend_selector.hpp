// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

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

enum class BackendProbeStatus { available, unavailable, not_probed };

/**
 * Evidence returned by a host/platform capability probe.
 *
 * The core does not probe CUDA or a driver itself. A host adapter can inject a
 * probe and report the provider, runtime version and device it actually
 * validated. This keeps platform SDKs and accelerator dependencies outside the
 * portable core library.
 */
struct BackendCapabilityDetail {
  Backend backend = Backend::cpu;
  BackendProbeStatus status = BackendProbeStatus::not_probed;
  std::string provider;
  std::string runtime_version;
  std::string device_name;
  std::string reason;
};

struct BackendProbeResult {
  BackendCapabilities capabilities{};
  std::string platform;
  std::vector<BackendCapabilityDetail> details;
};

using BackendCapabilityProbe =
    std::function<BackendCapabilityDetail(Backend backend)>;

struct BackendSelection {
  Backend requested = Backend::auto_select;
  Backend selected = Backend::cpu;
  bool requested_available = false;
  bool selected_available = false;
  bool used_fallback = false;
  std::string fallback_reason;
  std::string message;
  BackendCapabilityDetail requested_detail;
  BackendCapabilityDetail selected_detail;
};

[[nodiscard]] BackendSelection select_backend(
    Backend requested, const BackendCapabilities& capabilities);
[[nodiscard]] BackendSelection select_backend(
    Backend requested, const BackendProbeResult& probe_result);
[[nodiscard]] BackendProbeResult probe_backend_capabilities(
    const BackendCapabilityProbe& probe, std::string_view platform = {});
[[nodiscard]] BackendCapabilityDetail backend_capability_detail(
    const BackendProbeResult& probe_result, Backend backend);
[[nodiscard]] const char* backend_probe_status_name(
    BackendProbeStatus status) noexcept;
[[nodiscard]] const char* backend_name(Backend backend) noexcept;

}  // namespace obs_whisperbleep::runtime
