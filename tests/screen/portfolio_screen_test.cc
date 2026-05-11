#include "portfolio_screen.h"

#include <gtest/gtest.h>

#include "screen.h"
#include "support/fake_ibkr_client.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

class PortfolioScreenTest : public ::testing::Test {
 protected:
  test::FakeIbkrClient client;
  test::SpyRenderBackend backend{120, 20};
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

TEST_F(PortfolioScreenTest, OnEnterSubscribesAndOnExitUnsubscribes) {
  PortfolioScreen screen;
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.subscribe_portfolio_calls, 1);
  screen.OnExit(ctx);
  EXPECT_EQ(client.unsubscribe_portfolio_calls, 1);
}

TEST_F(PortfolioScreenTest, EmptyAccountsHeaderShowsWaitingMessage) {
  PortfolioScreen screen;
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);
  EXPECT_NE(backend.Row(2).find("waiting for managed accounts"),
            std::string::npos);
}

TEST_F(PortfolioScreenTest, ReadyPortfolioRendersTotalsAndTable) {
  PortfolioSnapshot snap;
  snap.ready = true;
  snap.accounts = {"DU1234"};
  AccountTotals tot;
  tot.account = "DU1234";
  tot.values["NetLiquidation"] = "10000.00";
  tot.values["TotalCashValue"] = "2500.00";
  tot.values["RT_DailyPnL"] = "150.00";
  snap.totals.push_back(tot);
  snap.positions.push_back({.symbol = "AAPL",
                             .sec_type = "STK",
                             .currency = "USD",
                             .accounts = "DU1234",
                             .position = 100,
                             .avg_cost = 150.0,
                             .market_value = 16000.0,
                             .daily_pnl = 100.0,
                             .unrealized_pnl = 1000.0,
                             .realized_pnl = 0.0});
  client.SetPortfolio(snap);

  PortfolioScreen screen;
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnFrame(ctx);
  screen.Render(ctx);

  EXPECT_NE(backend.Row(2).find("PORTFOLIO"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("Net Liq"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("+10000"), std::string::npos);
  // Table header row (row 4 = body_row 2).
  EXPECT_NE(backend.Row(4).find("SYMBOL"), std::string::npos);
  EXPECT_NE(backend.Row(4).find("POS"), std::string::npos);
  // Position row.
  EXPECT_NE(backend.Row(5).find("AAPL"), std::string::npos);
  EXPECT_NE(backend.Row(5).find("100"), std::string::npos);
}

TEST_F(PortfolioScreenTest, NotConnectedSkipsFrame) {
  client.SetConnected(false);
  PortfolioScreen screen;
  auto ctx = MakeCtx();
  screen.OnFrame(ctx);
  screen.Render(ctx);
  // No header rendered (block is empty).
  EXPECT_EQ(backend.Row(2), "");
}

}  // namespace
}  // namespace dlterm
