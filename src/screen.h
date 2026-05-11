#ifndef DLTERM_SRC_SCREEN_H_
#define DLTERM_SRC_SCREEN_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

#include "function_bar.h"
#include "ibkr_client.h"
#include "render_backend.h"
#include "text_grid.h"

namespace dlterm {

// Per-frame context handed to screens. Mutable; screens may write to
// `bar->status` to surface messages, etc.
struct ScreenContext {
  RenderBackend* render = nullptr;
  GridMetrics metrics;
  int win_w_px = 0;
  int win_h_px = 0;
  float pixel_density = 1.0f;
  Uint64 now_ms = 0;
  IIbkrClient* client = nullptr;
  FunctionBar* bar = nullptr;
  // Status messages drained from the IbkrClient this frame. Screens
  // may inspect this (e.g. to react to "Historical loaded:") but
  // should not consume it; main loop already updated `bar->status`.
  const std::vector<std::string>* frame_statuses = nullptr;
};

class Screen {
 public:
  virtual ~Screen() = default;

  // Lifecycle.
  virtual void OnEnter(ScreenContext& ctx) {}
  virtual void OnExit(ScreenContext& ctx) {}

  // Per-frame tick. Drain client APIs, update internal state.
  virtual void OnFrame(ScreenContext& ctx) {}

  // Render the body area (function bar is drawn by the main loop
  // before this is called).
  virtual void Render(ScreenContext& ctx) = 0;

  // Returns true if the event was consumed and should NOT fall through
  // to the main loop's default handling.
  virtual bool OnEvent(const SDL_Event& e, ScreenContext& ctx) {
    return false;
  }

  // Hooks for the INFO screen to report body stats.
  virtual std::size_t BodyLineCount() const { return 0; }
  virtual std::size_t BodyWordCount() const { return 0; }
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SCREEN_H_
