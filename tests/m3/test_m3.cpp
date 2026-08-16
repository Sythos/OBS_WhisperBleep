// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "obs_whisperbleep/model/model_manager.hpp"
#include "obs_whisperbleep/runtime/backend_selector.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M3 test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::filesystem::path test_cache_root(const char* suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (std::string("obs-whisperbleep-m3-manager-") + suffix + "-" +
          std::to_string(stamp));
}

class SuccessfulDownloader final
    : public obs_whisperbleep::model::IModelDownloader {
 public:
  [[nodiscard]] obs_whisperbleep::model::DownloadResult download(
      const obs_whisperbleep::model::ModelDescriptor& model,
      const std::filesystem::path& destination) override {
    last_model = model.id;
    last_destination = destination;
    ++calls;
    return {obs_whisperbleep::model::DownloadStatus::success, destination, {}};
  }

  int calls = 0;
  obs_whisperbleep::model::ModelId last_model =
      obs_whisperbleep::model::ModelId::tiny;
  std::filesystem::path last_destination;
};

class BlockingDownloader final
    : public obs_whisperbleep::model::IModelDownloader {
 public:
  [[nodiscard]] obs_whisperbleep::model::DownloadResult download(
      const obs_whisperbleep::model::ModelDescriptor& model,
      const std::filesystem::path& destination) override {
    {
      std::lock_guard lock(mutex);
      entered = true;
      model_id = model.id;
    }
    entered_condition.notify_one();
    std::unique_lock lock(mutex);
    release_condition.wait(lock, [this] { return released; });
    return {obs_whisperbleep::model::DownloadStatus::success, destination, {}};
  }

  void wait_until_entered() {
    std::unique_lock lock(mutex);
    entered_condition.wait(lock, [this] { return entered; });
  }

  void release() {
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    release_condition.notify_one();
  }

  std::mutex mutex;
  std::condition_variable entered_condition;
  std::condition_variable release_condition;
  bool entered = false;
  bool released = false;
  obs_whisperbleep::model::ModelId model_id =
      obs_whisperbleep::model::ModelId::tiny;
};

class ConfigurableVerifier final
    : public obs_whisperbleep::model::IModelVerifier {
 public:
  [[nodiscard]] obs_whisperbleep::model::ModelVerificationResult verify(
      const obs_whisperbleep::model::ModelDescriptor&,
      const std::filesystem::path&) override {
    ++calls;
    if (!accepted) {
      return {obs_whisperbleep::model::VerificationStatus::failed,
              "deterministic verification failure"};
    }
    return {obs_whisperbleep::model::VerificationStatus::verified, {}};
  }

  bool accepted = true;
  int calls = 0;
};

