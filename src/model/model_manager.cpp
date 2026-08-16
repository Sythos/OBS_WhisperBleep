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
  if (model.format.empty()) {
    return {VerificationStatus::failed, "Model descriptor has no format"};
  }
  if (path.empty() || !path.is_absolute()) {
    return {VerificationStatus::failed,
            "Downloaded model path is not an absolute path"};
  }
  const auto verification = ModelDownloader::verify_file(model, path);
  if (!verification.verified) {
    return {VerificationStatus::failed,
            verification.message.empty() ? "Model file verification failed"
                                          : verification.message};
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
    cache_error_ =
        "Unable to resolve an absolute per-user model cache directory";
  } else if (!cache_root_.is_absolute()) {
    cache_error_ =
        "Unable to resolve an absolute per-user model cache directory";
    cache_root_.clear();
  }
}

ModelManager::~ModelManager() {
  {
    std::lock_guard lock(async_mutex_);
    async_stopping_ = true;
  }
  async_condition_.notify_all();
  if (async_worker_.joinable()) {
    async_worker_.join();
  }
}

bool ModelManager::select(ModelId id, bool keep_previous) {
  return select(id, keep_previous ? ModelRetentionPolicy::retain_previous
                                 : ModelRetentionPolicy::selected_only);
}

ModelSelectionFuture ModelManager::select_async(ModelId id,
                                                ModelRetentionPolicy policy) {
  std::promise<bool> promise;
  auto future = promise.get_future();
  {
    std::lock_guard lock(async_mutex_);
    if (async_stopping_) {
      promise.set_value(false);
      return future;
    }
    if (!async_worker_.joinable()) {
      try {
        async_worker_ = std::thread(&ModelManager::worker_loop, this);
      } catch (...) {
        promise.set_value(false);
        set_error("Unable to start the model selection worker");
        return future;
      }
    }
    async_requests_.push(
        AsyncRequest{id, policy, std::move(promise)});
  }
  async_condition_.notify_one();
  return future;
}

bool ModelManager::select(ModelId id, ModelRetentionPolicy policy) {
  std::lock_guard selection_lock(selection_mutex_);
  const auto* descriptor = catalog_.find(id);
  if (descriptor == nullptr) {
    set_error("Model is not present in the catalog");
    return false;
  }

  if (cache_root_.empty()) {
    set_error(cache_error_.empty()
                  ? "Unable to resolve an absolute per-user model cache "
                    "directory"
                  : cache_error_);
    return false;
  }

  std::optional<ModelRecord> old_active;
  std::optional<ModelRecord> old_previous;
  bool already_active = false;
  {
    std::lock_guard state_lock(state_mutex_);
    old_active = active_model_;
    old_previous = previous_model_;
    last_error_.clear();
    pending_model_ = id;
    if (active_model_.has_value() && active_model_->id == id) {
      if (policy == ModelRetentionPolicy::selected_only) {
        previous_model_.reset();
      }
      pending_model_.reset();
      state_ = ModelState::active;
      already_active = true;
    } else {
      state_ = ModelState::downloading;
    }
  }

  const auto cleanup_selected_only = [&](const std::filesystem::path& keep) {
    if (policy != ModelRetentionPolicy::selected_only) {
      return;
    }
    std::string cleanup_message;
    const auto remove_record = [&](const std::optional<ModelRecord>& record) {
      if (!record.has_value() || record->path.empty() || record->path == keep) {
        return;
      }
      std::error_code cleanup_error;
      std::filesystem::remove(record->path, cleanup_error);
      if (cleanup_error && cleanup_message.empty()) {
        cleanup_message = cleanup_error.message();
      }
    };
    remove_record(old_active);
    remove_record(old_previous);
    if (!cleanup_message.empty()) {
      std::lock_guard state_lock(state_mutex_);
      last_error_ = "Model activated; an older cache entry was retained "
                    "because cleanup failed: " +
                    cleanup_message;
    }
  };

  if (already_active) {
    cleanup_selected_only(active_path());
    return true;
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
      std::string message = std::string("Whisper runtime activation failed: ") +
                            error.what();
      std::string restore_error;
      if (!restore_runtime(old_active, restore_error)) {
        message += "; previous runtime restore failed: " + restore_error;
      }
      set_error(message);
      return false;
    } catch (...) {
      std::string message =
          "Whisper runtime activation failed with an unknown error";
      std::string restore_error;
      if (!restore_runtime(old_active, restore_error)) {
        message += "; previous runtime restore failed: " + restore_error;
      }
      set_error(message);
      return false;
    }
    if (runtime_status != runtime::RuntimeStatus::ready) {
      std::string message =
          std::string("Whisper runtime activation failed: ") +
          runtime::runtime_status_name(runtime_status);
      std::string restore_error;
      if (!restore_runtime(old_active, restore_error)) {
        message += "; previous runtime restore failed: " + restore_error;
      }
      set_error(message);
      return false;
    }
  }

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

  cleanup_selected_only(result.path);
  return true;
}

