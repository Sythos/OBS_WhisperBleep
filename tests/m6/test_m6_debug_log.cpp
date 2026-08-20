// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include "obs_whisperbleep/diagnostics/debug_log.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M6 debug log test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::diagnostics;

  const auto root = std::filesystem::temp_directory_path() /
                    "obs_whisperbleep_m6_debug_log_test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  expect(!error, "creates an isolated test home directory");

  const auto disabled = DebugLog::open(DebugLogOptions{false, root, "20260820"});
  expect(disabled.status() == DebugLogStatus::disabled,
         "debug logging is disabled by default");
  expect(disabled.path().empty() &&
             !std::filesystem::exists(root / "WhisperBleep_20260820_001.log"),
         "disabled logging creates no file");

  {
    auto first =
        DebugLog::open(DebugLogOptions{true, root, "20260820"});
    expect(first.ready(), "enabled logging creates the first file");
    expect(first.path().filename() == "WhisperBleep_20260820_001.log",
           "the first sequence is zero-padded");
    expect(first.write_line("startup", "model=small"),
           "writes a diagnostic event");

    const auto first_content = read_file(first.path());
    expect(first_content.find("debug_log | enabled") != std::string::npos &&
               first_content.find("startup | model=small") != std::string::npos,
           "the log contains safe diagnostic text");

    const auto second =
        DebugLog::open(DebugLogOptions{true, root, "20260820"});
    expect(second.ready() &&
               second.path().filename() == "WhisperBleep_20260820_002.log",
           "the second daily file increments the sequence");

    {
      std::ofstream sentinel(root / "WhisperBleep_20260820_003.log",
                             std::ios::binary);
      sentinel << "do not overwrite";
    }
    auto third =
        DebugLog::open(DebugLogOptions{true, root, "20260820"});
    expect(third.ready() &&
               third.path().filename() == "WhisperBleep_20260820_004.log",
           "existing files are skipped without overwrite");
    expect(read_file(root / "WhisperBleep_20260820_003.log") ==
               "do not overwrite",
           "existing log content is preserved");

    expect(third.write_line("configuration",
                            "api_key=secret password=hunter2 backend=cpu"),
           "writes a sanitized diagnostic event");
    const auto sanitized = read_file(third.path());
    expect(sanitized.find("secret") == std::string::npos &&
               sanitized.find("hunter2") == std::string::npos &&
               sanitized.find("api_key=[redacted]") != std::string::npos &&
               sanitized.find("password=[redacted]") != std::string::npos,
           "sensitive key values are redacted");

    const auto invalid =
        DebugLog::open(DebugLogOptions{true, root, "2026-08-20"});
    expect(invalid.status() == DebugLogStatus::failed && !invalid.ready(),
           "invalid dates are rejected");
  }

  std::filesystem::remove_all(root, error);
  return EXIT_SUCCESS;
}