class ConfigurableRuntime final
    : public obs_whisperbleep::runtime::IWhisperRuntime {
 public:
  [[nodiscard]] obs_whisperbleep::runtime::RuntimeStatus initialize(
      std::string_view model_path) override {
    last_path = std::string(model_path);
    ++initializations;
    if (!queued_statuses.empty()) {
      const auto next = queued_statuses.front();
      queued_statuses.erase(queued_statuses.begin());
      return next;
    }
    return status;
  }

  [[nodiscard]] std::vector<obs_whisperbleep::runtime::TranscriptSegment>
  transcribe(const float*, std::size_t, std::uint32_t) override {
    return {};
  }

  obs_whisperbleep::runtime::RuntimeStatus status =
      obs_whisperbleep::runtime::RuntimeStatus::ready;
  std::vector<obs_whisperbleep::runtime::RuntimeStatus> queued_statuses;
  int initializations = 0;
  std::string last_path;
};

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;
  using namespace obs_whisperbleep::runtime;

  SuccessfulDownloader downloader;
  ConfigurableVerifier verifier;
  ConfigurableRuntime runtime;
  const auto cache_root = test_cache_root("sync");
  const auto async_cache_root = test_cache_root("async");
  ModelManager manager(default_catalog(), &downloader, &verifier, &runtime,
                       cache_root);

  expect(manager.select(ModelId::tiny), "activates a verified model");
  expect(manager.state() == ModelState::active &&
             manager.active_model() == ModelId::tiny,
         "publishes active model only after activation");
  expect(verifier.calls == 1 && runtime.initializations == 1,
         "runs verification and runtime activation");
  expect(manager.active_path().filename() == "tiny.model",
         "stores the active model path");

  expect(manager.select(ModelId::base, ModelRetentionPolicy::retain_previous),
         "activates a second model");
  expect(manager.active_model() == ModelId::base &&
             manager.previous_model() == ModelId::tiny,
         "retains the previous model after successful activation");
  expect(manager.rollback(), "rolls back to the retained model");
  expect(manager.active_model() == ModelId::tiny &&
             manager.previous_model() == ModelId::base,
         "rollback swaps active and previous records");

  verifier.accepted = false;
  expect(!manager.select(ModelId::small), "rejects failed verification");
  expect(manager.state() == ModelState::error &&
             manager.active_model() == ModelId::tiny,
         "keeps the last active model after verification failure");
  expect(manager.last_error() == "deterministic verification failure",
         "reports verification failure");
  verifier.accepted = true;

  runtime.status = RuntimeStatus::unavailable;
  runtime.queued_statuses = {RuntimeStatus::unavailable, RuntimeStatus::ready};
  expect(!manager.select(ModelId::small), "rejects unavailable runtime");
  expect(manager.active_model() == ModelId::tiny,
         "keeps the active model after runtime failure");
  expect(runtime.last_path == manager.active_path().string(),
         "restores the previous runtime after activation failure");
  runtime.status = RuntimeStatus::ready;

  expect(manager.select(ModelId::small, ModelRetentionPolicy::selected_only),
         "supports selected-only retention");
  expect(!manager.previous_model().has_value(),
         "clears previous retention only after activation");

  BlockingDownloader blocking_downloader;
  ModelManager asynchronous_manager(default_catalog(), &blocking_downloader,
                                     &verifier, nullptr, async_cache_root);
  auto selection = asynchronous_manager.select_async(ModelId::base);
  blocking_downloader.wait_until_entered();
  expect(asynchronous_manager.state() == ModelState::downloading &&
             asynchronous_manager.pending_model() == ModelId::base,
         "exposes a synchronized pending download state");
  blocking_downloader.release();
  expect(selection.get() && asynchronous_manager.state() == ModelState::active,
         "completes the serialized asynchronous selection");

  const auto fallback = select_backend(Backend::cuda, {true, false});
  expect(fallback.used_fallback && !fallback.requested_available &&
             fallback.selected_available &&
             fallback.fallback_reason == "Requested backend is unavailable",
         "reports CPU fallback metadata");
  const auto unavailable = select_backend(Backend::cuda, {false, false});
  expect(!unavailable.selected_available && unavailable.used_fallback,
         "reports when no fallback backend is usable");

  StubWhisperRuntime stub;
  expect(stub.initialize("not-a-real-model") == RuntimeStatus::unavailable,
         "keeps the stub runtime isolated from model activation");
  expect(std::string(runtime_status_name(RuntimeStatus::unavailable)) ==
             "unavailable",
         "reports runtime status metadata");

  const auto strict_root = test_cache_root("strict");
  const auto strict_source = strict_root / "source.pt";
  std::filesystem::create_directories(strict_root);
  {
    std::ofstream output(strict_source, std::ios::binary);
    output << "abc";
  }
  const ModelDescriptor strict_descriptor{
      ModelId::tiny,
      "tiny",
      "file://" + strict_source.generic_string(),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "MIT",
      0,
      std::nullopt,
      "openai-whisper-pytorch-checkpoint"};
  ModelManager strict_manager(
      ModelCatalog(std::vector<ModelDescriptor>{strict_descriptor}), nullptr,
      nullptr, nullptr, strict_root / "cache");
  expect(strict_manager.select(ModelId::tiny),
         "default verifier accepts a verified model file");

  const auto calls_before_relative_cache = downloader.calls;
  ModelManager relative_cache_manager(default_catalog(), &downloader, &verifier,
                                      nullptr, "relative-cache");
  const auto relative_cache_status = relative_cache_manager.status();
  expect(!relative_cache_manager.select(ModelId::tiny) &&
             downloader.calls == calls_before_relative_cache,
         "rejects a relative cache before downloading");
  expect(!relative_cache_status.cache_available &&
             !relative_cache_status.cache_error.empty(),
         "reports the unavailable cache in manager status");

  SuccessfulDownloader confined_downloader;
  const ModelDescriptor unsafe_name_descriptor{
      ModelId::tiny, "../outside", "file://unused", std::string(64, '0'),
      "MIT", 0, std::nullopt, "openai-whisper-pytorch-checkpoint"};
  ModelManager confined_manager(
      ModelCatalog(std::vector<ModelDescriptor>{unsafe_name_descriptor}),
      &confined_downloader, &verifier, nullptr, cache_root);
  expect(confined_manager.select(ModelId::tiny) &&
             confined_downloader.last_destination.parent_path() == cache_root &&
             confined_downloader.last_destination.filename() == "tiny.model",
         "confines model cache filenames to the selected cache directory");

  std::error_code cleanup_error;
  std::filesystem::remove_all(cache_root, cleanup_error);
  std::filesystem::remove_all(async_cache_root, cleanup_error);
  std::filesystem::remove_all(strict_root, cleanup_error);

  return EXIT_SUCCESS;
}
