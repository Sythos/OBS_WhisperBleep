// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <obs-module.h>
#include <obs.h>

#include <string>

#include "obs_whisperbleep/obs/obs_filter.hpp"

namespace {

using obs_whisperbleep::obs::FilterSettings;
using obs_whisperbleep::obs::ObsFilter;

struct NativeFilterData {
  obs_source_t* source{nullptr};
  ObsFilter filter;
};

const char* filter_name(void*) { return "WhisperBleep"; }

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
  result.phrases = setting_string(settings, "phrases", "");
  result.model = setting_string(settings, "model", "tiny");
  result.backend = setting_string(settings, "backend", "auto");
  result.replacement = setting_string(settings, "replacement", "beep");
  return result;
}

void* filter_create(obs_data_t* settings, obs_source_t* source) {
  auto* data = new NativeFilterData;
  data->source = source;
  data->filter.update(read_settings(settings));
  data->filter.load();
  return data;
}

void filter_destroy(void* opaque) {
  auto* data = static_cast<NativeFilterData*>(opaque);
  if (data == nullptr) {
    return;
  }
  data->filter.unload();
  delete data;
}

void filter_update(void* opaque, obs_data_t* settings) {
  auto* data = static_cast<NativeFilterData*>(opaque);
  if (data != nullptr) {
    data->filter.update(read_settings(settings));
  }
}

struct obs_audio_data* filter_audio(void*, struct obs_audio_data* audio) {
  // M1 deliberately leaves the audio untouched. Inference and replacement
  // belong to later milestones and must never run in this callback.
  return audio;
}

obs_properties_t* filter_properties(void*) {
  obs_properties_t* properties = obs_properties_create();
  obs_properties_add_bool(properties, "enabled", "Enable WhisperBleep");
  obs_properties_add_text(properties, "phrases", "Words or phrases to censor",
                           OBS_TEXT_MULTILINE);

  obs_property_t* model = obs_properties_add_list(
      properties, "model", "Whisper model", OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  for (const char* value : {"tiny", "base", "small", "medium", "large",
                            "turbo"}) {
    obs_property_list_add_string(model, value, value);
  }

  obs_property_t* backend = obs_properties_add_list(
      properties, "backend", "Backend", OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(backend, "Auto", "auto");
  obs_property_list_add_string(backend, "CPU", "cpu");
  obs_property_list_add_string(backend, "CUDA 13.2", "cuda");

  obs_property_t* replacement = obs_properties_add_list(
      properties, "replacement", "Replacement sound", OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(replacement, "Beep", "beep");
  obs_property_list_add_string(replacement, "Duck", "duck");
  obs_property_list_add_string(replacement, "Bark", "bark");
  obs_property_list_add_string(replacement, "Custom audio", "custom");
  return properties;
}

void filter_defaults(obs_data_t* settings) {
  obs_data_set_default_bool(settings, "enabled", true);
  obs_data_set_default_string(settings, "phrases", "");
  obs_data_set_default_string(settings, "model", "tiny");
  obs_data_set_default_string(settings, "backend", "auto");
  obs_data_set_default_string(settings, "replacement", "beep");
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
  return "OBS WhisperBleep audio censor filter";
}

struct obs_source_info obs_whisperbleep_filter = make_filter_info();

bool obs_module_load(void) {
  obs_register_source(&obs_whisperbleep_filter);
  return true;
}
