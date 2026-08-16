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

ModelCatalog::ModelCatalog(std::vector<ModelDescriptor> models)
    : models_(std::move(models)) {}

const std::vector<ModelDescriptor>& ModelCatalog::models() const noexcept {
  return models_;
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
  std::vector<ModelDescriptor> models;
  for (const auto id : {ModelId::tiny, ModelId::base, ModelId::small,
                        ModelId::medium, ModelId::large, ModelId::turbo}) {
    models.push_back(
        ModelDescriptor{id, model_id_name(id), {}, {}, "unverified", 0});
  }
  return ModelCatalog(std::move(models));
}

}  // namespace obs_whisperbleep::model
