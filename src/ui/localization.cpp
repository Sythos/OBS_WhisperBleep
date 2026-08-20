// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/ui/localization.hpp"

#include <cctype>
#include <iterator>

namespace obs_whisperbleep::ui {
namespace {

struct LocaleInfo {
  std::string_view id;
  std::string_view english_label;
};

// Keep this list aligned with the locale identifiers shipped by OBS Studio.
// The plugin currently supplies an English source catalog and an Italian
// translation; every other supported OBS locale deliberately falls back to
// the English messages until its translation catalog is complete.
constexpr LocaleInfo kSupportedLocales[] = {
    {"en-US", "English (United States)"},
    {"af-ZA", "Afrikaans (South Africa)"},
    {"ar-SA", "Arabic (Saudi Arabia)"},
    {"az-AZ", "Azerbaijani (Azerbaijan)"},
    {"ba-RU", "Bashkir (Russia)"},
    {"be-BY", "Belarusian (Belarus)"},
    {"bg-BG", "Bulgarian (Bulgaria)"},
    {"bn-BD", "Bengali (Bangladesh)"},
    {"ca-ES", "Catalan (Spain)"},
    {"cs-CZ", "Czech (Czechia)"},
    {"da-DK", "Danish (Denmark)"},
    {"de-DE", "German (Germany)"},
    {"el-GR", "Greek (Greece)"},
    {"en-GB", "English (United Kingdom)"},
    {"es-ES", "Spanish (Spain)"},
    {"et-EE", "Estonian (Estonia)"},
    {"eu-ES", "Basque (Spain)"},
    {"fa-IR", "Persian (Iran)"},
    {"fi-FI", "Finnish (Finland)"},
    {"fil-PH", "Filipino (Philippines)"},
    {"fr-FR", "French (France)"},
    {"gd-GB", "Scottish Gaelic (United Kingdom)"},
    {"gl-ES", "Galician (Spain)"},
    {"he-IL", "Hebrew (Israel)"},
    {"hi-IN", "Hindi (India)"},
    {"hr-HR", "Croatian (Croatia)"},
    {"hu-HU", "Hungarian (Hungary)"},
    {"hy-AM", "Armenian (Armenia)"},
    {"id-ID", "Indonesian (Indonesia)"},
    {"is-IS", "Icelandic (Iceland)"},
    {"it-IT", "Italian (Italy)"},
    {"ja-JP", "Japanese (Japan)"},
    {"ka-GE", "Georgian (Georgia)"},
    {"kaa", "Karakalpak"},
    {"kab-KAB", "Kabyle"},
    {"kmr-TR", "Northern Kurdish (Turkey)"},
    {"ko-KR", "Korean (South Korea)"},
    {"lo-LA", "Lao (Laos)"},
    {"lt-LT", "Lithuanian (Lithuania)"},
    {"ms-MY", "Malay (Malaysia)"},
    {"nb-NO", "Norwegian Bokmal (Norway)"},
    {"nl-NL", "Dutch (Netherlands)"},
    {"nn-NO", "Norwegian Nynorsk (Norway)"},
    {"oc-FR", "Occitan (France)"},
    {"pl-PL", "Polish (Poland)"},
    {"pt-BR", "Portuguese (Brazil)"},
    {"pt-PT", "Portuguese (Portugal)"},
    {"ro-RO", "Romanian (Romania)"},
    {"ru-RU", "Russian (Russia)"},
    {"si-LK", "Sinhala (Sri Lanka)"},
    {"sk-SK", "Slovak (Slovakia)"},
    {"sl-SI", "Slovenian (Slovenia)"},
    {"sr-CS", "Serbian (Serbia and Montenegro)"},
    {"sr-SP", "Serbian (Serbia)"},
    {"sv-SE", "Swedish (Sweden)"},
    {"szl-PL", "Silesian (Poland)"},
    {"ta-IN", "Tamil (India)"},
    {"th-TH", "Thai (Thailand)"},
    {"tl-PH", "Tagalog (Philippines)"},
    {"tr-TR", "Turkish (Turkey)"},
    {"tt-RU", "Tatar (Russia)"},
    {"ug-CN", "Uyghur (China)"},
    {"uk-UA", "Ukrainian (Ukraine)"},
    {"ur-PK", "Urdu (Pakistan)"},
    {"vi-VN", "Vietnamese (Vietnam)"},
    {"zh-CN", "Chinese (Simplified)"},
    {"zh-TW", "Chinese (Traditional)"},
};

bool equals_ignore_case(const std::string_view left,
                        const std::string_view right) noexcept;

const LocaleInfo* find_locale(const std::string_view requested) noexcept {
  for (const auto& locale : kSupportedLocales) {
    if (equals_ignore_case(requested, locale.id)) {
      return &locale;
    }
  }
  return nullptr;
}

bool equals_ignore_case(const std::string_view left,
                        const std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index) {
    const auto left_char = static_cast<unsigned char>(left[index]);
    const auto right_char = static_cast<unsigned char>(right[index]);
    if (std::tolower(left_char) != std::tolower(right_char)) {
      return false;
    }
  }
  return true;
}

std::string_view translate_english(const std::string_view key) noexcept {
  if (key == keys::plugin_name) {
    return "WhisperBleep";
  }
  if (key == keys::plugin_description) {
    return "OBS WhisperBleep audio censor filter";
  }
  if (key == keys::property_enabled) {
    return "Enable WhisperBleep";
  }
  if (key == keys::property_debug) {
    return "Debug";
  }
  if (key == keys::property_phrases) {
    return "Words or phrases to censor";
  }
  if (key == keys::property_model) {
    return "Whisper model";
  }
  if (key == keys::property_backend) {
    return "Backend";
  }
  if (key == keys::property_replacement) {
    return "Replacement sound";
  }
  if (key == keys::property_language) {
    return "Language";
  }
  if (key == keys::language_english) {
    return "English (United States)";
  }
  if (key == keys::language_italian) {
    return "Italian (Italy)";
  }
  if (key == keys::backend_auto) {
    return "Auto";
  }
  if (key == keys::backend_cpu) {
    return "CPU";
  }
  if (key == keys::backend_cuda) {
    return "CUDA 13.2";
  }
  if (key == keys::replacement_beep) {
    return "Beep";
  }
  if (key == keys::replacement_duck) {
    return "Duck";
  }
  if (key == keys::replacement_bark) {
    return "Bark";
  }
  if (key == keys::replacement_custom) {
    return "Custom audio";
  }
  if (key == keys::model_tiny) {
    return "tiny";
  }
  if (key == keys::model_base) {
    return "base";
  }
  if (key == keys::model_small) {
    return "small";
  }
  if (key == keys::model_medium) {
    return "medium";
  }
  if (key == keys::model_large) {
    return "large";
  }
  if (key == keys::model_turbo) {
    return "turbo";
  }
  if (key == keys::model_tiny_en) {
    return "tiny.en";
  }
  if (key == keys::model_base_en) {
    return "base.en";
  }
  if (key == keys::model_small_en) {
    return "small.en";
  }
  if (key == keys::model_medium_en) {
    return "medium.en";
  }
  if (key == keys::status_ready) {
    return "Ready";
  }
  if (key == keys::status_processing) {
    return "Processing";
  }
  if (key == keys::status_disabled) {
    return "Disabled";
  }
  if (key == keys::status_unavailable) {
    return "Unavailable";
  }
  if (key == keys::status_error) {
    return "Error";
  }
  if (key == keys::menu_general) {
    return "General";
  }
  if (key == keys::menu_audio) {
    return "Audio";
  }
  if (key == keys::menu_models) {
    return "Models";
  }
  if (key == keys::menu_matching) {
    return "Matching";
  }
  if (key == keys::menu_about) {
    return "About";
  }
  if (key == keys::menu_general_description) {
    return "General plugin settings and runtime status.";
  }
  if (key == keys::menu_audio_description) {
    return "Replacement audio and synchronization settings.";
  }
  if (key == keys::menu_models_description) {
    return "Whisper model selection, download and cache status. When GPU is selected, VRAM must hold both the Whisper model and the OBS game or application. Avoid models that are too large; when a recent mid-range CPU is available, prefer CPU to leave VRAM for the game or application.";
  }
  if (key == keys::menu_matching_description) {
    return "Word and phrase matching settings.";
  }
  if (key == keys::menu_about_description) {
    return "Plugin information and release update actions.";
  }
  if (key == keys::action_check_updates) {
    return "Check Updates";
  }
  if (key == keys::action_unknown) {
    return "Unknown Action";
  }
  if (key == keys::update_available) {
    return "A newer GitHub release is available";
  }
  if (key == keys::update_up_to_date) {
    return "The installed version is current";
  }
  if (key == keys::update_network_error) {
    return "GitHub release check failed";
  }
  if (key == keys::update_invalid_response) {
    return "GitHub returned an invalid release response";
  }
  if (key == keys::update_invalid_installed) {
    return "The installed version is invalid";
  }
  if (key == keys::update_open_releases) {
    return "Open GitHub Releases";
  }
  return {};
}

std::string_view translate_italian(const std::string_view key) noexcept {
  if (key == keys::plugin_name) {
    return "WhisperBleep";
  }
  if (key == keys::plugin_description) {
    return "Filtro audio di censura OBS WhisperBleep";
  }
  if (key == keys::property_enabled) {
    return "Abilita WhisperBleep";
  }
  if (key == keys::property_debug) {
    return "Debug";
  }
  if (key == keys::property_phrases) {
    return "Parole o frasi da censurare";
  }
  if (key == keys::property_model) {
    return "Modello Whisper";
  }
  if (key == keys::property_backend) {
    return "Backend";
  }
  if (key == keys::property_replacement) {
    return "Suono di sostituzione";
  }
  if (key == keys::property_language) {
    return "Lingua";
  }
  if (key == keys::language_english) {
    return "Inglese (Stati Uniti)";
  }
  if (key == keys::language_italian) {
    return "Italiano (Italia)";
  }
  if (key == keys::backend_auto) {
    return "Automatico";
  }
  if (key == keys::backend_cpu) {
    return "CPU";
  }
  if (key == keys::backend_cuda) {
    return "CUDA 13.2";
  }
  if (key == keys::replacement_beep) {
    return "Beep";
  }
  if (key == keys::replacement_duck) {
    return "Anatra";
  }
  if (key == keys::replacement_bark) {
    return "Abbaio";
  }
  if (key == keys::replacement_custom) {
    return "Audio personalizzato";
  }
  if (key == keys::model_tiny) {
    return "tiny";
  }
  if (key == keys::model_base) {
    return "base";
  }
  if (key == keys::model_small) {
    return "small";
  }
  if (key == keys::model_medium) {
    return "medium";
  }
  if (key == keys::model_large) {
    return "large";
  }
  if (key == keys::model_turbo) {
    return "turbo";
  }
  if (key == keys::model_tiny_en) {
    return "tiny.en";
  }
  if (key == keys::model_base_en) {
    return "base.en";
  }
  if (key == keys::model_small_en) {
    return "small.en";
  }
  if (key == keys::model_medium_en) {
    return "medium.en";
  }
  if (key == keys::status_ready) {
    return "Pronto";
  }
  if (key == keys::status_processing) {
    return "Elaborazione";
  }
  if (key == keys::status_disabled) {
    return "Disabilitato";
  }
  if (key == keys::status_unavailable) {
    return "Non disponibile";
  }
  if (key == keys::status_error) {
    return "Errore";
  }
  if (key == keys::menu_general) {
    return "Generale";
  }
  if (key == keys::menu_audio) {
    return "Audio";
  }
  if (key == keys::menu_models) {
    return "Modelli";
  }
  if (key == keys::menu_matching) {
    return "Corrispondenze";
  }
  if (key == keys::menu_about) {
    return "Informazioni";
  }
  if (key == keys::menu_general_description) {
    return "Impostazioni generali del plugin e stato del runtime.";
  }
  if (key == keys::menu_audio_description) {
    return "Audio di sostituzione e impostazioni di sincronizzazione.";
  }
  if (key == keys::menu_models_description) {
    return "Selezione, download e stato della cache dei modelli Whisper. Con la GPU selezionata, la VRAM deve contenere sia il modello Whisper sia il gioco o l'applicazione gestiti da OBS. Evita modelli troppo grandi; se disponibile una CPU recente di fascia media, preferisci la CPU per lasciare la VRAM al gioco o all'applicazione.";
  }
  if (key == keys::menu_matching_description) {
    return "Impostazioni per la corrispondenza di parole e frasi.";
  }
  if (key == keys::menu_about_description) {
    return "Informazioni sul plugin e azioni per gli aggiornamenti delle release.";
  }
  if (key == keys::action_check_updates) {
    return "Controlla aggiornamenti";
  }
  if (key == keys::action_unknown) {
    return "Azione sconosciuta";
  }
  if (key == keys::update_available) {
    return "È disponibile una nuova release GitHub";
  }
  if (key == keys::update_up_to_date) {
    return "La versione installata è aggiornata";
  }
  if (key == keys::update_network_error) {
    return "Controllo della release GitHub non riuscito";
  }
  if (key == keys::update_invalid_response) {
    return "GitHub ha restituito una risposta release non valida";
  }
  if (key == keys::update_invalid_installed) {
    return "La versione installata non è valida";
  }
  if (key == keys::update_open_releases) {
    return "Apri le release GitHub";
  }
  return {};
}

std::string_view language_label(const std::string_view locale,
                                const LocaleInfo& info) noexcept {
  if (locale == kItalianLocale) {
    if (info.id == kEnglishLocale) {
      return translate_italian(keys::language_english);
    }
    if (info.id == kItalianLocale) {
      return translate_italian(keys::language_italian);
    }
  }
  return info.english_label;
}

}  // namespace

