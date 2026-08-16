// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "obs_whisperbleep/model/model_downloader.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M3 downloader test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::filesystem::path test_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("obs-whisperbleep-m3-" + std::to_string(stamp));
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;

  const auto root = test_root();
  std::filesystem::create_directories(root);
  const auto source = root / "source.bin";
  const auto destination = root / "cache" / "tiny.model";
  {
    std::ofstream output(source, std::ios::binary);
    output << "abc";
  }

  ModelDescriptor model{
      ModelId::tiny,
      "tiny",
      "file://" + source.generic_string(),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "MIT",
      0,
      std::nullopt};
  ModelDownloader downloader;
  const auto downloaded = downloader.download(model, destination);
  expect(downloaded.status == DownloadStatus::success,
         "downloads and verifies a local model source");
  expect(downloaded.path == destination && std::filesystem::exists(destination),
         "atomically publishes the verified destination");

  const auto cached = downloader.download(model, destination);
  expect(cached.status == DownloadStatus::success &&
             cached.message == "Model already verified",
         "reuses an already verified cache entry");

  auto invalid = model;
  invalid.sha256.assign(64, '0');
  const auto invalid_result = downloader.download(invalid, root / "bad.model");
  expect(invalid_result.status == DownloadStatus::verification_failed,
         "rejects a checksum mismatch");

  const auto invalid_source = root / "invalid-source.bin";
  {
    std::ofstream output(invalid_source, std::ios::binary);
    output << "invalid";
  }
  invalid.source_url = "file://" + invalid_source.generic_string();
  const auto preserved = downloader.download(invalid, destination);
  expect(preserved.status == DownloadStatus::verification_failed,
         "rejects an invalid replacement for an existing cache entry");
  {
    std::ifstream input(destination, std::ios::binary);
    std::string contents;
    input >> contents;
    expect(contents == "abc", "preserves the last valid cache entry");
  }

  const auto https_model = [&] {
    auto value = model;
    value.source_url = "https://example.invalid/tiny.pt";
    return value;
  }();
  DownloadOptions transport_options;
  transport_options.transport = [](std::string_view, const std::filesystem::path& path,
                                   const DownloadCancellation&, std::string&) {
    std::ofstream output(path, std::ios::binary);
    output << "abc";
    return static_cast<bool>(output);
  };
  ModelDownloader transport_downloader(transport_options);
  const auto transported =
      transport_downloader.download(https_model, root / "transport.model");
  expect(transported.status == DownloadStatus::success,
         "uses an explicitly injected HTTPS transport");

  DownloadOptions cancelled_options;
  cancelled_options.is_cancelled = [] { return true; };
  ModelDownloader cancelled_downloader(cancelled_options);
  const auto cancelled =
      cancelled_downloader.download(model, root / "cancelled.model");
  expect(cancelled.status == DownloadStatus::cancelled,
         "cancels before starting a transfer");

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  return EXIT_SUCCESS;
}
