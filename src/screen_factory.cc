#include "screen_factory.h"

#include "body_screen.h"
#include "chart_screen.h"
#include "debug_screen.h"
#include "news_screen.h"
#include "portfolio_screen.h"
#include "quote_screen.h"

namespace dlterm {

std::unique_ptr<Screen> MakeScreenFor(const CommandResult& result) {
  switch (result.kind) {
    case CommandKind::kStaticText:
      return std::make_unique<BodyScreen>(result.text);
    case CommandKind::kStartChart:
      return std::make_unique<ChartScreen>(result.symbol, result.instrument);
    case CommandKind::kStartPortfolio:
      return std::make_unique<PortfolioScreen>();
    case CommandKind::kStartNews:
      return std::make_unique<NewsScreen>(result.symbol);
    case CommandKind::kStartGeneralNews:
      return std::make_unique<NewsScreen>("");
    case CommandKind::kStartQuote:
      return std::make_unique<QuoteScreen>(result.symbol, result.instrument);
    case CommandKind::kStartDebug:
      return std::make_unique<DebugScreen>();
    default:
      return nullptr;
  }
}

}  // namespace dlterm
