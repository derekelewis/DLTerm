#include "widgets.h"

#include <gtest/gtest.h>

namespace dlterm {
namespace {

TEST(BodyCols, ReportsZeroForDegenerateMetrics) {
  EXPECT_EQ(BodyCols({.cols = 0}), 0u);
  EXPECT_EQ(BodyCols({.cols = -5}), 0u);
}

TEST(BodyCols, ReturnsCols) {
  EXPECT_EQ(BodyCols({.cols = 80}), 80u);
}

TEST(TruncateToCols, NoChangeIfShortEnough) {
  EXPECT_EQ(TruncateToCols("hello", 5), "hello");
  EXPECT_EQ(TruncateToCols("hi", 80), "hi");
}

TEST(TruncateToCols, ReplacesLastCharWithTilde) {
  EXPECT_EQ(TruncateToCols("hello world", 5), "hell~");
}

TEST(TruncateToCols, ZeroColsProducesEmpty) {
  EXPECT_EQ(TruncateToCols("hello", 0), "");
}

TEST(TruncateToCols, OneColWithLongInputBecomesTilde) {
  EXPECT_EQ(TruncateToCols("hello", 1), "~");
}

TEST(TableFormat, RendersHeaderAndRows) {
  Table t;
  t.SetColumns({
      {"NAME", 4, Table::Align::kLeft},
      {"QTY", 4, Table::Align::kRight},
  });
  t.AddRow({"AAPL", "100"});
  t.AddRow({"MSFT", "5"});
  auto lines = t.Format(80);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "NAME  QTY");
  EXPECT_EQ(lines[1], "AAPL  100");
  EXPECT_EQ(lines[2], "MSFT    5");
}

TEST(TableFormat, TruncatesLongLines) {
  Table t;
  t.SetColumns({
      {"COL", 10, Table::Align::kLeft},
      {"OTHER", 10, Table::Align::kLeft},
  });
  t.AddRow({"A", "B"});
  auto lines = t.Format(8);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].size(), 8u);
  EXPECT_EQ(lines[0].back(), '~');
}

TEST(TableFormat, EmptyColumnsProducesEmpty) {
  Table t;
  t.AddRow({"x"});
  EXPECT_TRUE(t.Format(80).empty());
}

TEST(TableFormat, MissingCellsRendersBlank) {
  Table t;
  t.SetColumns({
      {"A", 3, Table::Align::kLeft},
      {"B", 3, Table::Align::kLeft},
  });
  t.AddRow({"x"});  // only one cell
  auto lines = t.Format(80);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1], "x      ");
}

}  // namespace
}  // namespace dlterm
