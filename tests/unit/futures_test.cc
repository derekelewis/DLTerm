#include "futures.h"

#include <gtest/gtest.h>

namespace dlterm {
namespace {

TEST(ExchangeForContinuousFuture, EnergyOnNymex) {
  EXPECT_EQ(ExchangeForContinuousFuture("CL"), "NYMEX");
  EXPECT_EQ(ExchangeForContinuousFuture("NG"), "NYMEX");
}

TEST(ExchangeForContinuousFuture, MetalsOnComex) {
  EXPECT_EQ(ExchangeForContinuousFuture("GC"), "COMEX");
  EXPECT_EQ(ExchangeForContinuousFuture("SI"), "COMEX");
}

TEST(ExchangeForContinuousFuture, EquityIndexOnCme) {
  EXPECT_EQ(ExchangeForContinuousFuture("ES"), "CME");
  EXPECT_EQ(ExchangeForContinuousFuture("NQ"), "CME");
}

TEST(ExchangeForContinuousFuture, RatesAndGrainsOnCbot) {
  EXPECT_EQ(ExchangeForContinuousFuture("ZN"), "CBOT");
  EXPECT_EQ(ExchangeForContinuousFuture("ZC"), "CBOT");
}

TEST(ExchangeForContinuousFuture, UnknownSymbolReturnsEmpty) {
  EXPECT_EQ(ExchangeForContinuousFuture("XYZ"), "");
  EXPECT_EQ(ExchangeForContinuousFuture(""), "");
}

TEST(ExchangeForContinuousFuture, CaseSensitive) {
  // Caller is expected to upper-case (parser does). Lowercase misses.
  EXPECT_EQ(ExchangeForContinuousFuture("cl"), "");
}

TEST(IsFuturesLocalSymbol, MatchesSingleDigitYear) {
  EXPECT_TRUE(IsFuturesLocalSymbol("CLM6"));
  EXPECT_TRUE(IsFuturesLocalSymbol("GCQ6"));
  EXPECT_TRUE(IsFuturesLocalSymbol("ESM6"));
}

TEST(IsFuturesLocalSymbol, MatchesTwoDigitYear) {
  EXPECT_TRUE(IsFuturesLocalSymbol("ESM26"));
  EXPECT_TRUE(IsFuturesLocalSymbol("ZNH26"));
}

TEST(IsFuturesLocalSymbol, RejectsPlainStockTicker) {
  EXPECT_FALSE(IsFuturesLocalSymbol("AAPL"));
  EXPECT_FALSE(IsFuturesLocalSymbol("BRK.B"));
}

TEST(IsFuturesLocalSymbol, RejectsTooShort) {
  EXPECT_FALSE(IsFuturesLocalSymbol("M6"));   // no root
  EXPECT_FALSE(IsFuturesLocalSymbol("C6"));   // just 2 chars; no month
}

TEST(IsFuturesLocalSymbol, RejectsAllDigitsOrAllAlpha) {
  EXPECT_FALSE(IsFuturesLocalSymbol("123456"));
  EXPECT_FALSE(IsFuturesLocalSymbol("ABCDE"));
}

TEST(IsFuturesLocalSymbol, RejectsThreeDigitYear) {
  EXPECT_FALSE(IsFuturesLocalSymbol("CLM126"));
}

TEST(RootFromFuturesLocalSymbol, ExtractsRoot) {
  EXPECT_EQ(RootFromFuturesLocalSymbol("CLM6"), "CL");
  EXPECT_EQ(RootFromFuturesLocalSymbol("ESM26"), "ES");
  EXPECT_EQ(RootFromFuturesLocalSymbol("ZNH26"), "ZN");
}

TEST(RootFromFuturesLocalSymbol, ReturnsEmptyForNonLocal) {
  EXPECT_EQ(RootFromFuturesLocalSymbol("AAPL"), "");
}

}  // namespace
}  // namespace dlterm
