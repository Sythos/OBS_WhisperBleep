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
};

[[nodiscard]] const char* model_id_name(ModelId id) noexcept;
[[nodiscard]] std::optional<ModelId> model_id_from_name(std::string_view name);

class ModelCatalog {
 public:
  ModelCatalog() = default;
  explicit ModelCatalog(std::vector<ModelDescriptor> models);

  [[nodiscard]] const std::vector<ModelDescriptor>& models() const noexcept;
  [[nodiscard]] const ModelDescriptor* find(ModelId id) const noexcept;
  [[nodiscard]] bool empty() const noexcept;

 private:
  std::vector<ModelDescriptor> models_;
};

[[nodiscard]] ModelCatalog default_catalog();

}  // namespace obs_whisperbleep::model
