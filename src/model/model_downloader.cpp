// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/model/model_downloader.hpp"

namespace obs_whisperbleep::model {

DownloadResult ModelDownloader::download(
    const ModelDescriptor& model, const std::filesystem::path& destination) {
  (void)destination;
  return {DownloadStatus::unsupported, {},
          "Model downloads are not available in the M0 scaffold: " +
              model.name};
}

}  // namespace obs_whisperbleep::model
