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

/** Controls whether a configured phrase may match inside a larger word. */
enum class MatchBoundary {
  substring,
  whole_word,
  whole_text,
};

/** Controls whether ASCII punctuation remains significant during matching. */
enum class PunctuationMode {
  significant,
  separator,
};

struct PhraseMatchOptions {
  bool case_sensitive = false;
  MatchBoundary boundary = MatchBoundary::substring;
  PunctuationMode punctuation = PunctuationMode::significant;
};

/**
 * Deterministic, dependency-free phrase matcher used by the core pipeline.
 *
 * The original find(text) overload keeps the M0 substring behavior. Callers
 * that need false-positive protection can select whole_word or whole_text and
 * may treat ASCII punctuation as a separator. Match offsets refer to the
 * normalized text, as they did in the original implementation.
 */
class PhraseMatcher {
 public:
  PhraseMatcher() = default;
  explicit PhraseMatcher(std::vector<std::string> phrases);

  void set_phrases(std::vector<std::string> phrases);
  [[nodiscard]] const std::vector<std::string>& phrases() const noexcept;
  [[nodiscard]] std::vector<PhraseMatch> find(std::string_view text) const;
  [[nodiscard]] std::vector<PhraseMatch> find(
      std::string_view text, const PhraseMatchOptions& options) const;

  [[nodiscard]] static std::string normalize(std::string_view text);
  [[nodiscard]] static std::string normalize(
      std::string_view text, const PhraseMatchOptions& options);

 private:
  std::vector<std::string> phrases_;
  // Keep the first configured spelling so case-sensitive matching remains
  // available while phrases() preserves the original normalized API.
  std::vector<std::string> original_phrases_;

  [[nodiscard]] static std::string normalize_preserving_case(
      std::string_view text);
};

}  // namespace obs_whisperbleep::core
