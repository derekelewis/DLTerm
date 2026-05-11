#ifndef DLTERM_SRC_PORTFOLIO_SCREEN_H_
#define DLTERM_SRC_PORTFOLIO_SCREEN_H_

#include "screen.h"
#include "widgets.h"

namespace dlterm {

// Real-time multi-account portfolio. Subscribes on enter, unsubscribes
// on exit. Re-renders body each frame from a fresh snapshot.
class PortfolioScreen : public Screen {
 public:
  void OnEnter(ScreenContext& ctx) override;
  void OnExit(ScreenContext& ctx) override;
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override { block_.Render(ctx); }

 private:
  TextBlock block_;
  Table table_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_PORTFOLIO_SCREEN_H_
