// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

#include <cctype>
#include <exception>
#include <utility>

namespace obs_whisperbleep::runtime {

namespace {

std::string lowercase_ascii(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<char>(std::tolower(character)));
  }
  return result;
}

bool is_english_language(const std::string_view language) {
  const auto normalized = lowercase_ascii(language);
  return normalized == "en" || normalized.starts_with("en-") ||
         normalized.starts_with("en_");
}

bool is_auto_language(const std::string_view language) {
  return lowercase_ascii(language) == "auto";
}

}  // namespace

const char* runtime_status_name(RuntimeStatus status) noexcept {
  switch (status) {
    case RuntimeStatus::ready:
      return "ready";
    case RuntimeStatus::unavailable:
      return "unavailable";
    case RuntimeStatus::error:
      return "error";
  }
  return "unknown";
}

RuntimeLanguagePolicyResult validate_language_policy(
    const model::ModelDescriptor& model, const std::string_view language) {
  RuntimeLanguagePolicyResult result;
  if (language.empty()) {
    result.status = RuntimeLanguageStatus::invalid;
    result.message = "Whisper language must be an explicit language tag or 'auto'";
    return result;
  }

  for (const unsigned char character : language) {
    if (std::isspace(character) != 0) {
      result.status = RuntimeLanguageStatus::invalid;
      result.message =
          "Whisper language must not contain whitespace characters";
      return result;
    }
  }

  if (model.english_only && !is_english_language(language)) {
    result.status = RuntimeLanguageStatus::english_only_conflict;
    result.message = "English-only Whisper model '" + model.name +
                     "' requires language 'en'";
    return result;
  }

  result.status = RuntimeLanguageStatus::accepted;
  result.normalized_language = is_auto_language(language)
                                   ? "auto"
                                   : (model.english_only ? "en"
                                                          : std::string(language));
  return result;
}

RuntimeStatus IWhisperRuntime::initialize(const std::string_view model_path,
                                          const std::string_view language) {
  (void)language;
  return initialize(model_path);
}

RuntimeStatus StubWhisperRuntime::initialize(std::string_view model_path) {
  (void)model_path;
  return RuntimeStatus::unavailable;
}

std::vector<TranscriptSegment> StubWhisperRuntime::transcribe(
    const float* samples, std::size_t sample_count, std::uint32_t sample_rate) {
  (void)samples;
  (void)sample_count;
  (void)sample_rate;
  return {};
}

WhisperRuntimeFactory::WhisperRuntimeFactory(RuntimeAdapterFactory factory)
    : factory_(std::move(factory)) {}

RuntimeFactoryResult WhisperRuntimeFactory::create(
    const RuntimeRequest& request) const {
  RuntimeFactoryResult result;

  const auto catalog = model::default_catalog();
  const auto* descriptor = catalog.find(request.model_id);
  if (descriptor == nullptr) {
    result.status = RuntimeStatus::error;
    result.message = "Whisper runtime request references an unknown model";
    return result;
  }

  const auto language_policy =
      validate_language_policy(*descriptor, request.language);
  if (!language_policy.accepted()) {
    result.status = RuntimeStatus::error;
    result.message = "Whisper runtime language policy rejected: " +
                     language_policy.message;
    return result;
  }

  RuntimeRequest normalized_request = request;
  normalized_request.language = language_policy.normalized_language;
  result.backend = select_backend_with_probe(request.requested_backend,
                                             request.backend_probe);

  if (!result.backend.selected_available) {
    result.status = RuntimeStatus::unavailable;
    result.message = "Whisper runtime unavailable: " +
                     result.backend.message;
    return result;
  }

  if (!factory_) {
    result.status = RuntimeStatus::unavailable;
    result.message =
        "Whisper runtime unavailable: no runtime adapter was injected";
    return result;
  }

  std::unique_ptr<IWhisperRuntime> adapter;
  try {
    adapter = factory_(normalized_request, result.backend);
  } catch (const std::exception& error) {
    result.status = RuntimeStatus::error;
    result.message = std::string("Whisper runtime adapter creation failed: ") +
                     error.what();
    return result;
  } catch (...) {
    result.status = RuntimeStatus::error;
    result.message =
        "Whisper runtime adapter creation failed with an unknown error";
    return result;
  }

  if (!adapter) {
    result.status = RuntimeStatus::unavailable;
    result.message =
        "Whisper runtime unavailable: injected factory returned no adapter";
    return result;
  }

  RuntimeStatus initialization_status = RuntimeStatus::error;
  try {
    initialization_status =
        adapter->initialize(normalized_request.model_path,
                            normalized_request.language);
  } catch (const std::exception& error) {
    result.status = RuntimeStatus::error;
    result.message = std::string("Whisper runtime initialization failed: ") +
                     error.what();
    return result;
  } catch (...) {
    result.status = RuntimeStatus::error;
    result.message =
        "Whisper runtime initialization failed with an unknown error";
    return result;
  }

  result.status = initialization_status;
  if (initialization_status == RuntimeStatus::ready) {
    result.runtime = std::move(adapter);
    result.message = "Whisper runtime adapter is ready";
    return result;
  }

  result.message = std::string("Whisper runtime adapter is ") +
                   runtime_status_name(initialization_status);
  return result;
}

}  // namespace obs_whisperbleep::runtime
