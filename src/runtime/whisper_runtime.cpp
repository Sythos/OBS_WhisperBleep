// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/whisper_runtime.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
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

std::string escape_json_string(const std::string_view value) {
  std::ostringstream result;
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        result << "\\\"";
        break;
      case '\\':
        result << "\\\\";
        break;
      case '\b':
        result << "\\b";
        break;
      case '\f':
        result << "\\f";
        break;
      case '\n':
        result << "\\n";
        break;
      case '\r':
        result << "\\r";
        break;
      case '\t':
        result << "\\t";
        break;
      default:
        if (character < 0x20U) {
          result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(character) << std::dec
                 << std::setfill(' ');
        } else {
          result << static_cast<char>(character);
        }
    }
  }
  return result.str();
}

struct JsonValue {
  enum class Type { null_value, boolean, number, string, array, object };

  Type type = Type::null_value;
  bool boolean_value = false;
  double number_value = 0.0;
  std::string string_value;
  std::vector<JsonValue> array_value;
  std::map<std::string, JsonValue> object_value;
};

class JsonParser final {
 public:
  explicit JsonParser(const std::string_view input) : input_(input) {}

  [[nodiscard]] JsonValue parse() {
    JsonValue value = parse_value();
    skip_whitespace();
    if (position_ != input_.size()) {
      throw std::runtime_error("unexpected trailing JSON data");
    }
    return value;
  }

