#include "sdl_render_backend.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstddef>

#include "text_grid.h"

namespace dlterm {

namespace {

SDL_Color ToSDL(Color c) { return SDL_Color{c.r, c.g, c.b, c.a}; }
SDL_FColor ToSDLF(Color c) {
  return SDL_FColor{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

}  // namespace

struct SdlRenderBackend::Impl {
  Impl(SDL_Window* w, SDL_Renderer* r, TTF_Font* body, TTF_Font* bold,
       Color body_fg, Color accent_fg)
      : window(w),
        renderer(r),
        body_font(body),
        bold_font(bold ? bold : body),
        body_cache(r, body, ToSDL(body_fg)),
        bold_cache(r, bold ? bold : body, ToSDL(body_fg)),
        accent_cache(r, body, ToSDL(accent_fg)) {}

  SDL_Window* window;
  SDL_Renderer* renderer;
  TTF_Font* body_font;
  TTF_Font* bold_font;
  GlyphCache body_cache;
  GlyphCache bold_cache;
  GlyphCache accent_cache;
};

SdlRenderBackend::SdlRenderBackend(SDL_Window* window, SDL_Renderer* renderer,
                                   TTF_Font* body_font, TTF_Font* bold_font,
                                   Color body_fg, Color accent_fg)
    : impl_(std::make_unique<Impl>(window, renderer, body_font, bold_font,
                                    body_fg, accent_fg)) {
  RefreshMetrics();
}

SdlRenderBackend::~SdlRenderBackend() = default;

void SdlRenderBackend::RefreshMetrics() {
  metrics_ = ComputeGridMetrics(impl_->window, impl_->body_font);
}

int SdlRenderBackend::WindowWidthPx() const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(impl_->window, &w, &h);
  return w;
}

int SdlRenderBackend::WindowHeightPx() const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(impl_->window, &w, &h);
  return h;
}

float SdlRenderBackend::PixelDensity() const {
  const float d = SDL_GetWindowPixelDensity(impl_->window);
  return d > 0.0f ? d : 1.0f;
}

void SdlRenderBackend::DrawTextLine(int row, std::string_view text,
                                    TextStyle style, int y_offset_px) {
  GlyphCache* cache = nullptr;
  switch (style) {
    case TextStyle::kBody:     cache = &impl_->body_cache; break;
    case TextStyle::kBodyBold: cache = &impl_->bold_cache; break;
    case TextStyle::kAccent:   cache = &impl_->accent_cache; break;
  }
  DrawLine(impl_->renderer, cache, text, row, metrics_, y_offset_px);
}

void SdlRenderBackend::FillRect(const Rect& r, Color c) {
  SDL_SetRenderDrawColor(impl_->renderer, c.r, c.g, c.b, c.a);
  SDL_FRect fr{r.x, r.y, r.w, r.h};
  SDL_RenderFillRect(impl_->renderer, &fr);
}

void SdlRenderBackend::DrawRectOutline(const Rect& r, Color c) {
  SDL_SetRenderDrawColor(impl_->renderer, c.r, c.g, c.b, c.a);
  SDL_FRect fr{r.x, r.y, r.w, r.h};
  SDL_RenderRect(impl_->renderer, &fr);
}

void SdlRenderBackend::DrawLineSegment(Point a, Point b, Color c) {
  SDL_SetRenderDrawColor(impl_->renderer, c.r, c.g, c.b, c.a);
  SDL_RenderLine(impl_->renderer, a.x, a.y, b.x, b.y);
}

void SdlRenderBackend::FillTriangle(Point a, Point b, Point c, Color color) {
  const SDL_FColor fc = ToSDLF(color);
  const SDL_Vertex tri[3] = {
      {{a.x, a.y}, fc, {0.0f, 0.0f}},
      {{b.x, b.y}, fc, {0.0f, 0.0f}},
      {{c.x, c.y}, fc, {0.0f, 0.0f}},
  };
  SDL_RenderGeometry(impl_->renderer, nullptr, tri, 3, nullptr, 0);
}

}  // namespace dlterm
