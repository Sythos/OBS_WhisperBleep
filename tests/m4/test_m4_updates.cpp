// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "obs_whisperbleep/updates/update_checker.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M4 update test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::updates;

  std::string requested_url;
  UpdateChecker newer([&](const std::string_view url, std::string& body,
                          std::string&) {
    requested_url = std::string(url);
    body = R"({"tag_name":"v0.2.0","name":"0.2.0"})";
    return true;
  });
  const auto newer_result = newer.check("0.1.0");
  expect(newer_result.status == UpdateStatus::update_available,
         "detects a newer release");
  expect(newer_result.latest_version == "0.2.0" &&
             newer_result.latest_tag == "v0.2.0",
         "normalizes and preserves the release version");
  expect(requested_url == UpdateChecker::latest_release_api_url(),
         "requests the canonical latest-release API endpoint");
  expect(newer_result.release_url == UpdateChecker::releases_url(),
         "returns the external GitHub releases URL");

  const auto italian_result = newer.check("0.1.0", "it-IT");
  expect(italian_result.message == "È disponibile una nuova release GitHub",
         "localizes update status messages");

  UpdateChecker current([](std::string_view, std::string& body, std::string&) {
    body = R"({"tag_name":"0.1.0"})";
    return true;
  });
  const auto current_result = current.check("v0.1.0");
  expect(current_result.status == UpdateStatus::up_to_date,
         "accepts an optional v prefix and detects an equal version");

  UpdateChecker prerelease([](std::string_view, std::string& body,
                              std::string&) {
    body = R"({"tag_name":"v1.0.0-rc.1"})";
    return true;
  });
  expect(prerelease.check("1.0.0-beta.2").status ==
             UpdateStatus::update_available,
         "compares semantic prerelease identifiers");
  expect(prerelease.check("1.0.0").status == UpdateStatus::up_to_date,
         "does not treat a prerelease as newer than a stable version");

  UpdateChecker unavailable([](std::string_view, std::string&,
                               std::string& error) {
    error = "offline";
    return false;
  });
  const auto unavailable_result = unavailable.check("0.1.0");
  expect(unavailable_result.status == UpdateStatus::network_error &&
             unavailable_result.message == "offline",
         "reports a transport failure without blocking the caller");

  UpdateChecker malformed([](std::string_view, std::string& body,
                             std::string&) {
    body = R"({"name":"missing tag"})";
    return true;
  });
  expect(malformed.check("0.1.0").status == UpdateStatus::invalid_response,
         "rejects a response without a release tag");

  UpdateChecker bad_tag([](std::string_view, std::string& body, std::string&) {
    body = R"({"tag_name":"not-a-version"})";
    return true;
  });
  expect(bad_tag.check("0.1.0").status == UpdateStatus::invalid_response,
         "rejects a non-semantic release tag");

  UpdateChecker no_transport;
  const auto no_transport_result = no_transport.check("0.1.0");
  expect(no_transport_result.status == UpdateStatus::network_error,
         "reports a missing transport as a network error");

  expect(newer.check("invalid").status == UpdateStatus::invalid_response,
         "rejects an invalid installed version before network access");

  return EXIT_SUCCESS;
}
