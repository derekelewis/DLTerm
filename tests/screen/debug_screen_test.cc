#include "debug_screen.h"

#include <gtest/gtest.h>

#include <string>

#include "diagnostics.h"
#include "log.h"
#include "screen.h"
#include "support/fake_ibkr_client.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

class DebugScreenTest : public ::testing::Test {
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

  void SetUp() override {
    diag::ResetForTesting();
    log::Shutdown();
    log::SetFilterForTesting("trace");
  }
  void TearDown() override {
    log::Shutdown();
    diag::ResetForTesting();
  }
};

TEST_F(DebugScreenTest, RendersDiagnosticsAndLogTail) {
  diag::SetCurrentScreen("PortfolioScreen");
  diag::SetTwsTarget("127.0.0.1", 4002, 1);
  diag::SetTwsConnected(true);
  diag::RecordFrame(16.0);
  diag::RecordFrame(17.0);
  log::Emit(log::Level::kInfo, "test", "hello world");

  DebugScreen screen;
  auto ctx = MakeCtx();
  screen.OnFrame(ctx);
  screen.Render(ctx);

  EXPECT_NE(backend.Row(2).find("DEBUG"), std::string::npos);
  // Field rows.
  bool saw_screen = false, saw_tws = false, saw_frame = false, saw_log = false;
  for (int r = 2; r < 30; ++r) {
    const std::string row = backend.Row(r);
    if (row.find("PortfolioScreen") != std::string::npos) saw_screen = true;
    if (row.find("CONNECTED") != std::string::npos &&
        row.find("127.0.0.1") != std::string::npos) {
      saw_tws = true;
    }
    if (row.find("FRAME") != std::string::npos &&
        row.find("avg=") != std::string::npos) {
      saw_frame = true;
    }
    if (row.find("hello world") != std::string::npos) saw_log = true;
  }
  EXPECT_TRUE(saw_screen);
  EXPECT_TRUE(saw_tws);
  EXPECT_TRUE(saw_frame);
  EXPECT_TRUE(saw_log);
}

TEST_F(DebugScreenTest, ShowsDisconnectedWhenNoTwsState) {
  DebugScreen screen;
  auto ctx = MakeCtx();
  screen.OnFrame(ctx);
  screen.Render(ctx);
  bool saw_disconnected = false;
  for (int r = 2; r < 30; ++r) {
    if (backend.Row(r).find("disconnected") != std::string::npos) {
      saw_disconnected = true;
      break;
    }
  }
  EXPECT_TRUE(saw_disconnected);
}

}  // namespace
}  // namespace dlterm
