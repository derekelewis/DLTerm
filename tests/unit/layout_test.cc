#include "layout.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace dlterm {
namespace {

TEST(OptimalBreaks, EmptyWordsReturnsSingleZero) {
  std::vector<std::string_view> words;
  auto breaks = OptimalBreaks(words, 80);
  ASSERT_EQ(breaks.size(), 1u);
  EXPECT_EQ(breaks[0], 0u);
}

TEST(OptimalBreaks, SingleShortWord) {
  std::vector<std::string_view> words = {"hello"};
  auto breaks = OptimalBreaks(words, 80);
  ASSERT_EQ(breaks.size(), 2u);
  EXPECT_EQ(breaks[0], 0u);
  EXPECT_EQ(breaks[1], 1u);
}

TEST(OptimalBreaks, FitsOnSingleLine) {
  std::vector<std::string_view> words = {"a", "b", "c"};
  auto breaks = OptimalBreaks(words, 80);
  ASSERT_EQ(breaks.size(), 2u);
  EXPECT_EQ(breaks[1], 3u);
}

TEST(OptimalBreaks, BreaksAcrossLines) {
  std::vector<std::string_view> words = {"hello", "world", "foo", "bar"};
  auto breaks = OptimalBreaks(words, 11);
  EXPECT_GT(breaks.size(), 2u);
  EXPECT_EQ(breaks.front(), 0u);
  EXPECT_EQ(breaks.back(), 4u);
}

TEST(OptimalBreaks, OverlongWordPlacedAlone) {
  std::vector<std::string_view> words = {"a", "supercalifragilistic", "b"};
  auto breaks = OptimalBreaks(words, 5);
  EXPECT_EQ(breaks.front(), 0u);
  EXPECT_EQ(breaks.back(), 3u);
  // Long word forms its own line — at least 4 break boundaries (3 lines).
  EXPECT_GE(breaks.size(), 4u);
}

TEST(LayoutText, EmptyInputReturnsEmpty) {
  EXPECT_TRUE(LayoutText("", 80).empty());
}

TEST(LayoutText, ZeroColsReturnsEmpty) {
  EXPECT_TRUE(LayoutText("hello world", 0).empty());
}

TEST(LayoutText, SimpleParagraphWraps) {
  auto lines = LayoutText("the quick brown fox jumps over the lazy dog", 16);
  EXPECT_GT(lines.size(), 1u);
  for (const auto& l : lines) {
    EXPECT_LE(l.size(), 16u + 5u);  // generous upper bound; some words longer
  }
}

TEST(LayoutText, ParagraphsSeparatedByBlankLine) {
  auto lines = LayoutText("alpha beta\n\ngamma delta", 80);
  ASSERT_GE(lines.size(), 3u);
  // There should be one empty line between the two paragraphs.
  bool found_blank = false;
  for (const auto& l : lines) {
    if (l.empty()) { found_blank = true; break; }
  }
  EXPECT_TRUE(found_blank);
}

TEST(LayoutText, WhitespaceOnlyParagraphIsBlank) {
  auto lines = LayoutText("   \n   ", 80);
  EXPECT_TRUE(lines.empty());
}

}  // namespace
}  // namespace dlterm
