// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <obs-module.h>
#include <obs.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "obs_whisperbleep/diagnostics/debug_log.hpp"
#include "obs_whisperbleep/model/model_catalog.hpp"
#include "obs_whisperbleep/obs/native_audio_bridge.hpp"
#include "obs_whisperbleep/obs/obs_filter.hpp"
#include "obs_whisperbleep/ui/localization.hpp"

namespace {

using obs_whisperbleep::obs::FilterSettings;
using obs_whisperbleep::obs::ObsFilter;
using obs_whisperbleep::diagnostics::DebugLog;

struct NativeFilterData {
  obs_source_t* source{nullptr};
  ObsFilter filter;
  DebugLog debug_log;
  std::unique_ptr<NativeAudioBridge> audio_bridge;
};

const char* filter_name(void*) {
  return obs_whisperbleep::ui::translate(
             obs_whisperbleep::ui::kDefaultLocale,
             obs_whisperbleep::ui::keys::plugin_name)
      .data();
}

std::filesystem::path user_home_directory() {
#if defined(_WIN32)
  const char* value = std::getenv("USERPROFILE");
#else
  const char* value = std::getenv("HOME");
#endif
  return value == nullptr || value[0] == '\0'
             ? std::filesystem::path{}
             : std::filesystem::path(value);
}

void configure_debug_log(NativeFilterData& data, const bool enabled) {
  if (!enabled) {
    data.debug_log = DebugLog{};
    return;
  }

  data.debug_log = DebugLog::open(
      {.enabled = true, .user_home = user_home_directory(), .date_yyyymmdd = {}});
  if (data.debug_log.ready()) {
    data.debug_log.write_line("filter", "debug logging enabled");
  }
}

std::uint32_t configured_sample_rate() {
  struct obs_audio_info audio_info{};
  return obs_get_audio_info(&audio_info) && audio_info.samples_per_sec != 0
             ? audio_info.samples_per_sec
             : 48000;
}

std::uint16_t configured_channels() {
  struct obs_audio_info audio_info{};
  if (obs_get_audio_info(&audio_info)) {
    const auto channels = get_audio_channels(audio_info.speakers);
    if (channels != 0 && channels <= MAX_AV_PLANES) {
      return static_cast<std::uint16_t>(channels);
    }
  }
  auto* audio = obs_get_audio();
  if (audio == nullptr) {
    return 1;
  }
  const auto channels = audio_output_get_channels(audio);
  return channels == 0 || channels > MAX_AV_PLANES
             ? 1
             : static_cast<std::uint16_t>(channels);
}

std::string setting_string(obs_data_t* settings, const char* key,
                           const char* fallback) {
  if (settings == nullptr) {
    return fallback;
  }
  const char* value = obs_data_get_string(settings, key);
  return value == nullptr || value[0] == '\0' ? fallback : value;
}

FilterSettings read_settings(obs_data_t* settings) {
  FilterSettings result;
  if (settings == nullptr) {
    return result;
  }

  result.enabled = obs_data_get_bool(settings, "enabled");
  result.debug = obs_data_get_bool(settings, "debug");
  result.phrases = setting_string(settings, "phrases", "");
  result.model = setting_string(settings, "model", "tiny");
  result.backend = setting_string(settings, "backend", "auto");
  result.replacement = setting_string(settings, "replacement", "beep");
  result.language = setting_string(settings, "language", "en-US");
  return result;
}

void* filter_create(obs_data_t* settings, obs_source_t* source) {
  auto* data = new NativeFilterData;
  data->source = source;
  const auto filter_settings = read_settings(settings);
  data->filter.update(filter_settings);
  configure_debug_log(*data, filter_settings.debug);
  data->filter.load();
  data->audio_bridge = std::make_unique<NativeAudioBridge>(
      filter_settings, configured_sample_rate(), configured_channels(),
      make_default_native_runtime());
  if (filter_settings.enabled && !data->audio_bridge->start() &&
      data->debug_log.ready()) {
    data->debug_log.write_line("filter", "audio pipeline did not start");
  }
  return data;
}

void filter_destroy(void* opaque) {
  auto* data = static_cast<NativeFilterData*>(opaque);
  if (data == nullptr) {
    return;
  }
  if (data->debug_log.ready()) {
    data->debug_log.write_line("filter", "destroyed");
  }
  if (data->audio_bridge != nullptr) {
    data->audio_bridge->stop();
  }
  data->filter.unload();
  delete data;
}

void filter_update(void* opaque, obs_data_t* settings) {
  auto* data = static_cast<NativeFilterData*>(opaque);
  if (data != nullptr) {
    const auto filter_settings = read_settings(settings);
    const bool debug_changed =
        filter_settings.debug != data->filter.settings().debug;
    data->filter.update(filter_settings);
    if (data->audio_bridge != nullptr) {
      data->audio_bridge->update(filter_settings);
    }
    if (debug_changed) {
      configure_debug_log(*data, filter_settings.debug);
    } else if (filter_settings.debug && data->debug_log.ready()) {
      data->debug_log.write_line("filter", "settings updated");
    }
  }
}

struct obs_audio_data* filter_audio(void* opaque, struct obs_audio_data* audio) {
  auto* data = static_cast<NativeFilterData*>(opaque);
  if (data == nullptr || data->audio_bridge == nullptr) {
    return audio;
  }
  return data->audio_bridge->filter_audio(audio);
}

obs_properties_t* filter_properties(void* opaque) {
  const auto* data = static_cast<const NativeFilterData*>(opaque);
  const auto locale = data == nullptr
                          ? obs_whisperbleep::ui::kDefaultLocale
                          : obs_whisperbleep::ui::resolve_locale(
                                data->filter.settings().language);
  const auto label = [locale](const std::string_view key) {
    return obs_whisperbleep::ui::translate(locale, key);
  };
  const auto localized_model_label = [locale](const std::string& model_name) {
    const std::string key = "model." + model_name;
    const auto translated = obs_whisperbleep::ui::translate(locale, key);
    return translated == key ? model_name : std::string(translated);
  };

  obs_properties_t* properties = obs_properties_create();
  const auto enabled_label =
      label(obs_whisperbleep::ui::keys::property_enabled);
  obs_properties_add_bool(properties, "enabled", enabled_label.data());
  const auto debug_label = label(obs_whisperbleep::ui::keys::property_debug);
  obs_properties_add_bool(properties, "debug", debug_label.data());
  const auto phrases_label =
      label(obs_whisperbleep::ui::keys::property_phrases);
  obs_properties_add_text(properties, "phrases", phrases_label.data(),
                           OBS_TEXT_MULTILINE);

  const auto model_property_label =
      label(obs_whisperbleep::ui::keys::property_model);
  obs_property_t* model = obs_properties_add_list(
      properties, "model", model_property_label.data(), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  const auto model_catalog = obs_whisperbleep::model::default_catalog();
  for (const auto& descriptor : model_catalog.models()) {
    const auto display_name = localized_model_label(descriptor.name);
    obs_property_list_add_string(model, display_name.c_str(),
                                 descriptor.name.c_str());
  }

  const auto backend_label =
      label(obs_whisperbleep::ui::keys::property_backend);
  obs_property_t* backend = obs_properties_add_list(
      properties, "backend", backend_label.data(), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  const auto backend_auto_label =
      label(obs_whisperbleep::ui::keys::backend_auto);
  const auto backend_cpu_label =
      label(obs_whisperbleep::ui::keys::backend_cpu);
  const auto backend_cuda_label =
      label(obs_whisperbleep::ui::keys::backend_cuda);
  obs_property_list_add_string(backend, backend_auto_label.data(), "auto");
  obs_property_list_add_string(backend, backend_cpu_label.data(), "cpu");
  obs_property_list_add_string(backend, backend_cuda_label.data(), "cuda");

  const auto replacement_label =
      label(obs_whisperbleep::ui::keys::property_replacement);
  obs_property_t* replacement = obs_properties_add_list(
      properties, "replacement", replacement_label.data(), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  const auto replacement_beep_label =
      label(obs_whisperbleep::ui::keys::replacement_beep);
  const auto replacement_duck_label =
      label(obs_whisperbleep::ui::keys::replacement_duck);
  const auto replacement_bark_label =
      label(obs_whisperbleep::ui::keys::replacement_bark);
  const auto replacement_custom_label =
      label(obs_whisperbleep::ui::keys::replacement_custom);
  obs_property_list_add_string(replacement, replacement_beep_label.data(),
                               "beep");
  obs_property_list_add_string(replacement, replacement_duck_label.data(),
                               "duck");
  obs_property_list_add_string(replacement, replacement_bark_label.data(),
                               "bark");
  obs_property_list_add_string(replacement, replacement_custom_label.data(),
                               "custom");

  const auto language_label =
      label(obs_whisperbleep::ui::keys::property_language);
  obs_property_t* language = obs_properties_add_list(
      properties, "language", language_label.data(), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  for (const auto& option : obs_whisperbleep::ui::language_options(locale)) {
    obs_property_list_add_string(language, option.label.c_str(),
                                 option.id.c_str());
  }
  return properties;
}

void filter_defaults(obs_data_t* settings) {
  obs_data_set_default_bool(settings, "enabled", true);
  obs_data_set_default_bool(settings, "debug", false);
  obs_data_set_default_string(settings, "phrases", "");
  obs_data_set_default_string(settings, "model", "tiny");
  obs_data_set_default_string(settings, "backend", "auto");
  obs_data_set_default_string(settings, "replacement", "beep");
  obs_data_set_default_string(settings, "language", "en-US");
}

struct obs_source_info make_filter_info() {
  struct obs_source_info info{};
  info.id = "obs_whisperbleep_filter";
  info.type = OBS_SOURCE_TYPE_FILTER;
  info.output_flags = OBS_SOURCE_AUDIO;
  info.get_name = filter_name;
  info.create = filter_create;
  info.destroy = filter_destroy;
  info.update = filter_update;
  info.filter_audio = filter_audio;
  info.get_properties = filter_properties;
  info.get_defaults = filter_defaults;
  return info;
}

}  // namespace

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-whisperbleep", "en-US")

MODULE_EXPORT const char* obs_module_description(void) {
  return obs_whisperbleep::ui::translate(
             obs_whisperbleep::ui::kDefaultLocale,
             obs_whisperbleep::ui::keys::plugin_description)
      .data();
}

struct obs_source_info obs_whisperbleep_filter = make_filter_info();

bool obs_module_load(void) {
  obs_register_source(&obs_whisperbleep_filter);
  return true;
}
