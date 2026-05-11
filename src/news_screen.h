#ifndef DLTERM_SRC_NEWS_SCREEN_H_
#define DLTERM_SRC_NEWS_SCREEN_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>

#include "ibkr_client.h"
#include "screen.h"
#include "widgets.h"

namespace dlterm {

// Per-ticker NEWS feed plus broadtape (no-symbol) variant. Internally
// hosts a modal "reader" sub-mode for viewing the full article body.
class NewsScreen : public Screen {
 public:
  // Empty `symbol` means BroadTape (general news, MARKET label).
  explicit NewsScreen(std::string symbol);

  void OnEnter(ScreenContext& ctx) override;
  void OnExit(ScreenContext& ctx) override;
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override;
  bool OnEvent(const SDL_Event& e, ScreenContext& ctx) override;

 private:
  void RebuildFeedLines(std::size_t cols, Uint64 now_ms);
  bool OnFeedEvent(const SDL_Event& e, ScreenContext& ctx);
  bool OnReaderEvent(const SDL_Event& e, ScreenContext& ctx);
  void InsertSorted(NewsHeadline h);

  // Feed state.
  std::string symbol_;          // empty + display "MARKET" = BroadTape
  std::string label_;
  bool loaded_ = false;
  std::deque<NewsHeadline> headlines_;
  std::unordered_map<std::string, Uint64> flash_until_ms_;
  int hovered_idx_ = -1;
  TextBlock feed_block_;

  // Reader sub-mode.
  bool reading_ = false;
  int reader_req_id_ = 0;
  bool reader_received_ = false;
  std::string reader_title_;
  std::string reader_subtitle_;
  std::string reader_body_text_;
  ScrollableTextBlock reader_block_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_NEWS_SCREEN_H_
