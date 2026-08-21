// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/runtime/persistent_process_runner.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M7 process runner test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main(int argc, char** argv) {
  expect(argc == 2, "receives the portable process helper path");

  using namespace obs_whisperbleep::runtime;
  PersistentWhisperProcessRunnerConfig config;
  config.response_timeout = std::chrono::seconds(5);
  config.shutdown_timeout = std::chrono::milliseconds(250);
  PersistentWhisperProcessRunner runner(config);

  const std::vector<std::string> command{argv[1]};
  const WhisperProcessResult first = runner({command, "first"});
  expect(first.started && first.exit_code == -1 && first.output_lines.size() == 1,
         "starts one persistent child and reads its first response");
  expect(first.output_lines.front().find("\"request_count\":1") !=
             std::string::npos,
         "first request is handled by the child");

  const WhisperProcessResult second = runner({command, "second"});
  expect(second.started && second.exit_code == -1 && second.output_lines.size() == 1,
         "reuses the child for a second request");
  expect(second.output_lines.front().find("\"request_count\":2") !=
             std::string::npos,
         "second request reaches the same child process");
  expect(runner.running(), "reports the child as alive between requests");

  const WhisperProcessResult changed = runner({{"different-command"}, "third"});
  expect(!changed.started && !changed.error_output.empty(),
         "rejects a command change without replacing a live child");

  runner.stop();
  expect(!runner.running(), "stops the child explicitly");
  return EXIT_SUCCESS;
}
