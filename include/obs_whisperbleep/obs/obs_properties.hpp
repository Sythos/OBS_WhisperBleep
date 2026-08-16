// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>
#include <vector>

namespace obs_whisperbleep::obs {

struct Property {
  std::string key;
  std::string label;
};

[[nodiscard]] std::vector<Property> default_properties();

}  // namespace obs_whisperbleep::obs
