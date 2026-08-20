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

class AcceptingVerifier final : public obs_whisperbleep::model::IModelVerifier {
 public:
  [[nodiscard]] obs_whisperbleep::model::ModelVerificationResult verify(
      const obs_whisperbleep::model::ModelDescriptor&,
      const std::filesystem::path&) override {
    return {obs_whisperbleep::model::VerificationStatus::verified, {}};
  }
};

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;
  using namespace obs_whisperbleep::runtime;

  const auto catalog = default_catalog();
  expect(catalog.models().size() == 10,
         "catalog exposes multilingual and English-only model ids");
  expect(catalog.find(ModelId::turbo) != nullptr, "catalog finds turbo");
  expect(catalog.find(ModelId::medium_en) != nullptr,
         "catalog finds the English-only selector option");
  expect(model_id_from_name("small") == ModelId::small, "parses model id");
  expect(model_id_from_name("small.en") == ModelId::small_en,
         "parses canonical English-only model id");
  expect(!model_id_from_name("missing").has_value(), "rejects unknown model");

  SuccessfulDownloader downloader;
  AcceptingVerifier verifier;
  const auto cache_root =
      std::filesystem::temp_directory_path() / "obs-whisperbleep-model-test";
  ModelManager manager(catalog, &downloader, &verifier, nullptr, cache_root);
  expect(manager.select(ModelId::tiny), "activates downloaded model");
  expect(manager.active_model() == ModelId::tiny, "stores active model");
  expect(downloader.last_destination.filename() == "tiny.model",
         "uses deterministic destination");
  expect(manager.select(ModelId::tiny_en),
         "activates an English-only selector option without bundled weights");
  expect(downloader.last_model == ModelId::tiny_en,
         "passes the English-only model descriptor to the downloader");
  expect(downloader.last_destination.filename() == "tiny.en.model",
         "uses the canonical English-only name in the cache destination");
  expect(manager.select(ModelId::base), "activates second model");
  expect(manager.previous_model() == ModelId::tiny_en,
         "retains the previously selected model");

  const auto fallback = select_backend(Backend::cuda, {true, false});
  expect(fallback.selected == Backend::cpu && fallback.used_fallback,
         "falls back to CPU when CUDA is unavailable");
  const auto automatic = select_backend(Backend::auto_select, {true, true});
  expect(automatic.selected == Backend::cuda, "auto selects CUDA when available");

  std::error_code cleanup_error;
  std::filesystem::remove_all(cache_root, cleanup_error);

  return EXIT_SUCCESS;
}
