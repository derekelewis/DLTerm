#include "log.h"

#include <gtest/gtest.h>

#include <string>

namespace dlterm::log {
namespace {

class LogTest : public ::testing::Test {
 protected:
  void SetUp() override { Shutdown(); }
  void TearDown() override { Shutdown(); }
};

TEST_F(LogTest, DefaultLevelIsInfo) {
  SetFilterForTesting("");
  EXPECT_FALSE(Enabled(Level::kTrace, "any"));
  EXPECT_FALSE(Enabled(Level::kDebug, "any"));
  EXPECT_TRUE(Enabled(Level::kInfo, "any"));
  EXPECT_TRUE(Enabled(Level::kWarn, "any"));
  EXPECT_TRUE(Enabled(Level::kError, "any"));
}

TEST_F(LogTest, BareLevelSetsDefault) {
  SetFilterForTesting("debug");
  EXPECT_FALSE(Enabled(Level::kTrace, "anything"));
  EXPECT_TRUE(Enabled(Level::kDebug, "anything"));
}

TEST_F(LogTest, LevelNamesAreCaseInsensitive) {
  SetFilterForTesting("WARN");
  EXPECT_FALSE(Enabled(Level::kInfo, "anything"));
  EXPECT_TRUE(Enabled(Level::kWarn, "anything"));
}

TEST_F(LogTest, PerCategoryOverrideMoreVerbose) {
  SetFilterForTesting("info,tws=trace");
  EXPECT_FALSE(Enabled(Level::kTrace, "other"));
  EXPECT_TRUE(Enabled(Level::kTrace, "tws"));
}

TEST_F(LogTest, PerCategoryOverrideLessVerbose) {
  SetFilterForTesting("debug,tws=error");
  EXPECT_TRUE(Enabled(Level::kDebug, "other"));
  EXPECT_FALSE(Enabled(Level::kDebug, "tws"));
  EXPECT_FALSE(Enabled(Level::kInfo, "tws"));
  EXPECT_TRUE(Enabled(Level::kError, "tws"));
}

TEST_F(LogTest, PrefixMatchAppliesToDottedSubcategories) {
  SetFilterForTesting("info,tws=trace");
  EXPECT_TRUE(Enabled(Level::kTrace, "tws.dispatch"));
  EXPECT_FALSE(Enabled(Level::kTrace, "twsbits"));   // no dot — not a child
  EXPECT_FALSE(Enabled(Level::kTrace, "service.tws"));  // doesn't start with tws
}

TEST_F(LogTest, LongestPrefixWins) {
  SetFilterForTesting("info,tws=debug,tws.dispatch=trace");
  EXPECT_TRUE(Enabled(Level::kTrace, "tws.dispatch"));
  EXPECT_FALSE(Enabled(Level::kTrace, "tws.other"));
  EXPECT_TRUE(Enabled(Level::kDebug, "tws.other"));
}

TEST_F(LogTest, RingBufferCapturesEmittedLines) {
  SetFilterForTesting("trace");
  Emit(Level::kInfo, "test", "first");
  Emit(Level::kWarn, "test", "second");
  auto entries = RecentEntries(10);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].message, "first");
  EXPECT_EQ(entries[1].message, "second");
  EXPECT_EQ(entries[0].level, Level::kInfo);
  EXPECT_EQ(entries[1].level, Level::kWarn);
  EXPECT_EQ(entries[0].category, "test");
}

TEST_F(LogTest, RingBufferRespectsMaxArgument) {
  SetFilterForTesting("trace");
  for (int i = 0; i < 5; ++i) Emit(Level::kInfo, "t", std::to_string(i));
  auto last3 = RecentEntries(3);
  ASSERT_EQ(last3.size(), 3u);
  EXPECT_EQ(last3[0].message, "2");
  EXPECT_EQ(last3[1].message, "3");
  EXPECT_EQ(last3[2].message, "4");
}

TEST_F(LogTest, FilteredCallsAreNotRecorded) {
  SetFilterForTesting("warn");
  Emit(Level::kInfo, "test", "filtered");
  Emit(Level::kError, "test", "kept");
  auto entries = RecentEntries(10);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].message, "kept");
}

TEST(LogMacros, EmitFormatTypeChecksAtCompileTime) {
  Shutdown();
  SetFilterForTesting("trace");
  DLTERM_LOG_INFO("macro", "value={}", 42);
  auto entries = RecentEntries(1);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].message, "value=42");
  Shutdown();
}

TEST(LogLevel, NameStringsAreReadable) {
  EXPECT_EQ(LevelName(Level::kTrace), "TRACE");
  EXPECT_EQ(LevelName(Level::kDebug), "DEBUG");
  EXPECT_EQ(LevelName(Level::kError), "ERROR");
}

}  // namespace
}  // namespace dlterm::log
