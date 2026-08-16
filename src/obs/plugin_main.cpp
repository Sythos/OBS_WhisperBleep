// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/plugin_main.hpp"

namespace obs_whisperbleep::obs {

const char* plugin_description() noexcept {
  return "OBS WhisperBleep - audio censor filter";
}

bool plugin_load() noexcept { return true; }

}  // namespace obs_whisperbleep::obs
