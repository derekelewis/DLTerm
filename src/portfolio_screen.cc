#include "portfolio_screen.h"

#include <cmath>
#include <cstddef>
#include <format>
#include <string>

#include "ibkr_client.h"

namespace dlterm {

namespace {

double ParseDoubleSafe(const std::string& s) {
  if (s.empty()) return 0.0;
  try {
    return std::stod(s);
  } catch (...) {
    return 0.0;
  }
}

double SumTotalsAcross(const PortfolioSnapshot& snap, const std::string& key) {
  double total = 0.0;
  for (const auto& t : snap.totals) {
    auto it = t.values.find(key);
    if (it != t.values.end()) total += ParseDoubleSafe(it->second);
  }
  return total;
}

}  // namespace

void PortfolioScreen::OnEnter(ScreenContext& ctx) {
  if (ctx.client) ctx.client->SubscribePortfolio();
  table_.SetColumns({
      {"SYMBOL",    8,  Table::Align::kLeft},
      {"POS",       8,  Table::Align::kRight},
      {"AVG COST",  12, Table::Align::kRight},
      {"MKT VALUE", 13, Table::Align::kRight},
      {"DAILY P&L", 13, Table::Align::kRight},
      {"UNR P&L",   12, Table::Align::kRight},
      {"REAL P&L",  12, Table::Align::kRight},
  });
}

void PortfolioScreen::OnExit(ScreenContext& ctx) {
  if (ctx.client) ctx.client->UnsubscribePortfolio();
}

void PortfolioScreen::OnFrame(ScreenContext& ctx) {
  if (!ctx.client || !ctx.client->IsConnected()) return;
  const auto snap = ctx.client->SnapshotPortfolio();
  const std::size_t cols = BodyCols(ctx.metrics);

  // Header lines (above the table).
  std::vector<std::string> lines;
  std::string header;
  if (snap.accounts.empty()) {
    header = "PORTFOLIO  (waiting for managed accounts...)";
  } else if (!snap.ready) {
    header = std::format("PORTFOLIO  ({}/{} accounts loading...)",
                         snap.accounts.size(), snap.accounts.size());
  } else {
    const double net_liq = SumTotalsAcross(snap, "NetLiquidation");
    const double cash = SumTotalsAcross(snap, "TotalCashValue");
    const double daily = SumTotalsAcross(snap, "RT_DailyPnL");
    header = std::format(
        "PORTFOLIO  {} acct(s)  Net Liq {:>+.2f}  Cash {:>+.2f}  "
        "Daily P&L {:>+.2f}",
        snap.accounts.size(), net_liq, cash, daily);
  }
  lines.push_back(TruncateToCols(header, cols));
  lines.emplace_back();

  // Build the table from this frame's snapshot.
  table_.Clear();
  for (const auto& p : snap.positions) {
    std::string pos_s;
    if (std::abs(p.position - std::round(p.position)) < 1e-9) {
      pos_s = std::format("{}", static_cast<long long>(p.position));
    } else {
      pos_s = std::format("{:.4f}", p.position);
    }
    table_.AddRow({
        p.symbol,
        pos_s,
        std::format("{:.4f}", p.avg_cost),
        std::format("{:+.2f}", p.market_value),
        std::format("{:+.2f}", p.daily_pnl),
        std::format("{:+.2f}", p.unrealized_pnl),
        std::format("{:+.2f}", p.realized_pnl),
    });
  }
  for (auto& line : table_.Format(cols)) {
    lines.push_back(std::move(line));
  }
  block_.SetLines(std::move(lines));
}

}  // namespace dlterm
