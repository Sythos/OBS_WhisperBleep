// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "obs_whisperbleep/model/model_manager.hpp"
#include "obs_whisperbleep/runtime/backend_selector.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "model test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class SuccessfulDownloader final : public obs_whisperbleep::model::IModelDownloader {
 public:
  [[nodiscard]] obs_whisperbleep::model::DownloadResult download(
      const obs_whisperbleep::model::ModelDescriptor& model,
      const std::filesystem::path& destination) override {
    last_model = model.id;
    last_destination = destination;
    return {obs_whisperbleep::model::DownloadStatus::success, destination, {}};
  }

  obs_whisperbleep::model::ModelId last_model =
      obs_whisperbleep::model::ModelId::tiny;
  std::filesystem::path last_destination;
};

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;
  using namespace obs_whisperbleep::runtime;

  const auto catalog = default_catalog();
  expect(catalog.models().size() == 6, "catalog exposes six model ids");
  expect(catalog.find(ModelId::turbo) != nullptr, "catalog finds turbo");
  expect(model_id_from_name("small") == ModelId::small, "parses model id");
  expect(!model_id_from_name("missing").has_value(), "rejects unknown model");

  SuccessfulDownloader downloader;
  ModelManager manager(catalog, &downloader);
  expect(manager.select(ModelId::tiny), "activates downloaded model");
  expect(manager.active_model() == ModelId::tiny, "stores active model");
  expect(downloader.last_destination.filename() == "tiny.model",
         "uses deterministic destination");
  expect(manager.select(ModelId::base), "activates second model");
  expect(manager.previous_model() == ModelId::tiny, "retains previous model");

  const auto fallback = select_backend(Backend::cuda, {true, false});
  expect(fallback.selected == Backend::cpu && fallback.used_fallback,
         "falls back to CPU when CUDA is unavailable");
  const auto automatic = select_backend(Backend::auto_select, {true, true});
  expect(automatic.selected == Backend::cuda, "auto selects CUDA when available");

  return EXIT_SUCCESS;
}
