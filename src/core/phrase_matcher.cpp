// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/core/phrase_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace obs_whisperbleep::core {

PhraseMatcher::PhraseMatcher(std::vector<std::string> phrases) {
  set_phrases(std::move(phrases));
}

void PhraseMatcher::set_phrases(std::vector<std::string> phrases) {
  phrases_.clear();
  phrases_.reserve(phrases.size());
  for (auto& phrase : phrases) {
    auto normalized = normalize(phrase);
    if (normalized.empty() ||
        std::find(phrases_.begin(), phrases_.end(), normalized) != phrases_.end()) {
      continue;
    }
    phrases_.push_back(std::move(normalized));
  }
}

const std::vector<std::string>& PhraseMatcher::phrases() const noexcept {
  return phrases_;
}

std::string PhraseMatcher::normalize(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previous_was_space = false;
  for (const unsigned char character : text) {
    if (std::isspace(character) != 0) {
      if (!normalized.empty() && !previous_was_space) {
        normalized.push_back(' ');
      }
      previous_was_space = true;
      continue;
    }
    normalized.push_back(static_cast<char>(std::tolower(character)));
    previous_was_space = false;
  }
  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }
  return normalized;
}

std::vector<PhraseMatch> PhraseMatcher::find(std::string_view text) const {
  const auto normalized_text = normalize(text);
  std::vector<PhraseMatch> matches;
  for (const auto& phrase : phrases_) {
    std::size_t search_from = 0;
    while (search_from < normalized_text.size()) {
      const auto position = normalized_text.find(phrase, search_from);
      if (position == std::string::npos) {
        break;
      }
      matches.push_back({position, position + phrase.size(), phrase});
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
