// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/diagnostics/debug_log.hpp"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using obs_whisperbleep::diagnostics::DebugLog;

[[nodiscard]] bool is_date(const std::string& value) {
  if (value.size() != 8) {
    return false;
  }
  for (const unsigned char character : value) {
    if (!std::isdigit(character)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string local_date_yyyymmdd() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#if defined(_WIN32)
  if (localtime_s(&local_time, &now) != 0) {
    return {};
  }
#else
  if (localtime_r(&now, &local_time) == nullptr) {
    return {};
  }
#endif

  char buffer[9]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y%m%d", &local_time) == 0) {
    return {};
  }
  return buffer;
}

[[nodiscard]] int open_exclusive(const std::filesystem::path& path) {
#if defined(_WIN32)
  int descriptor = -1;
  const errno_t result = _wsopen_s(
      &descriptor, path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
      _SH_DENYNO, _S_IREAD | _S_IWRITE);
  if (result != 0) {
    errno = result;
    return -1;
  }
  return descriptor;
#else
  return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#endif
}

[[nodiscard]] std::FILE* stream_from_descriptor(const int descriptor) {
#if defined(_WIN32)
  return _fdopen(descriptor, "wb");
#else
  return ::fdopen(descriptor, "w");
#endif
}

void close_descriptor(const int descriptor) noexcept {
#if defined(_WIN32)
  _close(descriptor);
#else
  ::close(descriptor);
#endif
}

[[nodiscard]] std::string lower_copy(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<char>(std::tolower(character)));
  }
  return result;
}

[[nodiscard]] bool is_sensitive_key(std::string_view key) {
  const auto lower = lower_copy(key);
  return lower == "api_key" || lower == "apikey" || lower == "api-token" ||
         lower == "token" || lower == "access_token" || lower == "password" ||
         lower == "passwd" || lower == "secret" || lower == "authorization" ||
         lower == "cookie";
}

[[nodiscard]] std::string sanitize(std::string_view value) {
  std::string result;
  result.reserve(value.size());

  std::size_t index = 0;
  while (index < value.size()) {
    const auto key_start = index;
    while (index < value.size() &&
           (std::isalnum(static_cast<unsigned char>(value[index])) != 0 ||
            value[index] == '_' || value[index] == '-')) {
      ++index;
    }

    const auto key_end = index;
    while (index < value.size() &&
           (value[index] == ' ' || value[index] == '\t')) {
      ++index;
    }
    if (key_end > key_start && index < value.size() &&
        (value[index] == '=' || value[index] == ':') &&
        is_sensitive_key(value.substr(key_start, key_end - key_start))) {
      result.append(value.substr(key_start, key_end - key_start));
      result.append(value.substr(key_end, index - key_end));
      result.append(1, value[index]);
      ++index;
      while (index < value.size() &&
             (value[index] == ' ' || value[index] == '\t')) {
        ++index;
      }
      result.append("[redacted]");
      if (index < value.size() &&
          (value[index] == '\'' || value[index] == '"')) {
        const char quote = value[index++];
        while (index < value.size() && value[index] != quote) {
          ++index;
        }
        if (index < value.size()) {
          ++index;
        }
      } else {
        while (index < value.size() && value[index] != ' ' &&
               value[index] != '\t' && value[index] != ',' &&
               value[index] != ';') {
          ++index;
        }
      }
      continue;
    }

    index = key_start;
    const auto character = static_cast<unsigned char>(value[index]);
    if (character == '\r' || character == '\n' || character == '\t') {
      result.push_back(' ');
    } else if (std::isprint(character) != 0) {
      result.push_back(static_cast<char>(character));
    } else {
      result.push_back('?');
    }
    ++index;
  }

  constexpr std::size_t kMaximumLineLength = 2048;
  if (result.size() > kMaximumLineLength) {
    result.resize(kMaximumLineLength);
    result.append("...");
  }
  return result;
}

}  // namespace

namespace obs_whisperbleep::diagnostics {

struct DebugLog::Stream {
  std::FILE* file = nullptr;
  mutable std::mutex mutex;

