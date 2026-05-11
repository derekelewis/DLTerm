#include "function_bar.h"

#include <gtest/gtest.h>

namespace dlterm {
namespace {

TerminalInfo MakeInfo() {
  TerminalInfo i;
  i.win_w_pt = 1280;
  i.win_h_pt = 800;
  i.win_w_px = 2560;
  i.win_h_px = 1600;
  i.pixel_density = 2.0f;
  i.cols = 100;
  i.rows = 40;
  i.cell_w = 13;
  i.cell_h = 28;
  i.font_pt = 22;
  i.font_path = "/path/IBMPlexMono-Regular.ttf";
  i.renderer_name = "metal";
  i.body_lines = 7;
  i.body_words = 100;
  return i;
}

TEST(RunCommand, EmptyInputProducesNone) {
  auto r = RunCommand("", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_TRUE(r.status.empty());
  EXPECT_TRUE(r.text.empty());
}

TEST(RunCommand, WhitespaceOnlyProducesNone) {
  auto r = RunCommand("   \t  ", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
}

TEST(RunCommand, InfoVerbProducesStaticText) {
  auto r = RunCommand("INFO", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStaticText);
  EXPECT_NE(r.text.find("DLTERM"), std::string::npos);
  EXPECT_NE(r.text.find("WINDOW"), std::string::npos);
  EXPECT_NE(r.text.find("100 cols"), std::string::npos);
}

TEST(RunCommand, InfoIsCaseInsensitive) {
  auto r = RunCommand("info", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStaticText);
}

TEST(RunCommand, NewsVerbAloneStartsGeneralNews) {
  auto r = RunCommand("NEWS", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartGeneralNews);
}

TEST(RunCommand, PosVerbStartsPortfolio) {
  auto r = RunCommand("POS", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartPortfolio);
}

TEST(RunCommand, SymbolNewsStartsTickerNews) {
  auto r = RunCommand("AAPL NEWS", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartNews);
  EXPECT_EQ(r.symbol, "AAPL");
}

TEST(RunCommand, SymbolNewsLowercaseIsUppercased) {
  auto r = RunCommand("aapl news", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartNews);
  EXPECT_EQ(r.symbol, "AAPL");
}

TEST(RunCommand, SymbolChartStartsChart) {
  auto r = RunCommand("MSFT CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.symbol, "MSFT");
}

TEST(RunCommand, SymbolWithDotIsValid) {
  auto r = RunCommand("BRK.B NEWS", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartNews);
  EXPECT_EQ(r.symbol, "BRK.B");
}

TEST(RunCommand, NumericSymbolIsRejected) {
  auto r = RunCommand("123 NEWS", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Invalid symbol"), std::string::npos);
}

TEST(RunCommand, OverlongSymbolIsRejected) {
  auto r = RunCommand("AAAAAAAAA NEWS", MakeInfo());  // 9 chars, max 8
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Invalid symbol"), std::string::npos);
}

TEST(RunCommand, UnknownSingleVerbInvalidSymbolReportsStatus) {
  // Numeric-only token isn't a valid symbol, so it isn't a quote.
  auto r = RunCommand("123", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Unknown command"), std::string::npos);
  EXPECT_NE(r.status.find("123"), std::string::npos);
}

TEST(RunCommand, BareSymbolStartsStockQuote) {
  auto r = RunCommand("AAPL", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartQuote);
  EXPECT_EQ(r.symbol, "AAPL");
  EXPECT_EQ(r.instrument, ChartInstrument::kStock);
}

TEST(RunCommand, BareSymbolLowercaseIsUppercased) {
  auto r = RunCommand("aapl", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartQuote);
  EXPECT_EQ(r.symbol, "AAPL");
}

TEST(RunCommand, BareFuturesLocalSymbolStartsFutureLocalQuote) {
  auto r = RunCommand("CLM6", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartQuote);
  EXPECT_EQ(r.symbol, "CLM6");
  EXPECT_EQ(r.instrument, ChartInstrument::kFutureLocal);
}

TEST(RunCommand, BareUnknownFuturesRootReportsStatus) {
  auto r = RunCommand("XYM6", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Unknown futures root"), std::string::npos);
}

TEST(RunCommand, ExitVerbQuits) {
  auto r = RunCommand("EXIT", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kQuit);
}

TEST(RunCommand, QuitVerbQuits) {
  auto r = RunCommand("QUIT", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kQuit);
}

TEST(RunCommand, ExitIsCaseInsensitive) {
  auto r = RunCommand("exit", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kQuit);
}

TEST(RunCommand, ReservedVerbStillWinsOverBareQuote) {
  // INFO is alpha-only and IsValidSymbol-true, but reserved verbs are
  // checked first.
  EXPECT_EQ(RunCommand("INFO", MakeInfo()).kind, CommandKind::kStaticText);
  EXPECT_EQ(RunCommand("NEWS", MakeInfo()).kind, CommandKind::kStartGeneralNews);
  EXPECT_EQ(RunCommand("POS", MakeInfo()).kind, CommandKind::kStartPortfolio);
  EXPECT_EQ(RunCommand("DEBUG", MakeInfo()).kind, CommandKind::kStartDebug);
  EXPECT_EQ(RunCommand("EXIT", MakeInfo()).kind, CommandKind::kQuit);
  EXPECT_EQ(RunCommand("QUIT", MakeInfo()).kind, CommandKind::kQuit);
}

TEST(RunCommand, SymbolFutStartsContinuousFutureQuote) {
  auto r = RunCommand("CL FUT", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartQuote);
  EXPECT_EQ(r.symbol, "CL");
  EXPECT_EQ(r.instrument, ChartInstrument::kContinuousFuture);
}

TEST(RunCommand, SymbolFutUnknownReportsStatus) {
  auto r = RunCommand("XYZ FUT", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Unknown futures symbol"), std::string::npos);
}

TEST(RunCommand, UnknownMultiTokenReportsJoinedStatus) {
  auto r = RunCommand("DO STUFF NOW", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("DO STUFF NOW"), std::string::npos);
}

TEST(RunCommand, ExtraWhitespaceIsTolerated) {
  auto r = RunCommand("   AAPL    NEWS   ", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartNews);
  EXPECT_EQ(r.symbol, "AAPL");
}

TEST(RunCommand, FutChartForKnownSymbolProducesContinuousFuture) {
  auto r = RunCommand("CL FUT CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.symbol, "CL");
  EXPECT_EQ(r.instrument, ChartInstrument::kContinuousFuture);
}

TEST(RunCommand, FutChartIsCaseInsensitive) {
  auto r = RunCommand("cl fut chart", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.symbol, "CL");
  EXPECT_EQ(r.instrument, ChartInstrument::kContinuousFuture);
}

TEST(RunCommand, PlainChartStaysStock) {
  auto r = RunCommand("AAPL CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.instrument, ChartInstrument::kStock);
}

TEST(RunCommand, FutChartUnknownSymbolReportsStatus) {
  auto r = RunCommand("XYZ FUT CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Unknown futures symbol"), std::string::npos);
  EXPECT_NE(r.status.find("XYZ"), std::string::npos);
}

TEST(RunCommand, FutChartInvalidSymbolReportsStatus) {
  // 9 chars exceeds the 8-char max, so this trips IsValidSymbol.
  auto r = RunCommand("CL1234567 FUT CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Invalid symbol"), std::string::npos);
}

TEST(RunCommand, ChartAutoDetectsFuturesLocalSymbol) {
  auto r = RunCommand("CLM6 CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.symbol, "CLM6");
  EXPECT_EQ(r.instrument, ChartInstrument::kFutureLocal);
}

TEST(RunCommand, ChartAutoDetectsTwoDigitYearLocalSymbol) {
  auto r = RunCommand("ESM26 CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.symbol, "ESM26");
  EXPECT_EQ(r.instrument, ChartInstrument::kFutureLocal);
}

TEST(RunCommand, ChartStockUnaffectedByAutoDetect) {
  auto r = RunCommand("AAPL CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kStartChart);
  EXPECT_EQ(r.instrument, ChartInstrument::kStock);
}

TEST(RunCommand, ChartUnknownFuturesRootReportsStatus) {
  auto r = RunCommand("XYM6 CHART", MakeInfo());
  EXPECT_EQ(r.kind, CommandKind::kNone);
  EXPECT_NE(r.status.find("Unknown futures root"), std::string::npos);
  EXPECT_NE(r.status.find("XY"), std::string::npos);
}

TEST(BarMaxInputCols, ShrinksWithGrid) {
  // 1 cursor cell + kBarLeftPadCols (2) reserved.
  EXPECT_EQ(BarMaxInputCols(10), 7u);
  EXPECT_EQ(BarMaxInputCols(100), 97u);
}

TEST(BarMaxInputCols, ZeroOnNarrowGrid) {
  EXPECT_EQ(BarMaxInputCols(3), 0u);
  EXPECT_EQ(BarMaxInputCols(2), 0u);
  EXPECT_EQ(BarMaxInputCols(0), 0u);
  EXPECT_EQ(BarMaxInputCols(-1), 0u);
}

}  // namespace
}  // namespace dlterm
