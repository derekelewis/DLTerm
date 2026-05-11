#ifndef DLTERM_SRC_CHART_SCREEN_H_
#define DLTERM_SRC_CHART_SCREEN_H_

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ibkr_client.h"
#include "screen.h"

namespace dlterm {

// 1-day, 5-min candlestick chart for a single ticker. Live updates
// via reqHistoricalData(keepUpToDate=true) for stocks; CONTFUT is
// historical-only (keepUpToDate=false) per TWS docs.
class ChartScreen : public Screen {
 public:
  explicit ChartScreen(std::string symbol,
                        ChartInstrument kind = ChartInstrument::kStock)
      : symbol_(std::move(symbol)), kind_(kind) {}

  void OnEnter(ScreenContext& ctx) override;
  void OnExit(ScreenContext& ctx) override;
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override;

 private:
  std::string symbol_;
  ChartInstrument kind_ = ChartInstrument::kStock;
  std::vector<ChartBar> bars_;
  // CONTFUT historical doesn't support keepUpToDate, so we re-issue
  // the request every ~30s while the screen is active.
  Uint64 last_refresh_ms_ = 0;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_CHART_SCREEN_H_
