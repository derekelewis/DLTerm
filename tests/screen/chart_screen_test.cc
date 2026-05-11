#include "chart_screen.h"

#include <gtest/gtest.h>

#include "screen.h"
#include "support/fake_ibkr_client.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

class ChartScreenTest : public ::testing::Test {
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

  void SetUp() override { client.SetConnected(true); }
};

TEST_F(ChartScreenTest, OnEnterRequestsChart) {
  ChartScreen screen("MSFT");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.request_chart_calls, 1);
  EXPECT_EQ(client.last_chart_symbol, "MSFT");
}

TEST_F(ChartScreenTest, OnExitCancelsChart) {
  ChartScreen screen("MSFT");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnExit(ctx);
  EXPECT_EQ(client.cancel_chart_calls, 1);
}

TEST_F(ChartScreenTest, EmptyBarsRendersLoadingHeader) {
  ChartScreen screen("MSFT");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  EXPECT_NE(backend.Row(2).find("CHART"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("MSFT"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("loading"), std::string::npos);
}

TEST_F(ChartScreenTest, BarsRenderHeaderWithCountAndCandles) {
  ChartScreen screen("MSFT");
  ChartSnapshot snap;
  snap.symbol = "MSFT";
  snap.ready = true;
  snap.bars.push_back({.ts = 1700000000, .open = 100, .high = 105,
                        .low = 99, .close = 104, .volume = 1000});
  snap.bars.push_back({.ts = 1700000300, .open = 104, .high = 110,
                        .low = 102, .close = 108, .volume = 1200});
  client.SetChart(snap);
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  EXPECT_NE(backend.Row(2).find("MSFT"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("Last 108"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("2 bars"), std::string::npos);
  // Two candle bodies (FillRect) + 2 wicks (DrawLineSegment) + 1 axis line.
  EXPECT_GE(backend.FillRectCount(), 2u);
  EXPECT_GE(backend.LineSegmentCount(), 3u);
}

}  // namespace
}  // namespace dlterm
