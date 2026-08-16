// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

#include "obs_whisperbleep/model/model_catalog.hpp"
#include "obs_whisperbleep/model/model_downloader.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace obs_whisperbleep::model {

enum class ModelState { unselected, downloading, verifying, active, error };

enum class ModelRetentionPolicy { retain_previous, selected_only };

enum class VerificationStatus { verified, failed };

struct ModelVerificationResult {
  VerificationStatus status = VerificationStatus::failed;
  std::string message;
};

/**
 * Verifies a completed model before the manager exposes it as active.
 * Implementations can perform checksum, metadata and format validation.
 */
class IModelVerifier {
 public:
  virtual ~IModelVerifier() = default;
  [[nodiscard]] virtual ModelVerificationResult verify(
      const ModelDescriptor& model,
      const std::filesystem::path& path) = 0;
};

/**
 * Dependency-free verifier for the M3 scaffold. It checks the absolute regular
 * file, catalog format metadata and SHA-256/size constraints before activation.
 */
class ModelVerifier final : public IModelVerifier {
 public:
  [[nodiscard]] ModelVerificationResult verify(
      const ModelDescriptor& model,
      const std::filesystem::path& path) override;
};

struct ModelManagerStatus {
  ModelState state = ModelState::unselected;
  bool cache_available = false;
  std::optional<ModelId> active_model;
  std::optional<ModelId> previous_model;
  std::optional<ModelId> pending_model;
  std::filesystem::path active_path;
  std::filesystem::path previous_path;
  std::string cache_error;
  std::string last_error;
};

using ModelSelectionFuture = std::future<bool>;

class ModelManager {
 public:
  explicit ModelManager(ModelCatalog catalog = default_catalog(),
                         IModelDownloader* downloader = nullptr,
                         IModelVerifier* verifier = nullptr,
                         runtime::IWhisperRuntime* runtime = nullptr,
                         std::filesystem::path cache_root = {});
  ~ModelManager();

  [[nodiscard]] bool select(ModelId id, bool keep_previous = true);
  [[nodiscard]] bool select(ModelId id, ModelRetentionPolicy policy);
  /** Queue model selection on the manager-owned worker thread. */
  [[nodiscard]] ModelSelectionFuture select_async(
      ModelId id,
      ModelRetentionPolicy policy = ModelRetentionPolicy::retain_previous);
  [[nodiscard]] bool rollback();
  [[nodiscard]] ModelState state() const noexcept;
  [[nodiscard]] std::optional<ModelId> active_model() const noexcept;
  [[nodiscard]] std::optional<ModelId> previous_model() const noexcept;
  [[nodiscard]] std::optional<ModelId> pending_model() const noexcept;
  [[nodiscard]] std::filesystem::path active_path() const;
  [[nodiscard]] std::filesystem::path previous_path() const;
  [[nodiscard]] std::string last_error() const;
  [[nodiscard]] ModelManagerStatus status() const;

 private:
  struct ModelRecord {
    ModelId id = ModelId::tiny;
    std::filesystem::path path;
  };

  struct AsyncRequest {
    ModelId id = ModelId::tiny;
    ModelRetentionPolicy policy = ModelRetentionPolicy::retain_previous;
    std::promise<bool> promise;
  };

  void set_error(const std::string& message);
  bool select_impl(ModelId id, ModelRetentionPolicy policy,
                   const DownloadCancellation& cancellation);
  bool restore_runtime(const std::optional<ModelRecord>& record,
                       std::string& error);
  void worker_loop();

  ModelCatalog catalog_;
  ModelDownloader default_downloader_;
  ModelVerifier default_verifier_;
  IModelDownloader* downloader_ = nullptr;
  IModelVerifier* verifier_ = nullptr;
  runtime::IWhisperRuntime* runtime_ = nullptr;
  std::filesystem::path cache_root_;
  std::string cache_error_;
  mutable std::mutex state_mutex_;
  std::mutex selection_mutex_;
  std::mutex async_mutex_;
  std::condition_variable async_condition_;
  std::queue<AsyncRequest> async_requests_;
  std::shared_ptr<std::atomic_bool> async_cancel_ =
      std::make_shared<std::atomic_bool>(false);
  bool async_stopping_ = false;
  std::thread async_worker_;
  ModelState state_ = ModelState::unselected;
  std::optional<ModelRecord> active_model_;
  std::optional<ModelRecord> previous_model_;
  std::optional<ModelId> pending_model_;
  std::string last_error_;
};

}  // namespace obs_whisperbleep::model