  ~Stream() {
    if (file != nullptr) {
      std::fclose(file);
    }
  }
};

[[nodiscard]] DebugLog DebugLog::open(const DebugLogOptions& options) {
  if (!options.enabled) {
    return DebugLog(DebugLogStatus::disabled, {}, {}, nullptr);
  }
  if (options.user_home.empty()) {
    return DebugLog(DebugLogStatus::failed, {},
                    "The debug log user home directory is empty.", nullptr);
  }

  const auto date = options.date_yyyymmdd.empty() ? local_date_yyyymmdd()
                                                   : options.date_yyyymmdd;
  if (!is_date(date)) {
    return DebugLog(DebugLogStatus::failed, {},
                    "The debug log date must use YYYYMMDD.", nullptr);
  }

  std::error_code error;
  if (!std::filesystem::is_directory(options.user_home, error)) {
    return DebugLog(DebugLogStatus::failed, {},
                    "The debug log user home directory is unavailable.",
                    nullptr);
  }

  for (unsigned int sequence = 1; sequence <= 999; ++sequence) {
    char suffix[4]{};
    std::snprintf(suffix, sizeof(suffix), "%03u", sequence);
    const auto candidate = options.user_home /
                           ("WhisperBleep_" + date + "_" + suffix + ".log");
    const int descriptor = open_exclusive(candidate);
    if (descriptor < 0) {
      if (errno == EEXIST) {
        continue;
      }
      return DebugLog(DebugLogStatus::failed, {},
                      "Unable to create the debug log file: " +
                          std::strerror(errno),
                      nullptr);
    }

    auto* file = stream_from_descriptor(descriptor);
    if (file == nullptr) {
      close_descriptor(descriptor);
      std::filesystem::remove(candidate, error);
      return DebugLog(DebugLogStatus::failed, {},
                      "Unable to open the debug log stream.", nullptr);
    }

    auto stream = std::make_unique<Stream>();
    stream->file = file;
    DebugLog logger(DebugLogStatus::ready, candidate, {}, std::move(stream));
    if (!logger.write_line("debug_log", "enabled")) {
      logger.stream_.reset();
      std::filesystem::remove(candidate, error);
      return DebugLog(DebugLogStatus::failed, {},
                      "Unable to write the debug log header.", nullptr);
    }
    return logger;
  }

  return DebugLog(DebugLogStatus::failed, {},
                  "No daily debug log sequence is available.", nullptr);
}

DebugLog::DebugLog(DebugLogStatus status, std::filesystem::path path,
                   std::string error, std::unique_ptr<Stream> stream) noexcept
    : status_(status),
      path_(std::move(path)),
      error_(std::move(error)),
      stream_(std::move(stream)) {}

DebugLog::DebugLog(DebugLog&& other) noexcept
    : status_(other.status_),
      path_(std::move(other.path_)),
      error_(std::move(other.error_)),
      stream_(std::move(other.stream_)) {
  other.status_ = DebugLogStatus::disabled;
}

DebugLog& DebugLog::operator=(DebugLog&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  status_ = other.status_;
  path_ = std::move(other.path_);
  error_ = std::move(other.error_);
  stream_ = std::move(other.stream_);
  other.status_ = DebugLogStatus::disabled;
  return *this;
}

DebugLog::~DebugLog() = default;

bool DebugLog::write_line(const std::string_view event,
                          const std::string_view details) {
  if (!ready() || stream_ == nullptr || stream_->file == nullptr) {
    return false;
  }

  const auto safe_event = sanitize(event);
  const auto safe_details = sanitize(details);
  const std::string line = safe_details.empty()
                               ? safe_event + "\n"
                               : safe_event + " | " + safe_details + "\n";
  std::lock_guard lock(stream_->mutex);
  const auto written = std::fwrite(line.data(), 1, line.size(), stream_->file);
  return written == line.size() && std::fflush(stream_->file) == 0;
}

}  // namespace obs_whisperbleep::diagnostics
