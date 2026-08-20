// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/obs/obs_properties.hpp"
#include "obs_whisperbleep/ui/localization.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M6 i18n test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

const obs_whisperbleep::obs::Property* find_property(
    const std::vector<obs_whisperbleep::obs::Property>& properties,
    const std::string& key) {
  for (const auto& property : properties) {
    if (property.key == key) {
      return &property;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  using namespace obs_whisperbleep;

  expect(ui::resolve_locale("") == ui::kEnglishLocale,
         "empty locale falls back to English");
  expect(ui::resolve_locale("en") == ui::kEnglishLocale,
         "short English locale resolves to English");
  expect(ui::resolve_locale("IT-it") == ui::kItalianLocale,
         "locale matching is case-insensitive");
  expect(ui::resolve_locale("fr-FR") == "fr-FR",
         "OBS-supported locales resolve to their canonical identifier");
  expect(ui::resolve_locale("xx-YY") == ui::kEnglishLocale,
         "unknown locales fall back to English");

  expect(ui::translate("en-US", "property.enabled") ==
             "Enable WhisperBleep",
         "English property label is stable");
  expect(ui::translate("it-IT", "property.enabled") ==
             "Abilita WhisperBleep",
         "Italian property label is available");
  expect(ui::translate("fr-FR", "property.enabled") ==
             "Enable WhisperBleep",
         "missing locale uses the English translation");
  expect(ui::translate("it-IT", "property.unknown") == "property.unknown",
         "unknown translation keys remain visible diagnostics");

  const auto english_options = ui::language_options();
  expect(english_options.size() == 67,
         "the language selector exposes the OBS-supported locale set");
  expect(english_options[0].id == "en-US" &&
             english_options[0].label == "English (United States)",
         "English option is first and localized in English");
  bool has_italian = false;
  bool has_french = false;
  bool has_traditional_chinese = false;
  for (const auto& option : english_options) {
    has_italian = has_italian || option.id == "it-IT";
    has_french = has_french || option.id == "fr-FR";
    has_traditional_chinese = has_traditional_chinese || option.id == "zh-TW";
  }
  expect(has_italian && has_french && has_traditional_chinese,
         "the selector includes representative OBS locales");

  const auto italian_options = ui::language_options("it-IT");
  std::string italian_label;
  for (const auto& option : italian_options) {
    if (option.id == "it-IT") {
      italian_label = option.label;
      break;
    }
  }
  expect(italian_options[0].label == "Inglese (Stati Uniti)" &&
             italian_label == "Italiano (Italia)",
         "language option labels follow the selected locale");

  const auto default_properties = obs::default_properties();
  expect(default_properties.size() == 7,
         "the stub properties contract includes debug and language selectors");
  const auto* debug = find_property(default_properties, "debug");
  expect(debug != nullptr && debug->type == obs::PropertyType::boolean &&
             debug->label == "Debug",
         "debug is an English boolean property and is off by default");
  const auto* models = find_property(default_properties, "model");
  const auto* backends = find_property(default_properties, "backend");
  const auto* replacements = find_property(default_properties, "replacement");
  expect(models != nullptr && models->options.size() == 10 &&
             models->options.front().value == "tiny" &&
             models->options.back().value == "medium.en",
         "the model property exposes multilingual and English-only models");
  expect(backends != nullptr && backends->options.size() == 3 &&
             backends->options[0].label == "Auto" &&
             backends->options[2].label == "CUDA 13.2",
         "the backend property exposes localized stable options");
  expect(replacements != nullptr && replacements->options.size() == 4 &&
             replacements->options[0].value == "beep" &&
             replacements->options[0].label == "Beep" &&
             replacements->options.back().value == "custom",
         "the replacement property exposes stable options");
  const auto* language = find_property(default_properties, "language");
  expect(language != nullptr && language->type == obs::PropertyType::select,
         "language is represented as a select property");
  expect(language->options.size() == 67 &&
             language->options[0].value == "en-US",
         "the stub language options preserve stable locale values");

  const auto italian_properties = obs::default_properties("it-IT");
  const auto* italian_enabled = find_property(italian_properties, "enabled");
  const auto* italian_backends = find_property(italian_properties, "backend");
  const auto* italian_replacements =
      find_property(italian_properties, "replacement");
  const auto* italian_language = find_property(italian_properties, "language");
  expect(italian_enabled != nullptr &&
             italian_enabled->label == "Abilita WhisperBleep",
         "the stub properties contract exposes localized labels");
  expect(italian_language != nullptr && italian_language->label == "Lingua" &&
             italian_language->options.size() == 67 &&
             italian_language->options[0].label == "Inglese (Stati Uniti)",
         "the language property localizes its own label and options");
  expect(italian_backends != nullptr && italian_backends->options[0].label ==
             "Automatico",
         "the backend options use the Italian catalog");
  expect(italian_replacements != nullptr &&
             italian_replacements->options[1].label == "Anatra" &&
             italian_replacements->options[3].label == "Audio personalizzato",
         "the replacement options use the Italian catalog");

  expect(ui::translate("it-IT", ui::keys::status_ready) == "Pronto" &&
             ui::translate("it-IT", ui::keys::menu_about) == "Informazioni" &&
             ui::translate("it-IT", ui::keys::action_check_updates) ==
                 "Controlla aggiornamenti" &&
             ui::translate("it-IT", ui::keys::update_available) ==
                 "È disponibile una nuova release GitHub",
         "status, menu and update keys are translated in Italian");
  expect(ui::translate("de-DE", ui::keys::backend_cuda) == "CUDA 13.2" &&
             ui::translate("de-DE", ui::keys::replacement_custom) ==
                 "Custom audio" &&
             ui::translate("de-DE", ui::keys::update_open_releases) ==
                 "Open GitHub Releases",
         "missing locale catalogs fall back to English for all UI groups");

  return EXIT_SUCCESS;
}
