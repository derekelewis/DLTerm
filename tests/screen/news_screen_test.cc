#include "news_screen.h"

#include <gtest/gtest.h>

#include "screen.h"
#include "support/fake_ibkr_client.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

class NewsScreenTest : public ::testing::Test {
 protected:
  test::FakeIbkrClient client;
  test::SpyRenderBackend backend{120, 30};
  FunctionBar bar;

  ScreenContext MakeCtx(std::vector<std::string>* statuses = nullptr) {
    ScreenContext ctx;
    ctx.render = &backend;
    ctx.metrics = backend.Metrics();
    ctx.win_w_px = backend.WindowWidthPx();
    ctx.win_h_px = backend.WindowHeightPx();
    ctx.pixel_density = backend.PixelDensity();
    ctx.client = &client;
    ctx.bar = &bar;
    ctx.frame_statuses = statuses;
    return ctx;
  }

  void SetUp() override { client.SetConnected(true); }
};

TEST_F(NewsScreenTest, OnEnterSubscribesToTickerNews) {
  NewsScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.subscribe_news_calls, 1);
  EXPECT_EQ(client.last_news_symbol, "AAPL");
  EXPECT_EQ(client.subscribe_general_calls, 0);
}

TEST_F(NewsScreenTest, OnEnterEmptySymbolStartsGeneralNews) {
  NewsScreen screen("");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  EXPECT_EQ(client.subscribe_general_calls, 1);
  EXPECT_EQ(client.subscribe_news_calls, 0);
}

TEST_F(NewsScreenTest, OnExitCancelsNews) {
  NewsScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  screen.OnExit(ctx);
  EXPECT_EQ(client.cancel_news_calls, 1);
}

TEST_F(NewsScreenTest, RendersHeaderWithLoadingThenLoadedCount) {
  NewsScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);

  // Frame 1: no headlines, no status. Header says "(loading...)".
  screen.OnFrame(ctx);
  screen.Render(ctx);
  EXPECT_NE(backend.Row(2).find("loading"), std::string::npos);

  backend.Clear();

  // Frame 2: push a headline + the loaded status.
  client.PushHeadline({.ts = 1700000000,
                        .provider = "BRFG",
                        .article_id = "a1",
                        .headline = "Hello"});
  std::vector<std::string> statuses = {"Historical loaded: 12 headlines"};
  auto ctx2 = MakeCtx(&statuses);
  screen.OnFrame(ctx2);
  screen.Render(ctx2);
  EXPECT_NE(backend.Row(2).find("AAPL"), std::string::npos);
  EXPECT_NE(backend.Row(2).find("1 headlines"), std::string::npos);
  // Headline shown on row 4 (after header + blank line).
  EXPECT_NE(backend.Row(4).find("Hello"), std::string::npos);
  EXPECT_NE(backend.Row(4).find("BRFG"), std::string::npos);
}

TEST_F(NewsScreenTest, EscFromReaderModeReturnsToFeed) {
  NewsScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);
  client.PushHeadline({.ts = 1700000000,
                        .provider = "BRFG",
                        .article_id = "a1",
                        .headline = "Hello"});
  screen.OnFrame(ctx);

  // Click on the headline row (body_row 2 = header rows 2 + 0 = grid row 4).
  // body_row 2 in body coordinates → grid row 4.
  // Convert body_row 2 to event_y: pixel_y = (body_row + 2) * cell_h.
  const float cell_h = static_cast<float>(backend.Metrics().cell_h);
  const float event_y = (2.0f + 2.0f) * cell_h;  // body_row 2 (third headline slot offset)
  // Adjust to body_row 0 so it matches our single headline (kFeedHeaderRows=2):
  // headline idx = body_row - kFeedHeaderRows = 0 → body_row 2.
  SDL_Event click{};
  click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  click.button.button = SDL_BUTTON_LEFT;
  click.button.y = event_y;
  EXPECT_TRUE(screen.OnEvent(click, ctx));
  EXPECT_EQ(client.request_article_calls, 1);
  EXPECT_EQ(client.last_article_id, "a1");

  // ESC consumes within the screen and returns to feed.
  SDL_Event esc{};
  esc.type = SDL_EVENT_KEY_DOWN;
  esc.key.key = SDLK_ESCAPE;
  EXPECT_TRUE(screen.OnEvent(esc, ctx));

  // ESC again at feed level is NOT consumed (returns false → main loop drops to body).
  EXPECT_FALSE(screen.OnEvent(esc, ctx));
}

TEST_F(NewsScreenTest, NotConnectedSetsBarStatus) {
  client.SetConnected(false);
  NewsScreen screen("AAPL");
  auto ctx = MakeCtx();
  screen.OnEnter(ctx);  // SubscribeNews returns 0 when not connected
  client.SetConnected(true);  // Reconnect for the click flow
  screen.OnFrame(ctx);
  // Push a headline so the click has something to land on.
  client.PushHeadline({.ts = 1700000000,
                        .provider = "BRFG",
                        .article_id = "a1",
                        .headline = "Hello"});
  screen.OnFrame(ctx);

  // Now disconnect again, so RequestArticle returns 0.
  client.SetConnected(false);
  const float cell_h = static_cast<float>(backend.Metrics().cell_h);
  SDL_Event click{};
  click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  click.button.button = SDL_BUTTON_LEFT;
  click.button.y = (2.0f + 2.0f) * cell_h;
  screen.OnEvent(click, ctx);
  EXPECT_EQ(bar.status, "Not connected");
}

}  // namespace
}  // namespace dlterm
