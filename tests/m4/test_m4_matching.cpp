// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/core/match_planner.hpp"
#include "obs_whisperbleep/core/phrase_matcher.hpp"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M4 matching test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::core;

  // The original overload remains case-insensitive and accepts substrings.
  PhraseMatcher legacy({"  Bad   Word  ", "LOUD", "bad word"});
  expect(legacy.phrases().size() == 2,
         "deduplicates normalized configured phrases");
  const auto legacy_matches = legacy.find("A BAD word, then loud.");
  expect(legacy_matches.size() == 2,
         "keeps the backward-compatible default behavior");
  expect(PhraseMatcher::normalize("A\t  B") == "a b",
         "normalizes whitespace and ASCII case");

  PhraseMatcher boundaries({"bad", "bad word"});
  PhraseMatchOptions whole_word;
  whole_word.boundary = MatchBoundary::whole_word;
  const auto boundary_matches =
      boundaries.find("badly bad, bad-word", whole_word);
  expect(boundary_matches.size() == 2,
         "matches whole words but rejects word prefixes");
  expect(boundary_matches[0].start == 6 && boundary_matches[0].end == 9 &&
             boundary_matches[1].start == 11 && boundary_matches[1].end == 14 &&
             boundary_matches[0].phrase == "bad" &&
             boundary_matches[1].phrase == "bad",
         "sorts matches deterministically by interval and phrase");

  PhraseMatcher punctuation({"bad word"});
  PhraseMatchOptions punctuation_as_separator;
  punctuation_as_separator.boundary = MatchBoundary::whole_word;
  punctuation_as_separator.punctuation = PunctuationMode::separator;
  const auto punctuation_matches =
      punctuation.find("BAD-word and bad.word", punctuation_as_separator);
  expect(punctuation_matches.size() == 2,
         "can normalize punctuation to phrase separators");
  expect(PhraseMatcher::normalize("Bad-word", punctuation_as_separator) ==
             "bad word",
         "exposes configured punctuation normalization");

  PhraseMatcher case_sensitive({"Bad"});
  PhraseMatchOptions sensitive;
  sensitive.case_sensitive = true;
  sensitive.boundary = MatchBoundary::whole_word;
  const auto sensitive_matches =
      case_sensitive.find("bad Bad BAD", sensitive);
  expect(sensitive_matches.size() == 1 && sensitive_matches[0].start == 4,
         "retains configured spelling for case-sensitive matching");

  PhraseMatcher exact({"bad word"});
  PhraseMatchOptions whole_text;
  whole_text.boundary = MatchBoundary::whole_text;
  expect(exact.find("bad word", whole_text).size() == 1 &&
             exact.find("a bad word", whole_text).empty(),
         "supports an exact normalized transcript policy");

  MatchPlannerConfig planner_config;
  planner_config.matching.boundary = MatchBoundary::whole_word;
  planner_config.timestamp = TimestampConfig{100, 10};
  MatchPlanner planner({"bad"}, planner_config);
  const auto intervals = planner.plan(
      {obs_whisperbleep::runtime::TranscriptSegment{10, 20, "BAD."},
       obs_whisperbleep::runtime::TranscriptSegment{15, 25, "bad again"},
       obs_whisperbleep::runtime::TranscriptSegment{30, 40, "good"},
       obs_whisperbleep::runtime::TranscriptSegment{-1, 2, "bad"},
       obs_whisperbleep::runtime::TranscriptSegment{40, 40, "bad"}});
  expect(intervals.size() == 1 && intervals[0].start_frame == 20 &&
             intervals[0].end_frame == 35,
         "converts matched runtime timestamps and merges overlap");

  return EXIT_SUCCESS;
}
