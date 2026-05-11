#ifndef DLTERM_SRC_TEXT_GRID_H_
#define DLTERM_SRC_TEXT_GRID_H_

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <array>
#include <string_view>

namespace dlterm {

struct GridMetrics {
  int cols = 0;
  int rows = 0;
  int cell_w = 0;
  int cell_h = 0;
};

GridMetrics ComputeGridMetrics(SDL_Window* window, TTF_Font* font);

class GlyphCache {
 public:
  GlyphCache(SDL_Renderer* renderer, TTF_Font* font, SDL_Color color);
  ~GlyphCache();

  GlyphCache(const GlyphCache&) = delete;
  GlyphCache& operator=(const GlyphCache&) = delete;

  SDL_Texture* Get(char c);

 private:
  SDL_Renderer* renderer_;
  TTF_Font* font_;
  SDL_Color color_;
  std::array<SDL_Texture*, 128> cache_{};
};

void DrawLine(SDL_Renderer* renderer, GlyphCache* cache,
              std::string_view line, int row, const GridMetrics& metrics,
              int y_offset_px = 0);

}  // namespace dlterm

#endif  // DLTERM_SRC_TEXT_GRID_H_
