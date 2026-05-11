#ifndef DLTERM_SRC_QUOTE_SCREEN_H_
#define DLTERM_SRC_QUOTE_SCREEN_H_

#include <cstddef>
#include <string>

#include "ibkr_client.h"
#include "screen.h"
#include "widgets.h"

namespace dlterm {

// Live single-symbol quote screen. Subscribes to reqMktData on enter,
// re-renders the snapshot every frame. Works for stocks (SMART/USD),
// continuous futures (CONTFUT), and futures by local symbol.
class QuoteScreen : public Screen {
 public:
  QuoteScreen(std::string symbol,
               ChartInstrument kind = ChartInstrument::kStock)
      : symbol_(std::move(symbol)), kind_(kind) {}

  void OnEnter(ScreenContext& ctx) override;
  void OnExit(ScreenContext& ctx) override;
  void OnFrame(ScreenContext& ctx) override;
  void Render(ScreenContext& ctx) override;

  std::size_t BodyLineCount() const override { return block_.LineCount(); }

 private:
  void Layout(const QuoteSnapshot& q, std::size_t cols);

  std::string symbol_;
  ChartInstrument kind_ = ChartInstrument::kStock;
  TextBlock block_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_QUOTE_SCREEN_H_
