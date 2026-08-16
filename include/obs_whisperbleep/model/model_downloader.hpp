// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <string>

#include "obs_whisperbleep/model/model_catalog.hpp"

namespace obs_whisperbleep::model {

enum class DownloadStatus { success, cancelled, unsupported, failed };

struct DownloadResult {
  DownloadStatus status = DownloadStatus::unsupported;
  std::filesystem::path path;
  std::string message;
};

class IModelDownloader {
 public:
  virtual ~IModelDownloader() = default;
  [[nodiscard]] virtual DownloadResult download(
      const ModelDescriptor& model,
      const std::filesystem::path& destination) = 0;
};

/** M0 no-network implementation; real HTTPS and checksum validation are M3. */
class ModelDownloader final : public IModelDownloader {
 public:
  [[nodiscard]] DownloadResult download(
      const ModelDescriptor& model,
      const std::filesystem::path& destination) override;
};

}  // namespace obs_whisperbleep::model
