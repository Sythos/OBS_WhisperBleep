// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/model/model_catalog.hpp"

#include <algorithm>
#include <utility>

namespace obs_whisperbleep::model {

const char* model_id_name(ModelId id) noexcept {
  switch (id) {
    case ModelId::tiny:
      return "tiny";
    case ModelId::base:
      return "base";
    case ModelId::small:
      return "small";
    case ModelId::medium:
      return "medium";
    case ModelId::large:
      return "large";
    case ModelId::turbo:
      return "turbo";
  }
  return "unknown";
}

std::optional<ModelId> model_id_from_name(std::string_view name) {
  for (const auto id : {ModelId::tiny, ModelId::base, ModelId::small,
                        ModelId::medium, ModelId::large, ModelId::turbo}) {
    if (name == model_id_name(id)) {
      return id;
    }
  }
  return std::nullopt;
}

ModelCatalog::ModelCatalog(std::vector<ModelDescriptor> models,
                           ModelCatalogMetadata metadata)
    : models_(std::move(models)), metadata_(std::move(metadata)) {}

const std::vector<ModelDescriptor>& ModelCatalog::models() const noexcept {
  return models_;
}

const ModelCatalogMetadata& ModelCatalog::metadata() const noexcept {
  return metadata_;
}

const ModelDescriptor* ModelCatalog::find(ModelId id) const noexcept {
  const auto iterator = std::find_if(
      models_.begin(), models_.end(), [id](const ModelDescriptor& model) {
        return model.id == id;
      });
  return iterator == models_.end() ? nullptr : &*iterator;
}

bool ModelCatalog::empty() const noexcept { return models_.empty(); }

ModelCatalog default_catalog() {
  constexpr std::string_view kWhisperSource =
      "https://github.com/openai/whisper";
  constexpr std::string_view kWhisperManifest =
      "https://github.com/openai/whisper/blob/main/whisper/__init__.py";

  // URLs and checksums mirror the official _MODELS manifest in the upstream
  // Whisper main branch. Expected byte sizes remain unset because that
  // manifest publishes no verified size values.
  std::vector<ModelDescriptor> models{
      {ModelId::tiny,
       "tiny",
       "https://openaipublic.azureedge.net/main/whisper/models/65147644a518d12f04e32d6f3b26facc3f8dd46e5390956a9424a650c0ce22b9/tiny.pt",
       "65147644a518d12f04e32d6f3b26facc3f8dd46e5390956a9424a650c0ce22b9",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
      {ModelId::base,
       "base",
       "https://openaipublic.azureedge.net/main/whisper/models/ed3a0b6b1c0edf879ad9b11b1af5a0e6ab5db9205f891f668f8b0e6c6326e34e/base.pt",
       "ed3a0b6b1c0edf879ad9b11b1af5a0e6ab5db9205f891f668f8b0e6c6326e34e",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
      {ModelId::small,
       "small",
       "https://openaipublic.azureedge.net/main/whisper/models/9ecf779972d90ba49c06d968637d720dd632c55bbf19d441fb42bf17a411e794/small.pt",
       "9ecf779972d90ba49c06d968637d720dd632c55bbf19d441fb42bf17a411e794",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
      {ModelId::medium,
       "medium",
       "https://openaipublic.azureedge.net/main/whisper/models/345ae4da62f9b3d59415adc60127b97c714f32e89e936602e85993674d08dcb1/medium.pt",
       "345ae4da62f9b3d59415adc60127b97c714f32e89e936602e85993674d08dcb1",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
      {ModelId::large,
       "large",
       "https://openaipublic.azureedge.net/main/whisper/models/e5b1a55b89c1367dacf97e3e19bfd829a01529dbfdeefa8caeb59b3f1b81dadb/large-v3.pt",
       "e5b1a55b89c1367dacf97e3e19bfd829a01529dbfdeefa8caeb59b3f1b81dadb",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
      {ModelId::turbo,
       "turbo",
       "https://openaipublic.azureedge.net/main/whisper/models/aff26ae408abcba5fbf8813c21e62b0941638c5f6eebfb145be0c9839262a19a/large-v3-turbo.pt",
       "aff26ae408abcba5fbf8813c21e62b0941638c5f6eebfb145be0c9839262a19a",
       "MIT",
       0,
       std::nullopt, "openai-whisper-pytorch-checkpoint"},
  };

  return ModelCatalog(std::move(models),
                      {"20250625", std::string(kWhisperSource),
                       std::string(kWhisperManifest),
                       "OpenAI Whisper PyTorch checkpoint manifest"});
}

}  // namespace obs_whisperbleep::model
