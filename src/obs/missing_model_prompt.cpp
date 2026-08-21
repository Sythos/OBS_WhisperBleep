// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "missing_model_prompt.hpp"

#include <obs.h>

#include <filesystem>
#include <new>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "obs_whisperbleep/platform/platform_info.hpp"

namespace obs_whisperbleep::obs {
namespace {

using OpenSourceProperties = void (*)(obs_source_t*);

struct PropertiesTask final {
  obs_source_t* source = nullptr;
};

[[nodiscard]] OpenSourceProperties resolve_frontend_opener() noexcept {
#if defined(_WIN32)
  HMODULE module = GetModuleHandleW(L"obs-frontend-api.dll");
  if (module == nullptr) {
    module = GetModuleHandleW(nullptr);
  }
  if (module == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<OpenSourceProperties>(
      GetProcAddress(module, "obs_frontend_open_source_properties"));
#else
  return reinterpret_cast<OpenSourceProperties>(dlsym(
      RTLD_DEFAULT, "obs_frontend_open_source_properties"));
#endif
}

void open_properties_task(void* opaque) noexcept {
  auto* task = static_cast<PropertiesTask*>(opaque);
  if (task == nullptr) {
    return;
  }

  if (task->source != nullptr) {
    if (const auto opener = resolve_frontend_opener(); opener != nullptr) {
      opener(task->source);
    }
    obs_source_release(task->source);
  }
  delete task;
}

}  // namespace

std::filesystem::path model_cache_path(const std::string_view model_name) {
  if (model_name.empty()) {
    return {};
  }
  const auto root = platform::user_cache_directory("Sythos/OBS-WhisperBleep");
  if (root.empty()) {
    return {};
  }
  return root / (std::string(model_name) + ".model");
}

bool model_cache_file_present(const std::string_view model_name) noexcept {
  try {
    const auto path = model_cache_path(model_name);
    if (path.empty()) {
      return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      return false;
    }
    const auto size = std::filesystem::file_size(path, error);
    return !error && size != 0;
  } catch (...) {
    return false;
  }
}

void request_model_selection(obs_source* source) noexcept {
  if (source == nullptr) {
    return;
  }

  auto* retained = obs_source_get_ref(source);
  if (retained == nullptr) {
    return;
  }

  auto* task = new (std::nothrow) PropertiesTask{retained};
  if (task == nullptr) {
    obs_source_release(retained);
    return;
  }

  obs_queue_task(OBS_TASK_UI, open_properties_task, task, false);
}

}  // namespace obs_whisperbleep::obs
