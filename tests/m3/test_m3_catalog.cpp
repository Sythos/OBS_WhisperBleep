// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "obs_whisperbleep/model/model_catalog.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M3 catalog test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::model;

  const auto catalog = default_catalog();
  expect(catalog.models().size() == 6, "catalog contains six model ids");
  expect(catalog.metadata().version == "20250625",
         "catalog records the upstream version");
  expect(catalog.metadata().source_url == "https://github.com/openai/whisper",
         "catalog records the upstream source");
  expect(catalog.metadata().manifest_url.find("__init__.py") !=
             std::string::npos,
         "catalog records the upstream manifest");
  expect(!catalog.metadata().compatibility.empty(),
         "catalog records format compatibility metadata");

  for (const auto id : {ModelId::tiny, ModelId::base, ModelId::small,
                        ModelId::medium, ModelId::large, ModelId::turbo}) {
    const auto* model = catalog.find(id);
    expect(model != nullptr, "catalog finds every required model");
    expect(model->license == "MIT", "catalog records the model license");
    expect(model->source_url.rfind("https://openaipublic.azureedge.net/", 0) ==
               0,
           "catalog uses the official model host");
    expect(model->sha256.size() == 64, "catalog records a SHA-256 checksum");
    expect(!model->expected_size_bytes.has_value(),
           "catalog does not invent an unverified byte size");
    expect(model->format == "openai-whisper-pytorch-checkpoint",
           "catalog records the required model format");
  }

  return EXIT_SUCCESS;
}
