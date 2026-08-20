// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <string>
#include <vector>

#include "obs_whisperbleep/ui/localization.hpp"

namespace obs_whisperbleep::obs {

enum class PropertyType { boolean, text, select };

struct PropertyOption {
  std::string value;
  std::string label;
};

struct Property {
  std::string key;
  std::string label;
  PropertyType type = PropertyType::text;
  std::vector<PropertyOption> options;
};

[[nodiscard]] std::vector<Property> default_properties(
    std::string_view locale = ui::kDefaultLocale);

}  // namespace obs_whisperbleep::obs
