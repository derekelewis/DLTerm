#include "widgets.h"

#include <gtest/gtest.h>

#include "screen.h"
#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

ScreenContext MakeContext(test::SpyRenderBackend& backend) {
  ScreenContext ctx;
  ctx.render = &backend;
  ctx.metrics = backend.Metrics();
  ctx.win_w_px = backend.WindowWidthPx();
  ctx.win_h_px = backend.WindowHeightPx();
  ctx.pixel_density = backend.PixelDensity();
  return ctx;
}

TEST(TextBlockRender, WritesEachLineToCorrectBodyRow) {
  test::SpyRenderBackend backend(40, 10);
  TextBlock block;
  block.SetLines({"alpha", "beta", "gamma"});
  auto ctx = MakeContext(backend);
  block.Render(ctx);
  // First body row is kBarReservedRows (== 2).
  EXPECT_EQ(backend.Row(2), "alpha");
  EXPECT_EQ(backend.Row(3), "beta");
  EXPECT_EQ(backend.Row(4), "gamma");
  EXPECT_EQ(backend.TextCount(), 3u);
}

TEST(TextBlockRender, BoldRowsUseBoldStyle) {
  test::SpyRenderBackend backend(40, 10);
  TextBlock block;
  block.SetLines({"plain", "bold", "plain"});
  block.SetBoldRows({1});
  auto ctx = MakeContext(backend);
  block.Render(ctx);
  ASSERT_EQ(backend.Calls().size(), 3u);
  EXPECT_EQ(backend.Calls()[0].style, RenderBackend::TextStyle::kBody);
  EXPECT_EQ(backend.Calls()[1].style, RenderBackend::TextStyle::kBodyBold);
  EXPECT_EQ(backend.Calls()[2].style, RenderBackend::TextStyle::kBody);
}

TEST(TextBlockRender, ClipsToBodyRowCount) {
  // 4-row grid → 2 body rows after kBarReservedRows.
  test::SpyRenderBackend backend(40, 4);
  TextBlock block;
  block.SetLines({"a", "b", "c", "d"});
  auto ctx = MakeContext(backend);
  block.Render(ctx);
  EXPECT_EQ(backend.TextCount(), 2u);
  EXPECT_EQ(backend.Row(2), "a");
  EXPECT_EQ(backend.Row(3), "b");
}

TEST(ScrollableTextBlockRender, RendersHeaderAndScrolledBody) {
  test::SpyRenderBackend backend(40, 10);  // 8 body rows
  ScrollableTextBlock block;
  block.SetHeader({"H1", "H2"});
  block.SetBody({"line0", "line1", "line2", "line3", "line4"});
  auto ctx = MakeContext(backend);
  block.Render(ctx);
  EXPECT_EQ(backend.Row(2), "H1");
  EXPECT_EQ(backend.Row(3), "H2");
  EXPECT_EQ(backend.Row(4), "line0");
  EXPECT_EQ(backend.Row(5), "line1");
}

TEST(DrawHoverRectRender, EmitsOneOutlineCall) {
  test::SpyRenderBackend backend(40, 10);
  DrawHoverRect(backend, backend.Metrics(), /*body_row=*/3);
  EXPECT_EQ(backend.OutlineCount(), 1u);
  ASSERT_EQ(backend.Calls().size(), 1u);
  const auto& call = backend.Calls()[0];
  EXPECT_EQ(call.kind, test::SpyRenderBackend::Call::Kind::kOutline);
  // Width = cols * cell_w; height = cell_h.
  EXPECT_FLOAT_EQ(call.rect.w, 40.0f * 10.0f);
  EXPECT_FLOAT_EQ(call.rect.h, 18.0f);
}

TEST(DrawHoverRectRender, NegativeBodyRowIsNoOp) {
  test::SpyRenderBackend backend(40, 10);
  DrawHoverRect(backend, backend.Metrics(), /*body_row=*/-1);
  EXPECT_EQ(backend.OutlineCount(), 0u);
}

TEST(MouseYToBodyRowUnit, AccountsForPixelDensity) {
  // 18 px per row, density 2 → mouse y in points doubles.
  GridMetrics m{.rows = 10, .cell_h = 18};
  // event_y=18 (points) * 2 density → 36 px → row 2 → body_row 0.
  EXPECT_EQ(MouseYToBodyRow(2.0f, m, 18.0f), 0);
  // event_y=27 (points) * 2 → 54 → row 3 → body_row 1.
  EXPECT_EQ(MouseYToBodyRow(2.0f, m, 27.0f), 1);
  // Above body returns negative.
  EXPECT_LT(MouseYToBodyRow(2.0f, m, 0.0f), 0);
}

TEST(MouseYToBodyRowUnit, ZeroCellHeightReturnsNegative) {
  GridMetrics m{.rows = 10, .cell_h = 0};
  EXPECT_EQ(MouseYToBodyRow(1.0f, m, 50.0f), -1);
}

}  // namespace
}  // namespace dlterm
