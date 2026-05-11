#include "quote_screen.h"

#include <gtest/gtest.h>
#include <string>

#include "screen.h"
#include "support/fake_ibkr_client.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

class QuoteScreenTest : public ::testing::Test {
 protected:
  test::FakeIbkrClient client;
  test::SpyRenderBackend backend{120, 30};
  FunctionBar bar;

  ScreenContext MakeCtx() {
    ScreenContext ctx;
    ctx.render = &backend;
    ctx.metrics = backend.Metrics();
    ctx.win_w_px = backend.WindowWidthPx();
    ctx.win_h_px = backend.WindowHeightPx();
    ctx.pixel_density = backend.PixelDensity();
    ctx.client = &client;
    ctx.bar = &bar;
    return ctx;
  }

  QuoteSnapshot MakePopulatedSnapshot() {
    QuoteSnapshot q;
    q.symbol = "AAPL";
    q.kind = ChartInstrument::kStock;
    q.ready = true;
    q.bid = 234.50;
    q.ask = 234.55;
    q.last = 234.52;
    q.bid_size = 1200;
    q.ask_size = 800;
    q.last_size = 100;
    q.high = 235.10;
    q.low = 232.50;
    q.open = 233.00;
    q.close = 232.80;
    q.volume = 12345678;
    q.last_ts = 0;  // Tested separately (TZ-dependent format).
    return q;
  }

  void SetUp() override { client.SetConnected(true); }
};

TEST_F(QuoteScreenTest, OnEnterRequestsQuoteForStock) {
  QuoteScreen screen("AAPL", ChartInstrument::kStock);
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.request_quote_calls, 1);
  EXPECT_EQ(client.last_quote_symbol, "AAPL");
  EXPECT_EQ(client.last_quote_instrument, ChartInstrument::kStock);
}

TEST_F(QuoteScreenTest, OnEnterPropagatesContinuousFutureKind) {
  QuoteScreen screen("CL", ChartInstrument::kContinuousFuture);
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.last_quote_instrument, ChartInstrument::kContinuousFuture);
}

TEST_F(QuoteScreenTest, OnExitCancelsQuote) {
  QuoteScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnExit(ctx);
  EXPECT_EQ(client.cancel_quote_calls, 1);
}

TEST_F(QuoteScreenTest, RendersWaitingBeforeSnapshotReady) {
  QuoteScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  // Body row 0 renders at backend row kBarReservedRows (2).
  EXPECT_NE(backend.Row(2).find("AAPL"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("STOCK"), std::string::npos);
  EXPECT_NE(backend.GridSnapshot().find("WAITING"), std::string::npos);
}

TEST_F(QuoteScreenTest, RendersPopulatedSnapshot) {
  client.SetQuote(MakePopulatedSnapshot());
  QuoteScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);

  const std::string grid = backend.GridSnapshot();
  EXPECT_NE(grid.find("AAPL"), std::string::npos);
  EXPECT_NE(grid.find("LAST"), std::string::npos);
  EXPECT_NE(grid.find("234.52"), std::string::npos);  // last
  EXPECT_NE(grid.find("234.50"), std::string::npos);  // bid
  EXPECT_NE(grid.find("234.55"), std::string::npos);  // ask
  EXPECT_NE(grid.find("HIGH"), std::string::npos);
  EXPECT_NE(grid.find("235.10"), std::string::npos);
  EXPECT_NE(grid.find("VOLUME"), std::string::npos);
  EXPECT_NE(grid.find("12,345,678"), std::string::npos);  // comma-grouped
}

TEST_F(QuoteScreenTest, RendersChangeVsPreviousClose) {
  client.SetQuote(MakePopulatedSnapshot());
  QuoteScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  // last=234.52, close=232.80 -> diff +1.72, pct +0.74%
  const std::string grid = backend.GridSnapshot();
  EXPECT_NE(grid.find("+1.72"), std::string::npos);
}

TEST_F(QuoteScreenTest, MissingFieldsRenderAsDashes) {
  QuoteSnapshot q;
  q.symbol = "AAPL";
  q.kind = ChartInstrument::kStock;
  q.ready = true;
  q.last = 100.00;
  // bid/ask/high/low/open/close stay NaN; sizes stay -1.
  client.SetQuote(q);
  QuoteScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  const std::string grid = backend.GridSnapshot();
  // "BID" row should show "--" placeholders for both price and size.
  EXPECT_NE(grid.find("BID"), std::string::npos);
  EXPECT_NE(grid.find("--"), std::string::npos);
}

TEST_F(QuoteScreenTest, ContinuousFutureRendersFuturesLabel) {
  QuoteScreen screen("CL", ChartInstrument::kContinuousFuture);
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  EXPECT_NE(backend.Row(2).find("CL"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("CONT FUT"), std::string::npos);
}

}  // namespace
}  // namespace dlterm