 private:
  [[nodiscard]] JsonValue parse_value() {
    skip_whitespace();
    if (position_ == input_.size()) {
      throw std::runtime_error("expected a JSON value");
    }
    switch (input_[position_]) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"': {
        JsonValue value;
        value.type = JsonValue::Type::string;
        value.string_value = parse_string();
        return value;
      }
      case 't':
        consume_literal("true");
        return JsonValue{JsonValue::Type::boolean, true};
      case 'f':
        consume_literal("false");
        return JsonValue{JsonValue::Type::boolean, false};
      case 'n':
        consume_literal("null");
        return JsonValue{};
      default:
        return parse_number();
    }
  }

  [[nodiscard]] JsonValue parse_object() {
    expect('{');
    JsonValue object;
    object.type = JsonValue::Type::object;
    skip_whitespace();
    if (consume_if('}')) {
      return object;
    }
    while (true) {
      skip_whitespace();
      if (position_ == input_.size() || input_[position_] != '"') {
        throw std::runtime_error("expected a JSON object key");
      }
      const std::string key = parse_string();
      skip_whitespace();
      expect(':');
      object.object_value.insert_or_assign(key, parse_value());
      skip_whitespace();
      if (consume_if('}')) {
        return object;
      }
      expect(',');
    }
  }

  [[nodiscard]] JsonValue parse_array() {
    expect('[');
    JsonValue array;
    array.type = JsonValue::Type::array;
    skip_whitespace();
    if (consume_if(']')) {
      return array;
    }
    while (true) {
      array.array_value.push_back(parse_value());
      skip_whitespace();
      if (consume_if(']')) {
        return array;
      }
      expect(',');
    }
  }

  [[nodiscard]] JsonValue parse_number() {
    const char* begin = input_.data() + position_;
    char* end = nullptr;
    const double parsed = std::strtod(begin, &end);
    if (end == begin) {
      throw std::runtime_error("expected a JSON number");
    }
    position_ += static_cast<std::size_t>(end - begin);
    JsonValue value;
    value.type = JsonValue::Type::number;
    value.number_value = parsed;
    return value;
  }

  [[nodiscard]] std::string parse_string() {
    expect('"');
    std::string result;
    while (position_ < input_.size()) {
      const char character = input_[position_++];
      if (character == '"') {
        return result;
      }
      if (static_cast<unsigned char>(character) < 0x20U) {
        throw std::runtime_error("unescaped control character in JSON string");
      }
      if (character != '\\') {
        result.push_back(character);
        continue;
      }
      if (position_ == input_.size()) {
        throw std::runtime_error("incomplete JSON string escape");
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          append_unicode_escape(result);
          break;
        default:
          throw std::runtime_error("invalid JSON string escape");
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  void append_unicode_escape(std::string& result) {
    if (position_ + 4 > input_.size()) {
      throw std::runtime_error("incomplete JSON unicode escape");
    }
    unsigned int codepoint = 0;
    for (int index = 0; index != 4; ++index) {
      const char character = input_[position_++];
      codepoint *= 16U;
      if (character >= '0' && character <= '9') {
        codepoint += static_cast<unsigned int>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        codepoint += static_cast<unsigned int>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        codepoint += static_cast<unsigned int>(character - 'A' + 10);
      } else {
        throw std::runtime_error("invalid JSON unicode escape");
      }
    }
    if (codepoint <= 0x7FU) {
      result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
      result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
      result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
      result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
      result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
  }

  void consume_literal(const std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      throw std::runtime_error("invalid JSON literal");
    }
    position_ += literal.size();
  }

  void skip_whitespace() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
      ++position_;
    }
  }

  void expect(const char expected) {
    skip_whitespace();
    if (position_ == input_.size() || input_[position_] != expected) {
      throw std::runtime_error("unexpected JSON token");
    }
    ++position_;
  }

  [[nodiscard]] bool consume_if(const char expected) {
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

const JsonValue* object_member(const JsonValue& object,
                               const std::string_view name) {
  if (object.type != JsonValue::Type::object) {
    return nullptr;
  }
  const auto found = object.object_value.find(std::string(name));
  return found == object.object_value.end() ? nullptr : &found->second;
}

const std::string* string_member(const JsonValue& object,
                                 const std::string_view name) {
  const JsonValue* value = object_member(object, name);
  if (value == nullptr || value->type != JsonValue::Type::string) {
    return nullptr;
  }
  return &value->string_value;
}

const JsonValue* parse_bridge_response(const WhisperProcessResult& result,
                                       std::string& error) {
  if (!result.started) {
    error = "OpenAI Whisper bridge process did not start";
    return nullptr;
  }
  if (result.exit_code > 0) {
    error = "OpenAI Whisper bridge process exited with code " +
            std::to_string(result.exit_code);
    if (!result.error_output.empty()) {
      error += ": " + result.error_output;
    }
    return nullptr;
  }
  if (result.output_lines.empty()) {
    error = "OpenAI Whisper bridge returned no JSON-lines response";
    return nullptr;
  }

  try {
    static thread_local JsonValue response;
    response = JsonParser(result.output_lines.back()).parse();
    if (response.type != JsonValue::Type::object) {
      error = "OpenAI Whisper bridge response must be a JSON object";
      return nullptr;
    }
    return &response;
  } catch (const std::exception& exception) {
    error = std::string("OpenAI Whisper bridge returned invalid JSON: ") +
            exception.what();
    return nullptr;
  }
}

bool bridge_ready(const JsonValue& response, std::string& error) {
  const std::string* status = string_member(response, "status");
  if (status == nullptr) {
    error = "OpenAI Whisper bridge response has no string status";
    return false;
  }
  if (*status == "ready") {
    return true;
  }
  const std::string* message = string_member(response, "message");
  error = "OpenAI Whisper bridge is " + *status;
  if (message != nullptr && !message->empty()) {
    error += ": " + *message;
  }
  return false;
}

std::string make_initialize_request(const std::string_view model_path,
                                    const std::string_view language) {
  return "{\"op\":\"initialize\",\"model_path\":\"" +
         escape_json_string(model_path) + "\",\"language\":\"" +
         escape_json_string(language) + "\"}";
}

std::string make_transcribe_request(const float* samples,
                                    const std::size_t sample_count,
                                    const std::uint32_t sample_rate) {
  std::ostringstream request;
  request.imbue(std::locale::classic());
  request << "{\"op\":\"transcribe\",\"sample_rate\":" << sample_rate
          << ",\"samples\":[" << std::setprecision(std::numeric_limits<float>::max_digits10);
  for (std::size_t index = 0; index < sample_count; ++index) {
    if (index != 0) {
      request << ',';
    }
    request << samples[index];
  }
  request << "]}";
  return request.str();
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

OpenAIWhisperRuntime::OpenAIWhisperRuntime(OpenAIWhisperRuntimeConfig config)
    : config_(std::move(config)) {}

RuntimeStatus OpenAIWhisperRuntime::initialize(
    const std::string_view model_path) {
  return initialize(model_path, "auto");
}

RuntimeStatus OpenAIWhisperRuntime::initialize(
    const std::string_view model_path, const std::string_view language) {
  initialized_ = false;
  last_error_.clear();
  if (model_path.empty()) {
    last_error_ = "OpenAI Whisper bridge requires a model path";
    return RuntimeStatus::error;
  }
  if (language.empty()) {
    last_error_ = "OpenAI Whisper bridge requires a language policy";
    return RuntimeStatus::error;
  }
  if (!config_.runner) {
    last_error_ = "OpenAI Whisper bridge runner was not injected";
    return RuntimeStatus::unavailable;
  }
  if (config_.python_executable.empty() || config_.bridge_script_path.empty()) {
    last_error_ = "OpenAI Whisper bridge command is incomplete";
    return RuntimeStatus::unavailable;
  }

  WhisperProcessResult result;
  try {
    result = run(make_initialize_request(model_path, language));
  } catch (const std::exception& exception) {
    last_error_ = std::string("OpenAI Whisper bridge runner failed: ") +
                  exception.what();
    return RuntimeStatus::error;
  } catch (...) {
    last_error_ = "OpenAI Whisper bridge runner failed with an unknown error";
    return RuntimeStatus::error;
  }

  const JsonValue* response = parse_bridge_response(result, last_error_);
  if (response == nullptr) {
    return result.started ? RuntimeStatus::error : RuntimeStatus::unavailable;
  }
  if (!bridge_ready(*response, last_error_)) {
    const std::string* status = string_member(*response, "status");
    return status != nullptr && *status == "unavailable"
               ? RuntimeStatus::unavailable
               : RuntimeStatus::error;
  }

  language_ = std::string(language);
  initialized_ = true;
  return RuntimeStatus::ready;
}

std::vector<TranscriptSegment> OpenAIWhisperRuntime::transcribe(
    const float* samples, const std::size_t sample_count,
    const std::uint32_t sample_rate) {
  last_error_.clear();
  if (!initialized_) {
    last_error_ = "OpenAI Whisper bridge was not initialized";
    return {};
  }
  if (sample_rate == 0) {
    last_error_ = "OpenAI Whisper bridge requires a positive sample rate";
    return {};
  }
  if (samples == nullptr && sample_count != 0) {
    last_error_ = "OpenAI Whisper bridge received null audio samples";
    return {};
  }
  for (std::size_t index = 0; index < sample_count; ++index) {
    if (!std::isfinite(samples[index])) {
      last_error_ = "OpenAI Whisper bridge received non-finite audio samples";
      return {};
    }
  }

  WhisperProcessResult result;
  try {
    result = run(make_transcribe_request(samples, sample_count, sample_rate));
  } catch (const std::exception& exception) {
    last_error_ = std::string("OpenAI Whisper bridge runner failed: ") +
                  exception.what();
    return {};
  } catch (...) {
    last_error_ = "OpenAI Whisper bridge runner failed with an unknown error";
    return {};
  }

  const JsonValue* response = parse_bridge_response(result, last_error_);
  if (response == nullptr || !bridge_ready(*response, last_error_)) {
    return {};
  }
  const JsonValue* segments = object_member(*response, "segments");
  if (segments == nullptr || segments->type != JsonValue::Type::array) {
    last_error_ = "OpenAI Whisper bridge response has no segments array";
    return {};
  }

  std::vector<TranscriptSegment> transcript;
  transcript.reserve(segments->array_value.size());
  for (const JsonValue& segment : segments->array_value) {
    const JsonValue* start = object_member(segment, "start_seconds");
    const JsonValue* end = object_member(segment, "end_seconds");
    const std::string* text = string_member(segment, "text");
    if (start == nullptr || end == nullptr || text == nullptr ||
        start->type != JsonValue::Type::number ||
        end->type != JsonValue::Type::number ||
        !std::isfinite(start->number_value) ||
        !std::isfinite(end->number_value) || start->number_value < 0.0 ||
        end->number_value < start->number_value) {
      last_error_ = "OpenAI Whisper bridge returned an invalid transcript segment";
      return {};
    }
    const double start_frames = start->number_value * sample_rate;
    const double end_frames = end->number_value * sample_rate;
    if (start_frames >
            static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
        end_frames >
            static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
      last_error_ = "OpenAI Whisper bridge returned an out-of-range segment";
      return {};
    }
    transcript.push_back(
        TranscriptSegment{static_cast<std::int64_t>(std::llround(start_frames)),
                          static_cast<std::int64_t>(std::llround(end_frames)),
                          *text});
  }
  return transcript;
}

const std::string& OpenAIWhisperRuntime::last_error() const noexcept {
  return last_error_;
}

WhisperProcessResult OpenAIWhisperRuntime::run(std::string input_line) const {
  return config_.runner(WhisperProcessRequest{
      {config_.python_executable, config_.bridge_script_path},
      std::move(input_line),
  });
}

RuntimeAdapterFactory make_openai_whisper_adapter(
    OpenAIWhisperRuntimeConfig config) {
  return [config = std::move(config)](const RuntimeRequest&,
                                      const BackendSelection&) mutable {
    return std::make_unique<OpenAIWhisperRuntime>(config);
  };
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
