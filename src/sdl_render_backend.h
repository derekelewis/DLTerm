#ifndef DLTERM_SRC_SDL_RENDER_BACKEND_H_
#define DLTERM_SRC_SDL_RENDER_BACKEND_H_

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <memory>

#include "render_backend.h"
#include "text_grid.h"

namespace dlterm {

// Concrete RenderBackend wrapping SDL3 + glyph caches. Owns three
// glyph caches (body / bold body / accent) so DrawTextLine can pick
// based on `TextStyle`.
class SdlRenderBackend : public RenderBackend {
 public:
  SdlRenderBackend(SDL_Window* window, SDL_Renderer* renderer,
                   TTF_Font* body_font, TTF_Font* bold_font,
                   Color body_fg, Color accent_fg);
  ~SdlRenderBackend() override;

  SdlRenderBackend(const SdlRenderBackend&) = delete;
  SdlRenderBackend& operator=(const SdlRenderBackend&) = delete;

  // Refresh cached metrics; call from the main loop on resize.
  void RefreshMetrics();

  GridMetrics Metrics() const override { return metrics_; }
  int WindowWidthPx() const override;
  int WindowHeightPx() const override;
  float PixelDensity() const override;

  void DrawTextLine(int row, std::string_view text,
                    TextStyle style = TextStyle::kBody,
                    int y_offset_px = 0) override;
  void FillRect(const Rect& r, Color c) override;
  void DrawRectOutline(const Rect& r, Color c) override;
  void DrawLineSegment(Point a, Point b, Color c) override;
  void FillTriangle(Point a, Point b, Point ctri, Color color) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  GridMetrics metrics_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SDL_RENDER_BACKEND_H_
