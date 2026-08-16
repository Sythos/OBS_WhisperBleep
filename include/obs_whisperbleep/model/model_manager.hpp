// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

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
 * Minimal M3 verifier for the dependency-free scaffold. The downloader owns
 * the actual transfer and integrity checks; this boundary still rejects an
 * empty path before activation and can be replaced by a strict verifier.
 */
class ModelVerifier final : public IModelVerifier {
 public:
  [[nodiscard]] ModelVerificationResult verify(
      const ModelDescriptor& model,
      const std::filesystem::path& path) override;
};

struct ModelManagerStatus {
  ModelState state = ModelState::unselected;
  std::optional<ModelId> active_model;
  std::optional<ModelId> previous_model;
  std::optional<ModelId> pending_model;
  std::filesystem::path active_path;
  std::filesystem::path previous_path;
  std::string last_error;
};

class ModelManager {
 public:
  explicit ModelManager(ModelCatalog catalog = default_catalog(),
                         IModelDownloader* downloader = nullptr,
                         IModelVerifier* verifier = nullptr,
                         runtime::IWhisperRuntime* runtime = nullptr,
                         std::filesystem::path cache_root = {});

  [[nodiscard]] bool select(ModelId id, bool keep_previous = true);
  [[nodiscard]] bool select(ModelId id, ModelRetentionPolicy policy);
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

  void set_error(const std::string& message);

  ModelCatalog catalog_;
  ModelDownloader default_downloader_;
  ModelVerifier default_verifier_;
  IModelDownloader* downloader_ = nullptr;
  IModelVerifier* verifier_ = nullptr;
  runtime::IWhisperRuntime* runtime_ = nullptr;
  std::filesystem::path cache_root_;
  mutable std::mutex state_mutex_;
  std::mutex selection_mutex_;
  ModelState state_ = ModelState::unselected;
  std::optional<ModelRecord> active_model_;
  std::optional<ModelRecord> previous_model_;
  std::optional<ModelId> pending_model_;
  std::string last_error_;
};

}  // namespace obs_whisperbleep::model
