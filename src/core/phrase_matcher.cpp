// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/phrase_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace obs_whisperbleep::core {

namespace {

[[nodiscard]] bool is_word_character(const char character) noexcept {
  const auto value = static_cast<unsigned char>(character);
  return std::isalnum(value) != 0 || character == '_';
}

[[nodiscard]] std::string normalize_impl(std::string_view text,
                                         const PhraseMatchOptions& options,
                                         const bool preserve_case) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previous_was_space = false;
  for (const unsigned char character : text) {
    if (std::isspace(character) != 0 ||
        (options.punctuation == PunctuationMode::separator &&
         std::ispunct(character) != 0)) {
      if (!normalized.empty() && !previous_was_space) {
        normalized.push_back(' ');
      }
      previous_was_space = true;
      continue;
    }

    const auto normalized_character =
        preserve_case ? character
                      : static_cast<unsigned char>(std::tolower(character));
    normalized.push_back(static_cast<char>(normalized_character));
    previous_was_space = false;
  }
  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }
  return normalized;
}

[[nodiscard]] bool respects_boundary(const std::string& text,
                                     const std::size_t position,
                                     const std::size_t length,
                                     const MatchBoundary boundary) noexcept {
  if (boundary == MatchBoundary::substring) {
    return true;
  }
  if (boundary == MatchBoundary::whole_text) {
    return position == 0 && length == text.size();
  }

  const bool has_left_word =
      position != 0 && is_word_character(text[position - 1]);
  const auto end = position + length;
  const bool has_right_word =
      end < text.size() && is_word_character(text[end]);
  return !has_left_word && !has_right_word;
}

}  // namespace

PhraseMatcher::PhraseMatcher(std::vector<std::string> phrases) {
  set_phrases(std::move(phrases));
}

void PhraseMatcher::set_phrases(std::vector<std::string> phrases) {
  phrases_.clear();
  original_phrases_.clear();
  phrases_.reserve(phrases.size());
  original_phrases_.reserve(phrases.size());
  for (auto& phrase : phrases) {
    auto normalized = normalize(phrase);
    if (normalized.empty() ||
        std::find(phrases_.begin(), phrases_.end(), normalized) !=
            phrases_.end()) {
      continue;
    }
    phrases_.push_back(normalized);
    original_phrases_.push_back(normalize_preserving_case(phrase));
  }
}

const std::vector<std::string>& PhraseMatcher::phrases() const noexcept {
  return phrases_;
}

std::string PhraseMatcher::normalize(std::string_view text) {
  return normalize_impl(text, PhraseMatchOptions{}, false);
}

std::string PhraseMatcher::normalize(std::string_view text,
                                     const PhraseMatchOptions& options) {
  return normalize_impl(text, options, options.case_sensitive);
}

std::string PhraseMatcher::normalize_preserving_case(std::string_view text) {
  return normalize_impl(text, PhraseMatchOptions{}, true);
}

std::vector<PhraseMatch> PhraseMatcher::find(std::string_view text) const {
  return find(text, PhraseMatchOptions{});
}

std::vector<PhraseMatch> PhraseMatcher::find(
    std::string_view text, const PhraseMatchOptions& options) const {
  const auto normalized_text = normalize(text, options);
  std::vector<PhraseMatch> matches;
  std::vector<std::string> effective_phrases;
  effective_phrases.reserve(original_phrases_.size());
  for (const auto& original_phrase : original_phrases_) {
    auto phrase = normalize(original_phrase, options);
    if (phrase.empty() ||
        std::find(effective_phrases.begin(), effective_phrases.end(), phrase) !=
            effective_phrases.end()) {
      continue;
    }
    effective_phrases.push_back(std::move(phrase));
  }

  for (const auto& phrase : effective_phrases) {
    std::size_t search_from = 0;
    while (search_from < normalized_text.size()) {
      const auto position = normalized_text.find(phrase, search_from);
      if (position == std::string::npos) {
        break;
      }
      if (respects_boundary(normalized_text, position, phrase.size(),
                            options.boundary)) {
        matches.push_back({position, position + phrase.size(), phrase});
      }
      search_from = position + 1;
    }
  }
  std::sort(matches.begin(), matches.end(), [](const PhraseMatch& left,
                                               const PhraseMatch& right) {
    if (left.start != right.start) {
      return left.start < right.start;
    }
    if (left.end != right.end) {
      return left.end < right.end;
    }
    return left.phrase < right.phrase;
  });
  return matches;
}

}  // namespace obs_whisperbleep::core
