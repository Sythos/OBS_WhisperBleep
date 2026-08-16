// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <optional>
#include <string>

#include "obs_whisperbleep/model/model_catalog.hpp"
#include "obs_whisperbleep/model/model_downloader.hpp"

namespace obs_whisperbleep::model {

enum class ModelState { unselected, downloading, verifying, active, error };

class ModelManager {
 public:
  explicit ModelManager(ModelCatalog catalog = default_catalog(),
                         IModelDownloader* downloader = nullptr);

  [[nodiscard]] bool select(ModelId id, bool keep_previous = true);
  [[nodiscard]] ModelState state() const noexcept;
  [[nodiscard]] std::optional<ModelId> active_model() const noexcept;
  [[nodiscard]] std::optional<ModelId> previous_model() const noexcept;
  [[nodiscard]] const std::string& last_error() const noexcept;

 private:
  ModelCatalog catalog_;
  ModelDownloader default_downloader_;
  IModelDownloader* downloader_ = nullptr;
  ModelState state_ = ModelState::unselected;
  std::optional<ModelId> active_model_;
  std::optional<ModelId> previous_model_;
  std::string last_error_;
};

}  // namespace obs_whisperbleep::model
