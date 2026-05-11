#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.h"
#include "function_bar.h"
#include "ibkr_client.h"
#include "log.h"
#include "render_backend.h"
#include "screen.h"
#include "screen_factory.h"
#include "sdl_render_backend.h"
#include "splash_screen.h"
#include "text_grid.h"

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 800;
constexpr int kFontPtSize = 22;

constexpr dlterm::Color kBackground{0, 0, 0, 255};
constexpr dlterm::Color kPrimary{255, 176, 0, 255};
constexpr dlterm::Color kAccent{255, 255, 255, 255};
constexpr dlterm::Color kBarOutline{60, 120, 195, 255};

constexpr std::string_view kFontRelativePath =
    "assets/fonts/IBMPlexMono-Regular.ttf";

std::string ResolveAssetPath(std::string_view rel) {
  const char* base = SDL_GetBasePath();
  if (base) {
    std::string p = std::string{base} + std::string{rel};
    std::ifstream f{p};
    if (f) return p;
  }
  return std::string{rel};
}

struct TwsConfig {
  std::string host = "127.0.0.1";
  int port = 7497;
  int client_id = 1;
};

TwsConfig ReadTwsConfig() {
  TwsConfig c;
  if (const char* h = std::getenv("DLTERM_TWS_HOST"); h && *h) c.host = h;
  if (const char* p = std::getenv("DLTERM_TWS_PORT"); p && *p) {
    try { c.port = std::stoi(p); } catch (...) {}
  }
  if (const char* id = std::getenv("DLTERM_TWS_CLIENT_ID"); id && *id) {
    try { c.client_id = std::stoi(id); } catch (...) {}
  }
  return c;
}

dlterm::TerminalInfo BuildInfo(SDL_Window* window, SDL_Renderer* renderer,
                               const dlterm::GridMetrics& metrics, int font_pt,
                               const std::string& font_path,
                               std::size_t body_lines, std::size_t body_words) {
  dlterm::TerminalInfo info;
  SDL_GetWindowSize(window, &info.win_w_pt, &info.win_h_pt);
  SDL_GetWindowSizeInPixels(window, &info.win_w_px, &info.win_h_px);
  info.pixel_density = SDL_GetWindowPixelDensity(window);
  info.cols = metrics.cols;
  info.rows = metrics.rows;
  info.cell_w = metrics.cell_w;
  info.cell_h = metrics.cell_h;
  info.font_pt = font_pt;
  info.font_path = font_path;
  const char* rname = SDL_GetRendererName(renderer);
  info.renderer_name = rname ? rname : "(unknown)";
  info.body_lines = body_lines;
  info.body_words = body_words;
  return info;
}

}  // namespace

