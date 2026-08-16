// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/obs_properties.hpp"

namespace obs_whisperbleep::obs {

std::vector<Property> default_properties() {
  return {{"enabled", "Enable WhisperBleep"},
          {"phrases", "Words or phrases to censor"},
          {"model", "Whisper model"},
          {"backend", "Backend"},
          {"replacement", "Replacement sound"}};
}

}  // namespace obs_whisperbleep::obs
