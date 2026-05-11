#ifndef DLTERM_SRC_DEBUG_SCREEN_H_
#define DLTERM_SRC_DEBUG_SCREEN_H_

#include "screen.h"
#include "widgets.h"

namespace dlterm {

// Live diagnostics overlay. Accessible via the DEBUG verb. Shows
// frame timing, TWS state, current screen name, and the recent log
// tail (drawn from the log ring buffer).
class DebugScreen : public Screen {
 public:
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override { block_.Render(ctx); }
  bool OnEvent(const SDL_Event& e, ScreenContext& ctx) override;

 private:
  TextBlock block_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_DEBUG_SCREEN_H_
