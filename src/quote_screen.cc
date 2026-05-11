#include "quote_screen.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <vector>

namespace dlterm {

namespace {

const char* KindLabel(ChartInstrument k) {
  switch (k) {
    case ChartInstrument::kContinuousFuture: return "CONT FUT";
    case ChartInstrument::kFutureLocal:      return "FUTURE";
    case ChartInstrument::kStock:
    default:                                  return "STOCK";
  }
}

std::string FormatPrice(double v) {
  if (!std::isfinite(v)) return "--";
  return std::format("{:.2f}", v);
}

// Comma-grouped integer for sizes / volume. Plain string ops — no
// locale dependency. Negative input (sentinel) -> "--".
std::string FormatSize(long long v) {
  if (v < 0) return "--";
  std::string raw = std::to_string(v);
  std::string out;
  out.reserve(raw.size() + raw.size() / 3);
  const int n = static_cast<int>(raw.size());
  for (int i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out.push_back(',');
    out.push_back(raw[i]);
  }
  return out;
}

std::string FormatTimeHMS(std::time_t ts) {
  if (ts <= 0) return "--";
  std::tm tm{};
  if (!localtime_r(&ts, &tm)) return "--";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
                 tm.tm_sec);
  return buf;
}

}  // namespace

void QuoteScreen::OnEnter(ScreenContext& ctx) {
  if (ctx.client) ctx.client->RequestQuote(symbol_, kind_);
  Layout(QuoteSnapshot{.symbol = symbol_, .kind = kind_},
          BodyCols(ctx.metrics));
}

void QuoteScreen::OnExit(ScreenContext& ctx) {
  if (ctx.client) ctx.client->CancelQuote();
}

void QuoteScreen::OnFrame(ScreenContext& ctx) {
  QuoteSnapshot q = ctx.client ? ctx.client->SnapshotQuote() : QuoteSnapshot{};
  // Stamp the screen's own symbol/kind on the snapshot so the header
  // renders correctly even before the first tick arrives.
  if (q.symbol.empty()) {
    q.symbol = symbol_;
    q.kind = kind_;
  }
  Layout(q, BodyCols(ctx.metrics));
}

void QuoteScreen::Render(ScreenContext& ctx) { block_.Render(ctx); }

void QuoteScreen::Layout(const QuoteSnapshot& q, std::size_t /*cols*/) {
  std::vector<std::string> lines;
  lines.reserve(16);

  lines.push_back(std::format("{}  {}  USD", q.symbol, KindLabel(q.kind)));
  lines.push_back("");

  if (!q.ready && !std::isfinite(q.bid) && !std::isfinite(q.ask)) {
    lines.push_back("  WAITING FOR DATA...");
    block_.SetLines(std::move(lines));
    block_.SetBoldRows({0});
    return;
  }

  // LAST + change-vs-prev-close.
  std::string change_str;
  if (std::isfinite(q.last) && std::isfinite(q.close) && q.close != 0.0) {
    const double diff = q.last - q.close;
    const double pct = diff / q.close * 100.0;
    change_str = std::format("     {:+.2f} ({:+.2f}%)", diff, pct);
  }
  lines.push_back(std::format("LAST          {:>10}{}", FormatPrice(q.last),
                               change_str));

  lines.push_back(std::format("BID           {:>10} x {:>6}",
                               FormatPrice(q.bid), FormatSize(q.bid_size)));
  lines.push_back(std::format("ASK           {:>10} x {:>6}",
                               FormatPrice(q.ask), FormatSize(q.ask_size)));
  lines.push_back("");
  lines.push_back(std::format("OPEN          {:>10}", FormatPrice(q.open)));
  lines.push_back(std::format("HIGH          {:>10}", FormatPrice(q.high)));
  lines.push_back(std::format("LOW           {:>10}", FormatPrice(q.low)));
  lines.push_back(std::format("PREV CLOSE    {:>10}", FormatPrice(q.close)));
  lines.push_back(std::format("VOLUME        {:>10}", FormatSize(q.volume)));
  lines.push_back("");
  lines.push_back(
      std::format("LAST TICK     {:>10}", FormatTimeHMS(q.last_ts)));

  block_.SetLines(std::move(lines));
  block_.SetBoldRows({0});
}

}  // namespace dlterm
