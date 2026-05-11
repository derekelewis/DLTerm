#include "function_bar.h"

#include <gtest/gtest.h>

#include "support/spy_render_backend.h"

namespace dlterm {
namespace {

constexpr Color kOutline{60, 120, 195, 255};

TEST(FunctionBarRender, EmptyBarShowsTriangleAndCursor) {
  test::SpyRenderBackend backend(40, 10);
  FunctionBar bar;
  // Cursor visible at now_ms=0 (since cursor blinks every 500ms; 0
  // sits in the visible window).
  DrawFunctionBar(backend, bar, backend.Metrics(), kOutline, /*now_ms=*/0);
  EXPECT_EQ(backend.TriangleCount(), 1u);  // prompt arrow
  EXPECT_EQ(backend.FillRectCount(), 1u);  // cursor block
  // Rounded rect is 8 line segments.
  EXPECT_EQ(backend.LineSegmentCount(), 8u);
  // Empty input → text line still drawn (just the prefix padding).
  EXPECT_EQ(backend.TextCount(), 1u);
}

TEST(FunctionBarRender, InputCharsAppearAfterPrefix) {
  test::SpyRenderBackend backend(40, 10);
  FunctionBar bar;
  bar.input = "AAPL NEWS";
  bar.cursor = bar.input.size();
  DrawFunctionBar(backend, bar, backend.Metrics(), kOutline, /*now_ms=*/0);
  // 2-col left padding + "" prompt → input begins at col 2.
  const std::string row0 = backend.Row(0);
  EXPECT_EQ(row0, "  AAPL NEWS");
}

TEST(FunctionBarRender, StatusReplacesInput) {
  test::SpyRenderBackend backend(40, 10);
  FunctionBar bar;
  bar.input = "ignored";
  bar.status = "Connection failed";
  DrawFunctionBar(backend, bar, backend.Metrics(), kOutline, /*now_ms=*/0);
  EXPECT_EQ(backend.Row(0), "  Connection failed");
  // No cursor block when displaying status.
  EXPECT_EQ(backend.FillRectCount(), 0u);
}

TEST(FunctionBarRender, CursorBlinksOff) {
  test::SpyRenderBackend backend(40, 10);
  FunctionBar bar;
  // 500ms passed → blink cycle places cursor in OFF half.
  DrawFunctionBar(backend, bar, backend.Metrics(), kOutline, /*now_ms=*/500);
  EXPECT_EQ(backend.FillRectCount(), 0u);
  // 1000ms → back ON.
  backend.Clear();
  DrawFunctionBar(backend, bar, backend.Metrics(), kOutline, /*now_ms=*/1000);
  EXPECT_EQ(backend.FillRectCount(), 1u);
}

}  // namespace
}  // namespace dlterm
