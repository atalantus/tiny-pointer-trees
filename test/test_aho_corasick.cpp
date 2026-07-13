#include "aho_corasick/automaton.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Collects the matched words in the order they are reported.
std::vector<std::string> MatchedWords(const Automaton& automaton, const std::string& text) {
  std::vector<std::string> words;
  for (const auto& match : automaton.Search(text)) {
    words.push_back(match.word);
  }
  return words;
}

}  // namespace

TEST(TestAhoCorasick, BasicNoMatch) {
  auto automaton = Automaton::Create({"he", "she", "his", "hers"});

  // empty text has no matches
  EXPECT_TRUE(automaton.Search("").empty());

  // no match
  EXPECT_TRUE(automaton.Search("xyzabc").empty());
}

TEST(TestAhoCorasick, SingleMatch) {
  auto automaton = Automaton::Create({"his"});
  auto matches = automaton.Search("this");

  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].word, "his");
  EXPECT_EQ(matches[0].start, 1u);
  EXPECT_EQ(matches[0].end, 3u);
}

TEST(TestAhoCorasick, OverlappingMatchesViaSuffixLinks) {
  // "she" also contains the pattern "he" as a suffix.
  auto automaton = Automaton::Create({"he", "she"});
  auto words = MatchedWords(automaton, "she");

  EXPECT_EQ(words, (std::vector<std::string>{"she", "he"}));
}

TEST(TestAhoCorasick, ClassicUshersExample) {
  auto automaton = Automaton::Create({"he", "she", "his", "hers"});
  auto words = MatchedWords(automaton, "ushers");

  // "she" (1-3), "he" (2-3) and "hers" (2-5) all occur.
  EXPECT_EQ(words, (std::vector<std::string>{"she", "he", "hers"}));
}

TEST(TestAhoCorasick, ReportsMatchPositions) {
  auto automaton = Automaton::Create({"he", "she", "hers"});
  auto matches = automaton.Search("ushers");

  ASSERT_EQ(matches.size(), 3u);

  EXPECT_EQ(matches[0].word, "she");
  EXPECT_EQ(matches[0].start, 1u);
  EXPECT_EQ(matches[0].end, 3u);

  EXPECT_EQ(matches[1].word, "he");
  EXPECT_EQ(matches[1].start, 2u);
  EXPECT_EQ(matches[1].end, 3u);

  EXPECT_EQ(matches[2].word, "hers");
  EXPECT_EQ(matches[2].start, 2u);
  EXPECT_EQ(matches[2].end, 5u);
}

TEST(TestAhoCorasick, RepeatedPatternMatchedEachOccurrence) {
  auto automaton = Automaton::Create({"aa"});
  auto matches = automaton.Search("aaaa");

  // Overlapping occurrences at positions 0, 1 and 2.
  ASSERT_EQ(matches.size(), 3u);
  EXPECT_EQ(matches[0].start, 0u);
  EXPECT_EQ(matches[1].start, 1u);
  EXPECT_EQ(matches[2].start, 2u);
}
