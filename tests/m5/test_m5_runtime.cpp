// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "obs_whisperbleep/runtime/backend_selector.hpp"
#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M5 runtime test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class TestRuntime final : public obs_whisperbleep::runtime::IWhisperRuntime {
 public:
  explicit TestRuntime(obs_whisperbleep::runtime::RuntimeStatus status)
      : status_(status) {}

  [[nodiscard]] obs_whisperbleep::runtime::RuntimeStatus initialize(
      std::string_view model_path) override {
    initialized_path = std::string(model_path);
    return status_;
  }

  [[nodiscard]] obs_whisperbleep::runtime::RuntimeStatus initialize(
      std::string_view model_path, std::string_view language) override {
    initialized_path = std::string(model_path);
    initialized_language = std::string(language);
    return status_;
  }

  [[nodiscard]] std::vector<obs_whisperbleep::runtime::TranscriptSegment>
  transcribe(const float*, std::size_t, std::uint32_t) override {
    return {};
  }

  std::string initialized_path;
  std::string initialized_language;

 private:
  obs_whisperbleep::runtime::RuntimeStatus status_;
};

obs_whisperbleep::runtime::BackendCapabilityDetail capability(
    obs_whisperbleep::runtime::Backend backend,
    obs_whisperbleep::runtime::BackendProbeStatus status,
    std::string reason) {
  obs_whisperbleep::runtime::BackendCapabilityDetail detail;
  detail.backend = backend;
  detail.status = status;
  detail.provider = "deterministic-test";
  detail.runtime_version = backend == obs_whisperbleep::runtime::Backend::cpu
                               ? "portable"
                               : "13.2";
  detail.device_name = backend == obs_whisperbleep::runtime::Backend::cpu
                           ? "host-cpu"
                           : "test-gpu";
  detail.reason = std::move(reason);
  return detail;
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::runtime;

  const auto default_probe = probe_backend_capabilities({}, "linux");
  expect(default_probe.platform == "linux", "preserves probe platform");
  expect(default_probe.capabilities.cpu,
         "default probe keeps portable CPU available");
  expect(!default_probe.capabilities.cuda_13_2,
         "default probe does not claim CUDA support");
  expect(backend_capability_detail(default_probe, Backend::cuda).status ==
             BackendProbeStatus::not_probed,
         "default probe distinguishes CUDA from unavailable hardware");
  expect(std::string(backend_probe_status_name(BackendProbeStatus::available)) ==
             "available",
         "reports probe status names");

  const auto cuda_probe = probe_backend_capabilities(
      [](Backend backend) {
        if (backend == Backend::cpu) {
          return capability(backend, BackendProbeStatus::available,
                            "CPU is available");
        }
        return capability(backend, BackendProbeStatus::available,
                          "CUDA 13.2 runtime and driver are available");
      },
      "windows");
  expect(cuda_probe.capabilities.cpu && cuda_probe.capabilities.cuda_13_2,
         "injected probe publishes CPU and CUDA capabilities");
  const auto cuda_detail = backend_capability_detail(cuda_probe, Backend::cuda);
  expect(cuda_detail.runtime_version == "13.2" &&
             cuda_detail.device_name == "test-gpu",
         "preserves concrete CUDA probe details");

  const auto mac_gpu_probe = probe_backend_capabilities(
      [](Backend backend) {
        return capability(backend, BackendProbeStatus::available,
                          "test adapter reports the backend available");
      },
      "macOS universal");
  expect(!mac_gpu_probe.capabilities.cuda_13_2 &&
             backend_capability_detail(mac_gpu_probe, Backend::cuda).status ==
                 BackendProbeStatus::unavailable,
         "never exposes CUDA on macOS even when a probe overreports it");
  const auto mac_gpu_selection =
      select_backend_with_probe(Backend::cuda, mac_gpu_probe);
  expect(mac_gpu_selection.used_fallback &&
             mac_gpu_selection.selected == Backend::cpu,
         "falls back to CPU for a CUDA request on macOS");

  BackendProbeResult inconsistent;
  inconsistent.capabilities.cpu = true;
  inconsistent.capabilities.cuda_13_2 = true;
  inconsistent.capabilities.cuda = true;
  inconsistent.details = {
      capability(Backend::cpu, BackendProbeStatus::available,
                 "CPU is available"),
      capability(Backend::cuda, BackendProbeStatus::unavailable,
                 "CUDA evidence was revoked")};
  const auto normalized =
      select_backend_with_probe(Backend::auto_select, inconsistent);
  expect(normalized.selected == Backend::cpu &&
             normalized.selected_detail.status == BackendProbeStatus::available,
         "uses concrete capability evidence over inconsistent aggregate flags");

  const auto automatic =
      select_backend_with_probe(Backend::auto_select, cuda_probe);
  expect(automatic.selected == Backend::cuda && automatic.selected_available &&
             automatic.selected_detail.status == BackendProbeStatus::available,
         "Auto selects the available CUDA backend with evidence");
  expect(automatic.requested_detail.status == BackendProbeStatus::not_probed,
         "Auto keeps concrete request evidence separate");

  const auto cpu = select_backend_with_probe(Backend::cpu, cuda_probe);
  expect(cpu.selected == Backend::cpu && cpu.requested_available &&
             cpu.selected_detail.backend == Backend::cpu,
         "explicit CPU selection remains available");

  const auto cpu_only_probe = probe_backend_capabilities(
      [](Backend backend) {
        return capability(
            backend,
            backend == Backend::cpu ? BackendProbeStatus::available
                                    : BackendProbeStatus::unavailable,
            backend == Backend::cpu ? "CPU is available"
                                    : "CUDA is unavailable on this host");
      },
      "macos");
  const auto cuda_fallback =
      select_backend_with_probe(Backend::cuda, cpu_only_probe);
  expect(cuda_fallback.used_fallback &&
             cuda_fallback.selected == Backend::cpu &&
             cuda_fallback.requested_detail.status ==
                 BackendProbeStatus::unavailable &&
             cuda_fallback.selected_detail.status ==
                 BackendProbeStatus::available,
         "unavailable CUDA selection reports a CPU fallback and both details");

  const auto failed_probe = probe_backend_capabilities(
      [](Backend) -> BackendCapabilityDetail {
        throw std::runtime_error("driver query failed");
      },
      "linux");
  expect(!failed_probe.capabilities.cpu && !failed_probe.capabilities.cuda_13_2,
         "probe exceptions do not claim a backend is available");
  expect(backend_capability_detail(failed_probe, Backend::cpu).reason.find(
             "driver query failed") != std::string::npos,
         "probe exception is retained in capability details");

  RuntimeRequest request;
  request.model_path = "C:/models/tiny.model";
  request.requested_backend = Backend::cpu;
  request.backend_probe = cpu_only_probe;
  request.language = "it";

  WhisperRuntimeFactory no_adapter;
  const auto unavailable = no_adapter.create(request);
  expect(unavailable.status == RuntimeStatus::unavailable &&
             unavailable.runtime == nullptr,
         "factory stays unavailable without an injected adapter");
  expect(unavailable.message.find("no runtime adapter") != std::string::npos,
         "factory reports the missing adapter clearly");

  std::vector<WhisperProcessRequest> bridge_requests;
  OpenAIWhisperRuntimeConfig bridge_config;
  bridge_config.python_executable = "python3";
  bridge_config.bridge_script_path = "runtime/openai_whisper_bridge.py";
  bridge_config.runner = [&](const WhisperProcessRequest& bridge_request) {
    bridge_requests.push_back(bridge_request);
    WhisperProcessResult result;
    result.started = true;
    result.exit_code = -1;
    if (bridge_request.input_line.find("\"op\":\"initialize\"") !=
        std::string::npos) {
      result.output_lines = {"{\"status\":\"ready\"}"};
    } else {
      result.output_lines = {
          "{\"status\":\"ready\",\"segments\":[{\"start_seconds\":0.25,"
          "\"end_seconds\":0.75,\"text\":\"ciao\"}]}"};
    }
    return result;
  };
  WhisperRuntimeFactory openai_factory(
      make_openai_whisper_adapter(bridge_config));
  auto openai_ready = openai_factory.create(request);
  expect(openai_ready.status == RuntimeStatus::ready &&
             openai_ready.runtime != nullptr && bridge_requests.size() == 1 &&
             bridge_requests.front().command ==
                 std::vector<std::string>{"python3",
                                          "runtime/openai_whisper_bridge.py"} &&
             bridge_requests.front().input_line.find("\"model_path\":\"C:/models/tiny.model\"") !=
                 std::string::npos,
         "OpenAI adapter initializes through the injected JSON-lines runner");
  const float audio[] = {0.0F, 0.5F, -0.5F};
  const auto openai_transcript =
      openai_ready.runtime->transcribe(audio, std::size(audio), 48'000);
  expect(bridge_requests.size() == 2 && openai_transcript.size() == 1 &&
             openai_transcript.front().start_frame == 12'000 &&
             openai_transcript.front().end_frame == 36'000 &&
             openai_transcript.front().text == "ciao" &&
             bridge_requests.back().input_line.find("\"op\":\"transcribe\"") !=
                 std::string::npos,
         "OpenAI adapter serializes audio and converts bridge seconds to frames");

  OpenAIWhisperRuntime no_runner(OpenAIWhisperRuntimeConfig{});
  expect(no_runner.initialize("model.bin") == RuntimeStatus::unavailable &&
             no_runner.last_error().find("runner was not injected") !=
                 std::string::npos,
         "OpenAI adapter remains unavailable without a host runner");

  OpenAIWhisperRuntime invalid_transcript(OpenAIWhisperRuntimeConfig{
      "python", "bridge.py",
      [](const WhisperProcessRequest& bridge_request) {
        WhisperProcessResult result;
        result.started = true;
        result.exit_code = -1;
        result.output_lines = {
            bridge_request.input_line.find("initialize") != std::string::npos
                ? "{\"status\":\"ready\"}"
                : "{\"status\":\"ready\",\"segments\":{}}"};
        return result;
      }});
  expect(invalid_transcript.initialize("model.bin", "it") ==
                 RuntimeStatus::ready &&
             invalid_transcript.transcribe(audio, std::size(audio), 48'000)
                     .empty() &&
             invalid_transcript.last_error().find("segments array") !=
                 std::string::npos,
         "OpenAI adapter rejects malformed bridge transcript responses");

  BackendSelection injected_selection;
  std::string injected_model_path;
  WhisperRuntimeFactory injected([&](const RuntimeRequest& received,
                                     const BackendSelection& selection) {
    injected_selection = selection;
    injected_model_path = received.model_path;
    return std::make_unique<TestRuntime>(RuntimeStatus::ready);
  });
  auto ready = injected.create(request);
  expect(ready.status == RuntimeStatus::ready && ready.runtime != nullptr,
         "injected adapter can become ready");
  expect(injected_selection.selected == Backend::cpu &&
             injected_model_path == request.model_path,
         "factory passes backend selection and model path to the adapter");
  expect(ready.message == "Whisper runtime adapter is ready",
         "ready result reports an explicit status message");

  WhisperRuntimeFactory unavailable_adapter(
      [](const RuntimeRequest&, const BackendSelection&) {
        return std::make_unique<TestRuntime>(RuntimeStatus::unavailable);
      });
  const auto rejected = unavailable_adapter.create(request);
  expect(rejected.status == RuntimeStatus::unavailable &&
             rejected.runtime == nullptr &&
             rejected.message.find("unavailable") != std::string::npos,
         "adapter initialization failure remains unavailable");

  WhisperRuntimeFactory null_adapter(
      [](const RuntimeRequest&, const BackendSelection&)
          -> std::unique_ptr<IWhisperRuntime> { return nullptr; });
  const auto null_result = null_adapter.create(request);
  expect(null_result.status == RuntimeStatus::unavailable &&
             null_result.message.find("returned no adapter") !=
                 std::string::npos,
         "null injected adapter is reported without claiming readiness");

  WhisperRuntimeFactory throwing_adapter(
      [](const RuntimeRequest&, const BackendSelection&)
          -> std::unique_ptr<IWhisperRuntime> {
        throw std::runtime_error("adapter unavailable");
      });
  const auto error = throwing_adapter.create(request);
  expect(error.status == RuntimeStatus::error &&
             error.message.find("adapter unavailable") != std::string::npos,
         "adapter exceptions become an explicit runtime error");

  RuntimeRequest no_backend_request = request;
  no_backend_request.backend_probe = failed_probe;
  const auto no_backend = injected.create(no_backend_request);
  expect(no_backend.status == RuntimeStatus::unavailable &&
             no_backend.runtime == nullptr &&
             no_backend.message.find("No backend is available") !=
                 std::string::npos,
         "factory rejects an unavailable backend before adapter creation");

  const auto catalog = obs_whisperbleep::model::default_catalog();
  const auto* multilingual_model =
      catalog.find(obs_whisperbleep::model::ModelId::tiny);
  const auto* english_model =
      catalog.find(obs_whisperbleep::model::ModelId::tiny_en);
  expect(multilingual_model != nullptr && english_model != nullptr &&
             !multilingual_model->english_only && english_model->english_only,
         "runtime tests use distinct multilingual and English-only metadata");

  const auto multilingual_policy =
      validate_language_policy(*multilingual_model, "it-IT");
  expect(multilingual_policy.accepted() &&
             multilingual_policy.normalized_language == "it-IT",
         "multilingual models accept an explicit non-English language");

  const auto english_policy = validate_language_policy(*english_model, "EN-us");
  expect(english_policy.accepted() &&
             english_policy.normalized_language == "en",
         "English-only models normalize English language tags to en");

  const auto rejected_policy = validate_language_policy(*english_model, "it");
  expect(!rejected_policy.accepted() &&
             rejected_policy.status == RuntimeLanguageStatus::english_only_conflict,
         "English-only models reject non-English language requests");

  RuntimeRequest english_request = request;
  english_request.model_id = obs_whisperbleep::model::ModelId::tiny_en;
  english_request.language = "en-US";
  int language_factory_calls = 0;
  std::string initialized_language;
  WhisperRuntimeFactory language_aware_factory(
      [&](const RuntimeRequest& received, const BackendSelection&) {
        ++language_factory_calls;
        initialized_language = received.language;
        return std::make_unique<TestRuntime>(RuntimeStatus::ready);
      });
  const auto english_ready = language_aware_factory.create(english_request);
  expect(english_ready.status == RuntimeStatus::ready &&
             language_factory_calls == 1 && initialized_language == "en",
         "factory passes the normalized English-only policy to the adapter");

  RuntimeRequest invalid_english_request = english_request;
  invalid_english_request.language = "it-IT";
  const auto invalid_english =
      language_aware_factory.create(invalid_english_request);
  expect(invalid_english.status == RuntimeStatus::error &&
             invalid_english.runtime == nullptr && language_factory_calls == 1 &&
             invalid_english.message.find("English-only") != std::string::npos,
         "factory rejects an incoherent English-only request before creation");

  RuntimeRequest auto_english_request = english_request;
  auto_english_request.language = "auto";
  const auto auto_english = language_aware_factory.create(auto_english_request);
  expect(auto_english.status == RuntimeStatus::error &&
             language_factory_calls == 1,
         "English-only models reject automatic language detection");

  return EXIT_SUCCESS;
}