bool ModelManager::rollback() {
  std::lock_guard selection_lock(selection_mutex_);
  std::optional<ModelRecord> rollback_target;
  std::optional<ModelRecord> current_active;
  {
    std::lock_guard state_lock(state_mutex_);
    if (!previous_model_.has_value()) {
      pending_model_.reset();
      state_ = ModelState::error;
      last_error_ = "No previous model is available for rollback";
      return false;
    }
    rollback_target = previous_model_;
    current_active = active_model_;
  }

  const auto* descriptor = catalog_.find(rollback_target->id);
  if (descriptor == nullptr) {
    set_error("Previous model is no longer present in the catalog");
    return false;
  }
  ModelVerificationResult verification;
  try {
    verification = verifier_->verify(*descriptor, rollback_target->path);
  } catch (const std::exception& error) {
    set_error(std::string("Rollback verification failed: ") + error.what());
    return false;
  } catch (...) {
    set_error("Rollback verification failed with an unknown error");
    return false;
  }
  if (verification.status != VerificationStatus::verified) {
    set_error(verification.message.empty() ? "Rollback verification failed"
                                           : verification.message);
    return false;
  }

  if (runtime_ != nullptr) {
    runtime::RuntimeStatus runtime_status = runtime::RuntimeStatus::error;
    try {
      runtime_status = runtime_->initialize(rollback_target->path.string());
    } catch (const std::exception& error) {
      std::string message = std::string("Rollback activation failed: ") +
                            error.what();
      std::string restore_error;
      if (!restore_runtime(current_active, restore_error)) {
        message += "; current runtime restore failed: " + restore_error;
      }
      set_error(message);
      return false;
    } catch (...) {
      std::string message =
          "Rollback activation failed with an unknown error";
      std::string restore_error;
      if (!restore_runtime(current_active, restore_error)) {
        message += "; current runtime restore failed: " + restore_error;
      }
      set_error(message);
      return false;
    }
    if (runtime_status != runtime::RuntimeStatus::ready) {
      std::string message = std::string("Rollback activation failed: ") +
                            runtime::runtime_status_name(runtime_status);
      std::string restore_error;
      if (!restore_runtime(current_active, restore_error)) {
        message += "; current runtime restore failed: " + restore_error;
      }
      set_error(message);
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

bool ModelManager::restore_runtime(const std::optional<ModelRecord>& record,
                                   std::string& error) {
  if (runtime_ == nullptr || !record.has_value()) {
    return true;
  }
  try {
    const auto status = runtime_->initialize(record->path.string());
    if (status == runtime::RuntimeStatus::ready) {
      return true;
    }
    error = runtime::runtime_status_name(status);
    return false;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  } catch (...) {
    error = "unknown runtime error";
    return false;
  }
}

void ModelManager::worker_loop() {
  for (;;) {
    AsyncRequest request;
    {
      std::unique_lock lock(async_mutex_);
      async_condition_.wait(lock, [this] {
        return async_stopping_ || !async_requests_.empty();
      });
      if (async_stopping_ && async_requests_.empty()) {
        return;
      }
      request = std::move(async_requests_.front());
      async_requests_.pop();
    }

    bool result = false;
    try {
      result = select(request.id, request.policy);
    } catch (const std::exception& exception) {
      set_error(std::string("Asynchronous model selection failed: ") +
                exception.what());
    } catch (...) {
      set_error("Asynchronous model selection failed with an unknown error");
    }
    request.promise.set_value(result);
  }
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
  result.cache_available = !cache_root_.empty();
  result.cache_error = cache_error_;
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
