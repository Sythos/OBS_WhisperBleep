// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstdint>
#include <vector>

namespace obs_whisperbleep::core {

struct CensorInterval {
  std::int64_t start_frame = 0;
  std::int64_t end_frame = 0;
};

/** M0 policy: invalid intervals are discarded and overlapping intervals merge. */
class CensorScheduler {
 public:
  [[nodiscard]] static std::vector<CensorInterval> merge(
      const std::vector<CensorInterval>& intervals);
};

}  // namespace obs_whisperbleep::core
