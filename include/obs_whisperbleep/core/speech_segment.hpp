// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>

namespace obs_whisperbleep::core {

/** Whisper-independent transcript segment used by the M2 test pipeline. */
struct SpeechSegment {
  double start_seconds = 0.0;
  double end_seconds = 0.0;
  std::string text;
  float confidence = 0.0F;
};

}  // namespace obs_whisperbleep::core
