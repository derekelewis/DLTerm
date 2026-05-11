#ifndef DLTERM_SRC_SPLASH_SCREEN_H_
#define DLTERM_SRC_SPLASH_SCREEN_H_

#include <SDL3/SDL.h>

#include <string>

#include "screen.h"
#include "widgets.h"

namespace dlterm {

// The home / splash screen. Shows the dlterm banner, world clocks,
// and a TWS connection metadata block. Re-layouts every frame so the
// clocks tick. On enter, attempts to connect to TWS using the
// configured host/port/client_id (auto-connect); failures surface on
// the function bar status line.
class SplashScreen : public Screen {
 public:
  SplashScreen(std::string host, int port, int client_id)
      : host_(std::move(host)), port_(port), client_id_(client_id) {}

  void OnEnter(ScreenContext& ctx) override;
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override { block_.Render(ctx); }
  bool OnEvent(const SDL_Event& e, ScreenContext& ctx) override;

 private:
  void Layout(ScreenContext& ctx);

  std::string host_;
  int port_ = 0;
  int client_id_ = 0;
  Uint64 last_server_time_req_ms_ = 0;
  TextBlock block_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SPLASH_SCREEN_H_
