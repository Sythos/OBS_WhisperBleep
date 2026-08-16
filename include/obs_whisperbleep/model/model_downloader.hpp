// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "obs_whisperbleep/model/model_catalog.hpp"

namespace obs_whisperbleep::model {

enum class DownloadStatus {
  success,
  cancelled,
  unsupported,
  failed,
  verification_failed,
};

struct DownloadResult {
  DownloadStatus status = DownloadStatus::unsupported;
  std::filesystem::path path;
  std::string message;
};

struct DownloadVerificationResult {
  bool verified = false;
  std::string message;
};

using DownloadCancellation = std::function<bool()>;

using DownloadTransport = std::function<bool(
    std::string_view source_url, const std::filesystem::path& temporary_path,
    const DownloadCancellation& is_cancelled, std::string& error)>;

struct DownloadOptions {
  /** Return true to cancel before or during the copy operation. */
  DownloadCancellation is_cancelled;
  /** Optional HTTPS-capable transport supplied by the platform integration. */
  DownloadTransport transport;
};

class IModelDownloader {
 public:
  virtual ~IModelDownloader() = default;
  [[nodiscard]] virtual DownloadResult download(
      const ModelDescriptor& model,
      const std::filesystem::path& destination) = 0;
};

/**
 * Verified model downloader for a caller-provided absolute user cache path.
 *
 * The portable default supports local file:// sources. HTTP and HTTPS are
 * handled by an explicitly injected transport so network access remains
 * outside the realtime path and deterministic tests can avoid the network.
 */
class ModelDownloader final : public IModelDownloader {
 public:
  explicit ModelDownloader(DownloadOptions options = {});

  /** Verify a completed cache file without starting a transfer. */
  [[nodiscard]] static DownloadVerificationResult verify_file(
      const ModelDescriptor& model, const std::filesystem::path& path);

  [[nodiscard]] DownloadResult download(
      const ModelDescriptor& model,
      const std::filesystem::path& destination) override;

  [[nodiscard]] DownloadResult download(
      const ModelDescriptor& model,
      const std::filesystem::path& destination,
      const DownloadOptions& options);

 private:
  DownloadOptions options_;
};

}  // namespace obs_whisperbleep::model
