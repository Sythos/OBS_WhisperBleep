// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/obs/obs_properties.hpp"

#include "obs_whisperbleep/model/model_catalog.hpp"
#include "obs_whisperbleep/replacement/replacement_catalog.hpp"

#include <utility>

namespace obs_whisperbleep::obs {

std::vector<Property> default_properties(const std::string_view locale) {
  const auto resolved_locale = ui::resolve_locale(locale);
  const auto translated = [resolved_locale](const std::string_view key) {
    const auto value = ui::translate(resolved_locale, key);
    return std::string(value);
  };
  const auto translated_dynamic =
      [resolved_locale](const std::string& key) {
        const auto value = ui::translate(resolved_locale, key);
        return std::string(value);
  };

  std::vector<Property> properties;
  properties.push_back({"enabled", translated(ui::keys::property_enabled),
                        PropertyType::boolean, {}});
  properties.push_back({"debug", translated(ui::keys::property_debug),
                        PropertyType::boolean, {}});
  properties.push_back({"phrases", translated(ui::keys::property_phrases),
                        PropertyType::text, {}});
  std::vector<PropertyOption> model_options;
  const auto model_catalog = model::default_catalog();
  for (const auto& descriptor : model_catalog.models()) {
    const std::string key = "model." + descriptor.name;
    model_options.push_back(
        {descriptor.name, translated_dynamic(key)});
  }
  properties.push_back({"model", translated(ui::keys::property_model),
                        PropertyType::select, std::move(model_options)});

  const std::vector<std::pair<std::string_view, std::string_view>> backends{
      {"auto", ui::keys::backend_auto},
      {"cpu", ui::keys::backend_cpu},
      {"cuda", ui::keys::backend_cuda},
  };
  std::vector<PropertyOption> backend_options;
  for (const auto& [value, key] : backends) {
    backend_options.push_back({std::string(value), translated(key)});
  }
  properties.push_back({"backend", translated(ui::keys::property_backend),
                        PropertyType::select, std::move(backend_options)});

  std::vector<PropertyOption> replacement_options;
  for (const auto& option : replacement::ReplacementCatalog::options()) {
    const std::string key = "replacement." + option.id;
    replacement_options.push_back({option.id, translated_dynamic(key)});
  }
  properties.push_back(
      {"replacement", translated(ui::keys::property_replacement),
       PropertyType::select, std::move(replacement_options)});

  std::vector<PropertyOption> language_options;
  for (const auto& option : ui::language_options(resolved_locale)) {
    language_options.push_back({option.id, option.label});
  }
  properties.push_back({"language", translated(ui::keys::property_language),
                        PropertyType::select, std::move(language_options)});
  return properties;
}

}  // namespace obs_whisperbleep::obs