int main(int argc, char* argv[]) {
  dlterm::log::Init();
  DLTERM_LOG_INFO("main", "dlterm starting");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (!TTF_Init()) {
    std::fprintf(stderr, "TTF_Init failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Window* window =
      SDL_CreateWindow("DLTerm", kWindowW, kWindowH,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  const float pixel_density = SDL_GetWindowPixelDensity(window);
  const float scale = pixel_density > 0.0f ? pixel_density : 1.0f;
  const std::string font_path = ResolveAssetPath(kFontRelativePath);
  TTF_Font* font = TTF_OpenFont(font_path.c_str(), kFontPtSize * scale);
  if (!font) {
    std::fprintf(stderr, "TTF_OpenFont(%s) failed: %s\n", font_path.c_str(),
                 SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
  TTF_Font* bold_font = TTF_OpenFont(font_path.c_str(), kFontPtSize * scale);
  if (bold_font) TTF_SetFontStyle(bold_font, TTF_STYLE_BOLD);

  SDL_StartTextInput(window);

  {
    dlterm::SdlRenderBackend backend(window, renderer, font, bold_font,
                                     kPrimary, kAccent);

    dlterm::FunctionBar bar;
    dlterm::IbkrClient client;
    const TwsConfig tws_cfg = ReadTwsConfig();
    dlterm::diag::SetTwsTarget(tws_cfg.host, tws_cfg.port, tws_cfg.client_id);
    dlterm::diag::SetCurrentScreen("SplashScreen");

    std::unique_ptr<dlterm::Screen> current_screen =
        std::make_unique<dlterm::SplashScreen>(tws_cfg.host, tws_cfg.port,
                                                tws_cfg.client_id);

    auto build_ctx = [&](Uint64 now_ms,
                          const std::vector<std::string>* statuses)
        -> dlterm::ScreenContext {
      int wpx = 0, hpx = 0;
      SDL_GetWindowSizeInPixels(window, &wpx, &hpx);
      dlterm::ScreenContext ctx;
      ctx.render = &backend;
      ctx.metrics = backend.Metrics();
      ctx.win_w_px = wpx;
      ctx.win_h_px = hpx;
      ctx.pixel_density = backend.PixelDensity();
      ctx.now_ms = now_ms;
      ctx.client = &client;
      ctx.bar = &bar;
      ctx.frame_statuses = statuses;
      return ctx;
    };

    auto reinstall_body = [&]() {
      auto ctx = build_ctx(SDL_GetTicks(), nullptr);
      if (current_screen) current_screen->OnExit(ctx);
      current_screen = std::make_unique<dlterm::SplashScreen>(
          tws_cfg.host, tws_cfg.port, tws_cfg.client_id);
      current_screen->OnEnter(ctx);
      dlterm::diag::SetCurrentScreen("SplashScreen");
    };

    {
      auto ctx = build_ctx(SDL_GetTicks(), nullptr);
      current_screen->OnEnter(ctx);
    }

    bool running = true;
    SDL_Event e;
    Uint64 frame_start = SDL_GetTicks();
    while (running) {
      const Uint64 now = SDL_GetTicks();
      const double dt_ms = static_cast<double>(now - frame_start);
      frame_start = now;
      if (now > 0) dlterm::diag::RecordFrame(dt_ms);
      dlterm::diag::SetTwsConnected(client.IsConnected());
      while (SDL_PollEvent(&e)) {
        // Quit shortcuts.
        if (e.type == SDL_EVENT_QUIT) { running = false; break; }
        if (e.type == SDL_EVENT_KEY_DOWN) {
          if (e.key.key == SDLK_Q && (e.key.mod & SDL_KMOD_GUI)) {
            running = false;
            break;
          }
        }

        // Window resize: refresh metrics, then notify the screen.
        if (e.type == SDL_EVENT_WINDOW_RESIZED ||
            e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
          backend.RefreshMetrics();
          if (current_screen) {
            auto ctx = build_ctx(SDL_GetTicks(), nullptr);
            current_screen->OnEvent(e, ctx);
          }
          const auto metrics = backend.Metrics();
          const std::size_t cap = dlterm::BarMaxInputCols(metrics.cols);
          if (bar.input.size() > cap) bar.input.resize(cap);
          if (bar.cursor > bar.input.size()) bar.cursor = bar.input.size();
          continue;
        }

        // Function-bar text input is global (not screen-owned).
        if (e.type == SDL_EVENT_TEXT_INPUT) {
          dlterm::HandleTextInput(&bar, e.text.text,
                                  dlterm::BarMaxInputCols(backend.Metrics().cols));
          continue;
        }

        // Function-bar editing keys are global, EXCEPT navigation keys
        // (Up, Down, PgUp, PgDn, Home, End) which screens may want for
        // scrolling. Try the screen first for those.
        if (e.type == SDL_EVENT_KEY_DOWN) {
          const SDL_Keycode k = e.key.key;
          if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            const std::size_t bl =
                current_screen ? current_screen->BodyLineCount() : 0;
            const std::size_t bw =
                current_screen ? current_screen->BodyWordCount() : 0;
            dlterm::CommandResult result = dlterm::RunCommand(
                bar.input,
                BuildInfo(window, renderer, backend.Metrics(), kFontPtSize,
                          font_path, bl, bw));
            DLTERM_LOG_DEBUG("command", "input='{}' kind={}", bar.input,
                              static_cast<int>(result.kind));
            if (result.kind == dlterm::CommandKind::kQuit) {
              DLTERM_LOG_INFO("main", "EXIT command — terminating");
              running = false;
              break;
            }
            if (result.kind == dlterm::CommandKind::kStartNews ||
                result.kind == dlterm::CommandKind::kStartGeneralNews ||
                result.kind == dlterm::CommandKind::kStartPortfolio ||
                result.kind == dlterm::CommandKind::kStartChart ||
                result.kind == dlterm::CommandKind::kStartQuote) {
              if (!client.IsConnected()) {
                if (!client.Connect(tws_cfg.host, tws_cfg.port,
                                    tws_cfg.client_id)) {
                  bar.status = std::string("TWS connect failed (")
                                   .append(tws_cfg.host)
                                   .append(":")
                                   .append(std::to_string(tws_cfg.port))
                                   .append(")");
                  bar.last_change_ms = SDL_GetTicks();
                  DLTERM_LOG_ERROR("tws", "connect failed host={} port={}",
                                    tws_cfg.host, tws_cfg.port);
                  continue;
                }
                DLTERM_LOG_INFO("tws", "connected host={} port={} client_id={}",
                                 tws_cfg.host, tws_cfg.port, tws_cfg.client_id);
              }
            }
            auto next = dlterm::MakeScreenFor(result);
            if (next) {
              auto ctx = build_ctx(SDL_GetTicks(), nullptr);
              if (current_screen) current_screen->OnExit(ctx);
              current_screen = std::move(next);
              current_screen->OnEnter(ctx);
              const char* sname =
                  result.kind == dlterm::CommandKind::kStartNews ? "NewsScreen"
                : result.kind == dlterm::CommandKind::kStartGeneralNews ? "NewsScreen"
                : result.kind == dlterm::CommandKind::kStartPortfolio ? "PortfolioScreen"
                : result.kind == dlterm::CommandKind::kStartChart ? "ChartScreen"
                : result.kind == dlterm::CommandKind::kStartQuote ? "QuoteScreen"
                : result.kind == dlterm::CommandKind::kStartDebug ? "DebugScreen"
                : "BodyScreen";
              dlterm::diag::SetCurrentScreen(sname);
              bar.input.clear();
              bar.cursor = 0;
              bar.status.clear();
              bar.last_change_ms = SDL_GetTicks();
            } else if (!result.status.empty()) {
              bar.status = std::move(result.status);
              bar.last_change_ms = SDL_GetTicks();
            }
            continue;
          }
          if (k == SDLK_ESCAPE) {
            // Give the screen first crack at ESC (e.g. reader sub-mode
            // returns to the feed). If it doesn't consume, drop back
            // to the body view.
            if (current_screen) {
              auto ctx = build_ctx(SDL_GetTicks(), nullptr);
              if (current_screen->OnEvent(e, ctx)) {
                dlterm::HandleEscape(&bar);
                continue;
              }
            }
            reinstall_body();
            dlterm::HandleEscape(&bar);
            continue;
          }
          // Editing keys handled by the function bar — but let the
          // screen consume Up/Down/PgUp/PgDn/Home/End first so reader
          // scrolling works.
          if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_PAGEUP ||
              k == SDLK_PAGEDOWN || k == SDLK_HOME || k == SDLK_END) {
            if (current_screen) {
              auto ctx = build_ctx(SDL_GetTicks(), nullptr);
              if (current_screen->OnEvent(e, ctx)) continue;
            }
          }
          if (k == SDLK_BACKSPACE) { dlterm::HandleBackspace(&bar); continue; }
          if (k == SDLK_DELETE)    { dlterm::HandleDelete(&bar);    continue; }
          if (k == SDLK_LEFT)      { dlterm::HandleCursorLeft(&bar); continue; }
          if (k == SDLK_RIGHT)     { dlterm::HandleCursorRight(&bar); continue; }
          if (k == SDLK_HOME)      { dlterm::HandleCursorHome(&bar); continue; }
          if (k == SDLK_END)       { dlterm::HandleCursorEnd(&bar);  continue; }
          // Other keys: forward to screen.
          if (current_screen) {
            auto ctx = build_ctx(SDL_GetTicks(), nullptr);
            current_screen->OnEvent(e, ctx);
          }
          continue;
        }

        // Mouse events go straight to the screen.
        if (e.type == SDL_EVENT_MOUSE_MOTION ||
            e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
            e.type == SDL_EVENT_MOUSE_WHEEL) {
          if (current_screen) {
            auto ctx = build_ctx(SDL_GetTicks(), nullptr);
            current_screen->OnEvent(e, ctx);
          }
          continue;
        }
      }

      // Pump TWS, drain the global side channels (status, headlines
      // are screen-private but status is global for the function bar).
      std::vector<std::string> frame_statuses;
      if (client.IsConnected()) {
        client.Pump();
        frame_statuses = client.DrainStatus();
        if (!frame_statuses.empty()) {
          bar.status = frame_statuses.back();
          bar.last_change_ms = SDL_GetTicks();
        }
      }

      if (current_screen) {
        auto ctx = build_ctx(SDL_GetTicks(), &frame_statuses);
        current_screen->OnFrame(ctx);
      }

      // Render: clear, draw bar, draw screen, present.
      SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g,
                             kBackground.b, kBackground.a);
      SDL_RenderClear(renderer);
      dlterm::DrawFunctionBar(backend, bar, backend.Metrics(), kBarOutline,
                              SDL_GetTicks());
      if (current_screen) {
        auto ctx = build_ctx(SDL_GetTicks(), &frame_statuses);
        current_screen->Render(ctx);
      }
      SDL_RenderPresent(renderer);
      SDL_Delay(16);
    }

    {
      auto ctx = build_ctx(SDL_GetTicks(), nullptr);
      if (current_screen) current_screen->OnExit(ctx);
    }
    client.Disconnect();
    SDL_StopTextInput(window);
  }

  TTF_CloseFont(font);
  if (bold_font) TTF_CloseFont(bold_font);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
  DLTERM_LOG_INFO("main", "dlterm exiting");
  dlterm::log::Shutdown();
  return 0;
}
