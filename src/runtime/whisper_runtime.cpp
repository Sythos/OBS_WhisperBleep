// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

#include <exception>
#include <utility>

namespace obs_whisperbleep::runtime {

const char* runtime_status_name(RuntimeStatus status) noexcept {
  switch (status) {
    case RuntimeStatus::ready:
      return "ready";
    case RuntimeStatus::unavailable:
      return "unavailable";
    case RuntimeStatus::error:
      return "error";
  }
  return "unknown";
}

RuntimeStatus StubWhisperRuntime::initialize(std::string_view model_path) {
  (void)model_path;
  return RuntimeStatus::unavailable;
}

std::vector<TranscriptSegment> StubWhisperRuntime::transcribe(
    const float* samples, std::size_t sample_count, std::uint32_t sample_rate) {
  (void)samples;
  (void)sample_count;
  (void)sample_rate;
  return {};
}

WhisperRuntimeFactory::WhisperRuntimeFactory(RuntimeAdapterFactory factory)
    : factory_(std::move(factory)) {}

RuntimeFactoryResult WhisperRuntimeFactory::create(
    const RuntimeRequest& request) const {
  RuntimeFactoryResult result;
  result.backend = select_backend(request.requested_backend,
                                  request.backend_probe);

  if (!result.backend.selected_available) {
    result.status = RuntimeStatus::unavailable;
    result.message = "Whisper runtime unavailable: " +
                     result.backend.message;
    return result;
  }

  if (!factory_) {
    result.status = RuntimeStatus::unavailable;
    result.message =
        "Whisper runtime unavailable: no runtime adapter was injected";
    return result;
  }

  std::unique_ptr<IWhisperRuntime> adapter;
  try {
    adapter = factory_(request, result.backend);
  } catch (const std::exception& error) {
    result.status = RuntimeStatus::error;
    result.message = std::string("Whisper runtime adapter creation failed: ") +
                     error.what();
    return result;
  } catch (...) {
    result.status = RuntimeStatus::error;
    result.message =
        "Whisper runtime adapter creation failed with an unknown error";
    return result;
  }

  if (!adapter) {
    result.status = RuntimeStatus::unavailable;
    result.message =
        "Whisper runtime unavailable: injected factory returned no adapter";
    return result;
  }

  RuntimeStatus initialization_status = RuntimeStatus::error;
  try {
    initialization_status = adapter->initialize(request.model_path);
  } catch (const std::exception& error) {
    result.status = RuntimeStatus::error;
    result.message = std::string("Whisper runtime initialization failed: ") +
                     error.what();
    return result;
  } catch (...) {
    result.status = RuntimeStatus::error;
    result.message =
        "Whisper runtime initialization failed with an unknown error";
    return result;
  }

  result.status = initialization_status;
  if (initialization_status == RuntimeStatus::ready) {
    result.runtime = std::move(adapter);
    result.message = "Whisper runtime adapter is ready";
    return result;
  }

  result.message = std::string("Whisper runtime adapter is ") +
                   runtime_status_name(initialization_status);
  return result;
}

}  // namespace obs_whisperbleep::runtime
