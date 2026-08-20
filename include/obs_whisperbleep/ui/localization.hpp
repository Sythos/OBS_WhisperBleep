// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace obs_whisperbleep::ui {

inline constexpr std::string_view kDefaultLocale = "en-US";
inline constexpr std::string_view kEnglishLocale = "en-US";
inline constexpr std::string_view kItalianLocale = "it-IT";

// Translation identifiers are part of the UI contract. Keep these values
// stable when a visible label changes so persisted settings and future UI
// surfaces can share the same catalogs.
namespace keys {
inline constexpr std::string_view plugin_name = "plugin.name";
inline constexpr std::string_view plugin_description = "plugin.description";

inline constexpr std::string_view property_enabled = "property.enabled";
inline constexpr std::string_view property_debug = "property.debug";
inline constexpr std::string_view property_phrases = "property.phrases";
inline constexpr std::string_view property_model = "property.model";
inline constexpr std::string_view property_backend = "property.backend";
inline constexpr std::string_view property_replacement =
    "property.replacement";
inline constexpr std::string_view property_language = "property.language";
inline constexpr std::string_view language_english = "language.english";
inline constexpr std::string_view language_italian = "language.italian";

inline constexpr std::string_view backend_auto = "backend.auto";
inline constexpr std::string_view backend_cpu = "backend.cpu";
inline constexpr std::string_view backend_cuda = "backend.cuda";

inline constexpr std::string_view replacement_beep = "replacement.beep";
inline constexpr std::string_view replacement_duck = "replacement.duck";
inline constexpr std::string_view replacement_bark = "replacement.bark";
inline constexpr std::string_view replacement_custom = "replacement.custom";

inline constexpr std::string_view model_tiny = "model.tiny";
inline constexpr std::string_view model_base = "model.base";
inline constexpr std::string_view model_small = "model.small";
inline constexpr std::string_view model_medium = "model.medium";
inline constexpr std::string_view model_large = "model.large";
inline constexpr std::string_view model_turbo = "model.turbo";
inline constexpr std::string_view model_tiny_en = "model.tiny.en";
inline constexpr std::string_view model_base_en = "model.base.en";
inline constexpr std::string_view model_small_en = "model.small.en";
inline constexpr std::string_view model_medium_en = "model.medium.en";

inline constexpr std::string_view status_ready = "status.ready";
inline constexpr std::string_view status_processing = "status.processing";
inline constexpr std::string_view status_disabled = "status.disabled";
inline constexpr std::string_view status_unavailable = "status.unavailable";
inline constexpr std::string_view status_error = "status.error";

inline constexpr std::string_view menu_general = "menu.general";
inline constexpr std::string_view menu_audio = "menu.audio";
inline constexpr std::string_view menu_models = "menu.models";
inline constexpr std::string_view menu_matching = "menu.matching";
inline constexpr std::string_view menu_about = "menu.about";
inline constexpr std::string_view menu_general_description =
    "menu.general.description";
inline constexpr std::string_view menu_audio_description =
    "menu.audio.description";
inline constexpr std::string_view menu_models_description =
    "menu.models.description";
inline constexpr std::string_view menu_matching_description =
    "menu.matching.description";
inline constexpr std::string_view menu_about_description =
    "menu.about.description";
inline constexpr std::string_view action_check_updates =
    "action.check_updates";
inline constexpr std::string_view action_unknown = "action.unknown";

inline constexpr std::string_view update_available = "update.available";
inline constexpr std::string_view update_up_to_date = "update.up_to_date";
inline constexpr std::string_view update_network_error =
    "update.network_error";
inline constexpr std::string_view update_invalid_response =
    "update.invalid_response";
inline constexpr std::string_view update_invalid_installed =
    "update.invalid_installed";
inline constexpr std::string_view update_open_releases =
    "update.open_releases";
}  // namespace keys

/** A stable value/label pair for a locale selector. */
struct LocaleOption {
  std::string id;
  std::string label;
};

/**
 * Resolve a user or host locale to one of the locales shipped by the plugin.
 *
 * Locale matching is case-insensitive and accepts the short language forms
 * "en" and "it". Unknown, empty or malformed values intentionally resolve to
 * English so that settings can never make a property label disappear.
 */
[[nodiscard]] std::string_view resolve_locale(
    std::string_view requested_locale) noexcept;

/**
 * Translate a known UI key. Missing translations fall back to English, while
 * an unknown key is returned unchanged as a visible diagnostic.
 */
[[nodiscard]] std::string_view translate(std::string_view locale,
                                         std::string_view key) noexcept;

/** Return the deterministic locale list shown in the plugin options. */
[[nodiscard]] std::vector<LocaleOption> language_options(
    std::string_view locale = kDefaultLocale);

}  // namespace obs_whisperbleep::ui
