// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/obs/obs_filter.hpp"
#include "obs_whisperbleep/obs/obs_properties.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "obs test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool same_frame(const obs_whisperbleep::core::AudioFrame& left,
                const obs_whisperbleep::core::AudioFrame& right) {
  return left.first_frame == right.first_frame &&
         left.audio.sample_rate == right.audio.sample_rate &&
         left.audio.channels == right.audio.channels &&
         left.audio.samples == right.audio.samples;
}

bool has_property(const std::vector<obs_whisperbleep::obs::Property>& properties,
                 const std::string& key) {
  return std::any_of(properties.begin(), properties.end(),
                     [&key](const auto& property) {
                       return property.key == key;
                     });
}

}  // namespace

int main() {
  using namespace obs_whisperbleep;
  using obs::FilterSettings;
  using obs::ObsFilter;

  const core::AudioFrame input{
      123, core::AudioBuffer{48000, 1, {1.F, 1.F, 1.F, 1.F, 1.F, 1.F}}};
  const core::AudioBuffer replacement{48000, 1, {0.25F, 0.5F}};
  const std::vector<core::CensorInterval> intervals{{1, 4}};

  ObsFilter filter;
  expect(!filter.loaded(), "starts unloaded");
  expect(filter.settings().enabled && filter.settings().model == "tiny" &&
             filter.settings().backend == "auto" &&
             filter.settings().replacement == "beep",
         "starts with default settings");
  expect(same_frame(filter.process(input, intervals, replacement), input),
         "passes through while unloaded");

  expect(filter.load(), "loads successfully");
  expect(filter.loaded(), "reports loaded state");
  filter.unload();
  expect(!filter.loaded(), "reports unloaded state");

  FilterSettings settings;
  settings.enabled = true;
  settings.phrases = "spoiler words";
  settings.model = "small";
  settings.backend = "cpu";
  settings.replacement = "custom-beep";
  filter.update(settings);
  expect(filter.settings().enabled && filter.settings().phrases == "spoiler words" &&
             filter.settings().model == "small" &&
             filter.settings().backend == "cpu" &&
             filter.settings().replacement == "custom-beep",
         "persists updated settings");
  filter.load();
  filter.unload();
  expect(filter.settings().phrases == "spoiler words" &&
             filter.settings().model == "small",
         "keeps settings across lifecycle changes");

  filter.load();
  settings.enabled = false;
  filter.update(settings);
  expect(same_frame(filter.process(input, intervals, replacement), input),
         "passes through while disabled");

  settings.enabled = true;
  filter.update(settings);
  expect(same_frame(filter.process(input, {}, replacement), input),
         "passes through when there are no intervals");

  const auto processed = filter.process(input, intervals, replacement);
  const auto processed_again = filter.process(input, intervals, replacement);
  const core::AudioFrame expected{
      123, core::AudioBuffer{48000, 1, {1.F, 0.25F, 0.5F, 0.25F, 1.F, 1.F}}};
  expect(same_frame(processed, expected),
         "replaces scheduled frames while loaded and enabled");
  expect(same_frame(processed_again, processed),
         "processing is deterministic");

  const auto properties = obs::default_properties();
  expect(properties.size() == 5, "exposes five default properties");
  expect(has_property(properties, "enabled"), "exposes enabled property");
  expect(has_property(properties, "phrases"), "exposes phrases property");
  expect(has_property(properties, "model"), "exposes model property");
  expect(has_property(properties, "backend"), "exposes backend property");
  expect(has_property(properties, "replacement"),
         "exposes replacement property");

  return EXIT_SUCCESS;
}
