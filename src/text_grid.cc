#include "text_grid.h"

#include <cstddef>

namespace dlterm {

GridMetrics ComputeGridMetrics(SDL_Window* window, TTF_Font* font) {
  GridMetrics m;

  int win_w = 0;
  int win_h = 0;
  SDL_GetWindowSizeInPixels(window, &win_w, &win_h);

  int probe_w = 0;
  int probe_h = 0;
  TTF_GetStringSize(font, "M", 1, &probe_w, &probe_h);
  m.cell_w = probe_w > 0 ? probe_w : 1;

  const int line_skip = TTF_GetFontHeight(font);
  m.cell_h = line_skip > 0 ? line_skip : (probe_h > 0 ? probe_h : 1);

  m.cols = win_w / m.cell_w;
  m.rows = win_h / m.cell_h;
  return m;
}

GlyphCache::GlyphCache(SDL_Renderer* renderer, TTF_Font* font, SDL_Color color)
    : renderer_(renderer), font_(font), color_(color) {}

GlyphCache::~GlyphCache() {
  for (SDL_Texture* t : cache_) {
    if (t) SDL_DestroyTexture(t);
  }
}

SDL_Texture* GlyphCache::Get(char c) {
  const unsigned char idx = static_cast<unsigned char>(c);
  if (idx >= cache_.size()) return nullptr;
  if (cache_[idx]) return cache_[idx];
  if (idx < 0x20) return nullptr;

  SDL_Surface* surface =
      TTF_RenderGlyph_Blended(font_, static_cast<Uint32>(idx), color_);
  if (!surface) return nullptr;
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_DestroySurface(surface);
  cache_[idx] = texture;
  return texture;
}

void DrawLine(SDL_Renderer* renderer, GlyphCache* cache,
              std::string_view line, int row, const GridMetrics& metrics,
              int y_offset_px) {
  const int max_cols = metrics.cols;
  const float y = static_cast<float>(row * metrics.cell_h + y_offset_px);
  for (std::size_t col = 0;
       col < line.size() && static_cast<int>(col) < max_cols; ++col) {
    SDL_Texture* tex = cache->Get(line[col]);
    if (!tex) continue;
    float tw = 0.0f;
    float th = 0.0f;
    SDL_GetTextureSize(tex, &tw, &th);
    SDL_FRect dst{
        static_cast<float>(static_cast<int>(col) * metrics.cell_w),
        y,
        tw,
        th,
    };
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
  }
}

}  // namespace dlterm
