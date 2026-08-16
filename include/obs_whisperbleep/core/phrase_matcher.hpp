// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace obs_whisperbleep::core {

struct PhraseMatch {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string phrase;
};

/**
 * Deterministic, dependency-free phrase matcher used by the M0 scaffold.
 * Matching is case-insensitive for ASCII text and treats runs of whitespace
 * as a single separator. Punctuation is kept as text so callers can choose
 * their own transcription normalization policy later.
 */
class PhraseMatcher {
 public:
  PhraseMatcher() = default;
  explicit PhraseMatcher(std::vector<std::string> phrases);

  void set_phrases(std::vector<std::string> phrases);
  [[nodiscard]] const std::vector<std::string>& phrases() const noexcept;
  [[nodiscard]] std::vector<PhraseMatch> find(std::string_view text) const;

  [[nodiscard]] static std::string normalize(std::string_view text);

 private:
  std::vector<std::string> phrases_;
};

}  // namespace obs_whisperbleep::core