std::string_view resolve_locale(
    const std::string_view requested_locale) noexcept {
  if (const auto* exact = find_locale(requested_locale); exact != nullptr) {
    return exact->id;
  }

  const auto separator = requested_locale.find('-');
  const auto language = requested_locale.substr(0, separator);
  if (separator == std::string_view::npos && language.size() == 2) {
    for (const auto& locale : kSupportedLocales) {
      const auto locale_separator = locale.id.find('-');
      if (equals_ignore_case(language,
                             locale.id.substr(0, locale_separator))) {
        return locale.id;
      }
    }
  }

  return kEnglishLocale;
}

std::string_view translate(const std::string_view locale,
                           const std::string_view key) noexcept {
  const auto resolved_locale = resolve_locale(locale);
  const auto english_text = translate_english(key);
  if (resolved_locale == kItalianLocale) {
    const auto italian_text = translate_italian(key);
    if (!italian_text.empty()) {
      return italian_text;
    }
  }
  if (!english_text.empty()) {
    return english_text;
  }
  return key;
}

std::vector<LocaleOption> language_options(const std::string_view locale) {
  const auto resolved_locale = resolve_locale(locale);
  std::vector<LocaleOption> options;
  options.reserve(std::size(kSupportedLocales));
  for (const auto& info : kSupportedLocales) {
    options.push_back(
        {std::string(info.id), std::string(language_label(resolved_locale, info))});
  }
  return options;
}

}  // namespace obs_whisperbleep::ui
