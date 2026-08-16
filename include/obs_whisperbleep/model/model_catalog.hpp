// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obs_whisperbleep::model {

enum class ModelId { tiny, base, small, medium, large, turbo };

struct ModelDescriptor {
  ModelId id = ModelId::tiny;
  std::string name;
  std::string source_url;
  std::string sha256;
  std::string license = "unverified";
  std::uint64_t minimum_memory_mb = 0;
  // An empty value means the upstream manifest does not publish a verified
  // byte size for this model.
  std::optional<std::uint64_t> expected_size_bytes;
  std::string format = "openai-whisper-pytorch-checkpoint";
};

struct ModelCatalogMetadata {
  std::string version;
  std::string source_url;
  std::string manifest_url;
  std::string compatibility;
};

[[nodiscard]] const char* model_id_name(ModelId id) noexcept;
[[nodiscard]] std::optional<ModelId> model_id_from_name(std::string_view name);

class ModelCatalog {
 public:
  ModelCatalog() = default;
  explicit ModelCatalog(std::vector<ModelDescriptor> models,
                        ModelCatalogMetadata metadata = {});

  [[nodiscard]] const std::vector<ModelDescriptor>& models() const noexcept;
  [[nodiscard]] const ModelCatalogMetadata& metadata() const noexcept;
  [[nodiscard]] const ModelDescriptor* find(ModelId id) const noexcept;
  [[nodiscard]] bool empty() const noexcept;

 private:
  std::vector<ModelDescriptor> models_;
  ModelCatalogMetadata metadata_;
};

[[nodiscard]] ModelCatalog default_catalog();

}  // namespace obs_whisperbleep::model
