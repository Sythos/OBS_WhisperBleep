// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace obs_whisperbleep::diagnostics {

/** Options used to create the per-user diagnostic log. */
struct DebugLogOptions {
  /** Logging is disabled by default and must be explicitly enabled. */
  bool enabled = false;

  /** The user's home directory. The caller supplies this for testability. */
  std::filesystem::path user_home;

  /** Optional local date override in the strict YYYYMMDD form. */
  std::string date_yyyymmdd;
};

enum class DebugLogStatus { disabled, ready, failed };

/**
 * Owns one uniquely-created diagnostic log file.
 *
 * A log is created directly in the supplied user home directory as
 * WhisperBleep_yyyymmdd_xxx.log. The file is reserved with exclusive-create
 * semantics, so an existing daily log is never overwritten. Diagnostic lines
 * are sanitized before writing and sensitive key/value fields are redacted.
 */
class DebugLog final {
 public:
  DebugLog() noexcept;
  DebugLog(const DebugLog&) = delete;
  DebugLog& operator=(const DebugLog&) = delete;
  DebugLog(DebugLog&& other) noexcept;
  DebugLog& operator=(DebugLog&& other) noexcept;
  ~DebugLog();

  [[nodiscard]] static DebugLog open(const DebugLogOptions& options);

  [[nodiscard]] DebugLogStatus status() const noexcept { return status_; }
  [[nodiscard]] bool enabled() const noexcept {
    return status_ == DebugLogStatus::ready;
  }
  [[nodiscard]] bool ready() const noexcept {
    return status_ == DebugLogStatus::ready;
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  /** Writes one sanitized, timestamp-free diagnostic event. */
  [[nodiscard]] bool write_line(std::string_view event,
                                std::string_view details = {});

 private:
  struct Stream;

  DebugLog(DebugLogStatus status, std::filesystem::path path,
           std::string error, std::unique_ptr<Stream> stream) noexcept;

  DebugLogStatus status_ = DebugLogStatus::disabled;
  std::filesystem::path path_;
  std::string error_;
  std::unique_ptr<Stream> stream_;
};

}  // namespace obs_whisperbleep::diagnostics
