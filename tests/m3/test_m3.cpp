// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
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
    return status;
  }

  [[nodiscard]] std::vector<obs_whisperbleep::runtime::TranscriptSegment>
  transcribe(const float*, std::size_t, std::uint32_t) override {
    return {};
  }

  obs_whisperbleep::runtime::RuntimeStatus status =
      obs_whisperbleep::runtime::RuntimeStatus::ready;
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
  ModelManager manager(default_catalog(), &downloader, &verifier, &runtime);

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
  expect(!manager.select(ModelId::small), "rejects unavailable runtime");
  expect(manager.active_model() == ModelId::tiny,
         "keeps the active model after runtime failure");
  runtime.status = RuntimeStatus::ready;

  expect(manager.select(ModelId::small, ModelRetentionPolicy::selected_only),
         "supports selected-only retention");
  expect(!manager.previous_model().has_value(),
         "clears previous retention only after activation");

  BlockingDownloader blocking_downloader;
  ModelManager asynchronous_manager(default_catalog(), &blocking_downloader);
  bool selected = false;
  std::thread selector([&] { selected = asynchronous_manager.select(ModelId::base); });
  blocking_downloader.wait_until_entered();
  expect(asynchronous_manager.state() == ModelState::downloading &&
             asynchronous_manager.pending_model() == ModelId::base,
         "exposes a synchronized pending download state");
  blocking_downloader.release();
  selector.join();
  expect(selected && asynchronous_manager.state() == ModelState::active,
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

  return EXIT_SUCCESS;
}
