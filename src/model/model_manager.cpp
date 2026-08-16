// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/model/model_manager.hpp"

#include <exception>
#include <filesystem>
#include <utility>

#include "obs_whisperbleep/platform/platform_info.hpp"

namespace obs_whisperbleep::model {

ModelVerificationResult ModelVerifier::verify(
    const ModelDescriptor& model, const std::filesystem::path& path) {
  if (model.name.empty()) {
    return {VerificationStatus::failed, "Model descriptor has no name"};
  }
  if (path.empty()) {
    return {VerificationStatus::failed, "Downloaded model path is empty"};
  }
  return {VerificationStatus::verified, {}};
}

ModelManager::ModelManager(ModelCatalog catalog, IModelDownloader* downloader,
                           IModelVerifier* verifier,
                           runtime::IWhisperRuntime* runtime,
                           std::filesystem::path cache_root)
    : catalog_(std::move(catalog)),
      downloader_(downloader == nullptr ? &default_downloader_ : downloader),
      verifier_(verifier == nullptr ? &default_verifier_ : verifier),
      runtime_(runtime),
      cache_root_(cache_root.empty()
                      ? platform::user_cache_directory("Sythos/OBS-WhisperBleep")
                      : std::move(cache_root)) {
  if (cache_root_.empty()) {
    cache_root_ = std::filesystem::path("model-cache");
  }
}

bool ModelManager::select(ModelId id, bool keep_previous) {
  return select(id, keep_previous ? ModelRetentionPolicy::retain_previous
                                 : ModelRetentionPolicy::selected_only);
}

bool ModelManager::select(ModelId id, ModelRetentionPolicy policy) {
  std::lock_guard selection_lock(selection_mutex_);
  const auto* descriptor = catalog_.find(id);
  if (descriptor == nullptr) {
    set_error("Model is not present in the catalog");
    return false;
  }

  {
    std::lock_guard state_lock(state_mutex_);
    last_error_.clear();
    pending_model_ = id;
  }

  {
    std::lock_guard state_lock(state_mutex_);
    if (active_model_.has_value() && active_model_->id == id) {
      if (policy == ModelRetentionPolicy::selected_only) {
        previous_model_.reset();
      }
      pending_model_.reset();
      state_ = ModelState::active;
      return true;
    }
    state_ = ModelState::downloading;
  }

  const auto destination = cache_root_ / (descriptor->name + ".model");
  DownloadResult result;
  try {
    result = downloader_->download(*descriptor, destination);
  } catch (const std::exception& error) {
    set_error(std::string("Model download failed: ") + error.what());
    return false;
  } catch (...) {
    set_error("Model download failed with an unknown error");
    return false;
  }

  if (result.status != DownloadStatus::success) {
    set_error(result.message.empty() ? "Model download failed"
                                     : result.message);
    return false;
  }

  {
    std::lock_guard state_lock(state_mutex_);
    state_ = ModelState::verifying;
  }

  ModelVerificationResult verification;
  try {
    verification = verifier_->verify(*descriptor, result.path);
  } catch (const std::exception& error) {
    set_error(std::string("Model verification failed: ") + error.what());
    return false;
  } catch (...) {
    set_error("Model verification failed with an unknown error");
    return false;
  }
  if (verification.status != VerificationStatus::verified) {
    set_error(verification.message.empty() ? "Model verification failed"
                                           : verification.message);
    return false;
  }

  if (runtime_ != nullptr) {
    runtime::RuntimeStatus runtime_status = runtime::RuntimeStatus::error;
    try {
      runtime_status = runtime_->initialize(result.path.string());
    } catch (const std::exception& error) {
      set_error(std::string("Whisper runtime activation failed: ") +
                error.what());
      return false;
    } catch (...) {
      set_error("Whisper runtime activation failed with an unknown error");
      return false;
    }
    if (runtime_status != runtime::RuntimeStatus::ready) {
      set_error(std::string("Whisper runtime activation failed: ") +
                runtime::runtime_status_name(runtime_status));
      return false;
    }
  }

  std::optional<ModelRecord> old_active;
  {
    std::lock_guard state_lock(state_mutex_);
    // Do not change active or previous records until download, verification
    // and optional runtime activation have all succeeded.
    old_active = active_model_;
    if (policy == ModelRetentionPolicy::retain_previous) {
      previous_model_ = active_model_;
    } else {
      previous_model_.reset();
    }
    active_model_ = ModelRecord{id, result.path};
    pending_model_.reset();
    last_error_.clear();
    state_ = ModelState::active;
  }

  if (policy == ModelRetentionPolicy::selected_only && old_active.has_value() &&
      old_active->path != result.path) {
    std::error_code cleanup_error;
    std::filesystem::remove(old_active->path, cleanup_error);
    if (cleanup_error) {
      std::lock_guard state_lock(state_mutex_);
      last_error_ = "Model activated; the previous model was retained because "
                    "cache cleanup failed: " +
                    cleanup_error.message();
    }
  }
  return true;
}

bool ModelManager::rollback() {
  std::lock_guard selection_lock(selection_mutex_);
  std::optional<ModelRecord> rollback_target;
  {
    std::lock_guard state_lock(state_mutex_);
    if (!previous_model_.has_value()) {
      pending_model_.reset();
      state_ = ModelState::error;
      last_error_ = "No previous model is available for rollback";
      return false;
    }
    rollback_target = previous_model_;
  }

  if (runtime_ != nullptr) {
    runtime::RuntimeStatus runtime_status = runtime::RuntimeStatus::error;
    try {
      runtime_status = runtime_->initialize(rollback_target->path.string());
    } catch (const std::exception& error) {
      set_error(std::string("Rollback activation failed: ") + error.what());
      return false;
    } catch (...) {
      set_error("Rollback activation failed with an unknown error");
      return false;
    }
    if (runtime_status != runtime::RuntimeStatus::ready) {
      set_error(std::string("Rollback activation failed: ") +
                runtime::runtime_status_name(runtime_status));
      return false;
    }
  }

  {
    std::lock_guard state_lock(state_mutex_);
    std::swap(active_model_, previous_model_);
    pending_model_.reset();
    last_error_.clear();
    state_ = ModelState::active;
  }
  return true;
}

void ModelManager::set_error(const std::string& message) {
  std::lock_guard state_lock(state_mutex_);
  pending_model_.reset();
  state_ = ModelState::error;
  last_error_ = message;
}

ModelState ModelManager::state() const noexcept {
  std::lock_guard state_lock(state_mutex_);
  return state_;
}

std::optional<ModelId> ModelManager::active_model() const noexcept {
  std::lock_guard state_lock(state_mutex_);
  if (!active_model_.has_value()) {
    return std::nullopt;
  }
  return active_model_->id;
}

std::optional<ModelId> ModelManager::previous_model() const noexcept {
  std::lock_guard state_lock(state_mutex_);
  if (!previous_model_.has_value()) {
    return std::nullopt;
  }
  return previous_model_->id;
}

std::optional<ModelId> ModelManager::pending_model() const noexcept {
  std::lock_guard state_lock(state_mutex_);
  return pending_model_;
}

std::filesystem::path ModelManager::active_path() const {
  std::lock_guard state_lock(state_mutex_);
  return active_model_.has_value() ? active_model_->path
                                   : std::filesystem::path{};
}

std::filesystem::path ModelManager::previous_path() const {
  std::lock_guard state_lock(state_mutex_);
  return previous_model_.has_value() ? previous_model_->path
                                     : std::filesystem::path{};
}

std::string ModelManager::last_error() const {
  std::lock_guard state_lock(state_mutex_);
  return last_error_;
}

ModelManagerStatus ModelManager::status() const {
  std::lock_guard state_lock(state_mutex_);
  ModelManagerStatus result;
  result.state = state_;
  result.pending_model = pending_model_;
  result.last_error = last_error_;
  if (active_model_.has_value()) {
    result.active_model = active_model_->id;
    result.active_path = active_model_->path;
  }
  if (previous_model_.has_value()) {
    result.previous_model = previous_model_->id;
    result.previous_path = previous_model_->path;
  }
  return result;
}

}  // namespace obs_whisperbleep::model
