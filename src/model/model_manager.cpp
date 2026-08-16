// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/model/model_manager.hpp"

#include <filesystem>
#include <utility>

namespace obs_whisperbleep::model {

ModelManager::ModelManager(ModelCatalog catalog, IModelDownloader* downloader)
    : catalog_(std::move(catalog)),
      downloader_(downloader == nullptr ? &default_downloader_ : downloader) {}

bool ModelManager::select(ModelId id, bool keep_previous) {
  last_error_.clear();
  const auto* descriptor = catalog_.find(id);
  if (descriptor == nullptr) {
    state_ = ModelState::error;
    last_error_ = "Model is not present in the catalog";
    return false;
  }
  if (active_model_ == id) {
    state_ = ModelState::active;
    return true;
  }

  const auto old_active = active_model_;
  state_ = ModelState::downloading;
  const auto destination = std::filesystem::path("model-cache") /
                           (descriptor->name + ".model");
  const auto result = downloader_->download(*descriptor, destination);
  if (result.status != DownloadStatus::success) {
    state_ = ModelState::error;
    last_error_ = result.message.empty() ? "Model download failed"
                                          : result.message;
    return false;
  }

  state_ = ModelState::verifying;
  if (keep_previous) {
    previous_model_ = old_active;
  } else {
    previous_model_.reset();
  }
  active_model_ = id;
  state_ = ModelState::active;
  return true;
}

ModelState ModelManager::state() const noexcept { return state_; }

std::optional<ModelId> ModelManager::active_model() const noexcept {
  return active_model_;
}

std::optional<ModelId> ModelManager::previous_model() const noexcept {
  return previous_model_;
}

const std::string& ModelManager::last_error() const noexcept {
  return last_error_;
}

}  // namespace obs_whisperbleep::model
