#ifndef DLTERM_SRC_RENDER_BACKEND_H_
#define DLTERM_SRC_RENDER_BACKEND_H_

#include <cstdint>
#include <string_view>

#include "text_grid.h"

namespace dlterm {

struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  friend constexpr bool operator==(const Color&, const Color&) = default;
};

struct Point {
  float x = 0.0f;
  float y = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Abstract drawing surface. The SDL implementation lives in
// sdl_render_backend; tests use SpyRenderBackend (under tests/support)
// which records every call.
//
// All "px" coordinates are pixels in the backbuffer's coordinate
// system. Text is positioned in grid units (cols/rows).
class RenderBackend {
 public:
  enum class TextStyle {
    kBody,      // primary fg color (orange in production)
    kBodyBold,  // bold variant of kBody
    kAccent,    // contrast / status / labels (white in production)
  };

  virtual ~RenderBackend() = default;

  // Surface info — typically forwarded from the underlying window.
  virtual GridMetrics Metrics() const = 0;
  virtual int WindowWidthPx() const = 0;
  virtual int WindowHeightPx() const = 0;
  virtual float PixelDensity() const = 0;

  // One full grid row of text at row index `row` (0 = top of window).
  // `y_offset_px` shifts vertically within the cell — used by the
  // function bar to center text within its decorative rect.
  virtual void DrawTextLine(int row, std::string_view text,
                            TextStyle style = TextStyle::kBody,
                            int y_offset_px = 0) = 0;

  // Pixel-precise primitives.
  virtual void FillRect(const Rect& r, Color c) = 0;
  virtual void DrawRectOutline(const Rect& r, Color c) = 0;
  virtual void DrawLineSegment(Point a, Point b, Color c) = 0;
  virtual void FillTriangle(Point a, Point b, Point c, Color color) = 0;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_RENDER_BACKEND_H_
