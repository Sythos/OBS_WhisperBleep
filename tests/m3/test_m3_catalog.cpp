// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "obs_whisperbleep/model/model_catalog.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M3 catalog test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;

  const auto catalog = default_catalog();
  expect(catalog.models().size() == 10,
         "catalog contains six multilingual and four English-only model ids");
  expect(catalog.metadata().version == "20250625",
         "catalog records the upstream version");
  expect(catalog.metadata().source_url == "https://github.com/openai/whisper",
         "catalog records the upstream source");
  expect(catalog.metadata().manifest_url.find("__init__.py") !=
             std::string::npos,
         "catalog records the upstream manifest");
  expect(!catalog.metadata().compatibility.empty(),
         "catalog records format compatibility metadata");

  for (const auto id : {ModelId::tiny, ModelId::base, ModelId::small,
                        ModelId::medium, ModelId::large, ModelId::turbo,
                        ModelId::tiny_en, ModelId::base_en, ModelId::small_en,
                        ModelId::medium_en}) {
    const auto* model = catalog.find(id);
    expect(model != nullptr, "catalog finds every required model");
    expect(model->license == "MIT", "catalog records the model license");
    expect(model->source_url.rfind("https://openaipublic.azureedge.net/", 0) ==
               0,
           "catalog uses the official model host");
    expect(model->sha256.size() == 64, "catalog records a SHA-256 checksum");
    expect(!model->expected_size_bytes.has_value(),
           "catalog does not invent an unverified byte size");
    expect(model->format == "openai-whisper-pytorch-checkpoint",
           "catalog records the required model format");
    if (id == ModelId::tiny || id == ModelId::base || id == ModelId::small ||
        id == ModelId::medium || id == ModelId::large || id == ModelId::turbo) {
      expect(!model->english_only,
             "multilingual models are not marked English-only");
    }
  }

  struct ExpectedEnglishModel {
    ModelId id;
    const char* name;
    const char* sha256;
  };

  for (const auto& expected : {
           ExpectedEnglishModel{
               ModelId::tiny_en,
               "tiny.en",
               "d3dd57d32accea0b295c96e26691aa14d8822fac7d9d27d5dc00b4ca2826dd03"},
           ExpectedEnglishModel{
               ModelId::base_en,
               "base.en",
               "25a8566e1d0c1e2231d1c762132cd20e0f96a85d16145c3a00adf5d1ac670ead"},
           ExpectedEnglishModel{
               ModelId::small_en,
               "small.en",
               "f953ad0fd29cacd07d5a9eda5624af0f6bcf2258be67c92b79389873d91e0872"},
           ExpectedEnglishModel{
               ModelId::medium_en,
               "medium.en",
               "d7440d1dc186f76616474e0ff0b3b6b879abc9d1a4926b7adfa41db2d497ab4f"},
       }) {
    const auto* model = catalog.find(expected.id);
    expect(model != nullptr, "catalog finds every English-only model");
    expect(std::string(model_id_name(expected.id)) == expected.name,
           "selector exposes the canonical OpenAI model name");
    expect(model->name == expected.name,
           "catalog records the canonical English-only model name");
    expect(model->english_only,
           "catalog marks English-only models explicitly");
    expect(model->sha256 == expected.sha256,
           "catalog records the published English-only SHA-256 checksum");
    expect(model->source_url.find(std::string("/") + expected.name + ".pt") !=
               std::string::npos,
           "catalog points to the matching official English-only checkpoint");
  }

  for (const auto name : {"tiny.en", "base.en", "small.en", "medium.en"}) {
    expect(model_id_from_name(name).has_value(),
           "selector parses every canonical English-only model name");
  }

  return EXIT_SUCCESS;
}
