#ifndef DLTERM_SRC_BODY_SCREEN_H_
#define DLTERM_SRC_BODY_SCREEN_H_

#include <string>

#include "screen.h"
#include "widgets.h"

namespace dlterm {

// Renders a static block of text (sample text or `INFO` output).
// No subscriptions, no per-frame work. Re-layouts on resize.
class BodyScreen : public Screen {
 public:
  explicit BodyScreen(std::string text) : text_(std::move(text)) {}

  void OnEnter(ScreenContext& ctx) override { Layout(ctx); }

  bool OnEvent(const SDL_Event& e, ScreenContext& ctx) override {
    if (e.type == SDL_EVENT_WINDOW_RESIZED ||
        e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
      Layout(ctx);
    }
    return false;
  }

  void Render(ScreenContext& ctx) override { block_.Render(ctx); }

  std::size_t BodyLineCount() const override { return block_.LineCount(); }
  std::size_t BodyWordCount() const override;

 private:
  void Layout(ScreenContext& ctx);

  std::string text_;
  TextBlock block_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_BODY_SCREEN_H_
