// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <string_view>

struct obs_source;

namespace obs_whisperbleep::obs {

/** Returns the user-cache path used for a selected model. */
[[nodiscard]] std::filesystem::path model_cache_path(
    std::string_view model_name);

/** Checks presence only; checksum verification remains a worker operation. */
[[nodiscard]] bool model_cache_file_present(std::string_view model_name) noexcept;

/** Queues the OBS frontend properties window on the UI task queue. */
void request_model_selection(obs_source* source) noexcept;

}  // namespace obs_whisperbleep::obs
