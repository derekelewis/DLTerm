#include "chart_screen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <format>
#include <string>

#include "render_backend.h"
#include "widgets.h"

namespace dlterm {

namespace {

constexpr int kPriceLabelCols = 8;
constexpr Color kUp{80, 200, 120, 255};
constexpr Color kDown{230, 80, 80, 255};
constexpr Color kAxis{120, 120, 120, 255};

float YForPrice(double price, double pmin, double pmax, const Rect& chart_rect) {
  if (pmax <= pmin) return chart_rect.y + chart_rect.h * 0.5f;
  const double frac = (price - pmin) / (pmax - pmin);
  return chart_rect.y + static_cast<float>((1.0 - frac) * chart_rect.h);
}

}  // namespace

void ChartScreen::OnEnter(ScreenContext& ctx) {
  if (ctx.client) ctx.client->RequestChart(symbol_, kind_);
  last_refresh_ms_ = ctx.now_ms;
}

void ChartScreen::OnExit(ScreenContext& ctx) {
  if (ctx.client) ctx.client->CancelChart();
}

void ChartScreen::OnFrame(ScreenContext& ctx) {
  if (!ctx.client || !ctx.client->IsConnected()) return;
  bars_ = ctx.client->SnapshotChart().bars;

  // CONTFUT can't ride a keepUpToDate stream — poll every 30s.
  if (kind_ == ChartInstrument::kContinuousFuture) {
    constexpr Uint64 kRefreshIntervalMs = 30'000;
    if (ctx.now_ms - last_refresh_ms_ >= kRefreshIntervalMs) {
      ctx.client->RefreshChart();
      last_refresh_ms_ = ctx.now_ms;
    }
  }
}

void ChartScreen::Render(ScreenContext& ctx) {
  if (!ctx.render) return;
  RenderBackend& render = *ctx.render;

  // Header line on the first body row.
  const char* kind_label =
      kind_ == ChartInstrument::kContinuousFuture ? "  CONTFUT" :
      kind_ == ChartInstrument::kFutureLocal      ? "  FUT" :
                                                     "";
  std::string header;
  if (bars_.empty()) {
    header = std::format("CHART  {}{}  1D 5min  (loading...)", symbol_,
                          kind_label);
  } else {
    const double last = bars_.back().close;
    const double first_open = bars_.front().open;
    const double change = last - first_open;
    header = std::format(
        "CHART  {}{}  1D 5min  Last {:.2f}  {:+.2f}  {} bars", symbol_,
        kind_label, last, change, bars_.size());
  }
  const std::size_t cols =
      ctx.metrics.cols > 0 ? static_cast<std::size_t>(ctx.metrics.cols) : 0;
  render.DrawTextLine(kBarReservedRows, TruncateToCols(header, cols),
                      RenderBackend::TextStyle::kAccent);
  if (bars_.empty()) return;

  // Chart rect: leave one row for header, kPriceLabelCols on the right
  // for price labels, one row at bottom for time labels.
  const float top =
      static_cast<float>((kBarReservedRows + 1) * ctx.metrics.cell_h);
  const float right_reserved =
      static_cast<float>(kPriceLabelCols * ctx.metrics.cell_w);
  const float bottom_reserved = static_cast<float>(ctx.metrics.cell_h);
  const Rect chart_rect{
      0.0f,
      top,
      static_cast<float>(ctx.win_w_px) - right_reserved,
      static_cast<float>(ctx.win_h_px) - top - bottom_reserved,
  };
  if (chart_rect.w <= 0 || chart_rect.h <= 0) return;

  double pmin = bars_.front().low;
  double pmax = bars_.front().high;
  for (const auto& b : bars_) {
    pmin = std::min(pmin, b.low);
    pmax = std::max(pmax, b.high);
  }
  const double pad = (pmax - pmin) * 0.02;
  pmin -= pad;
  pmax += pad;
  if (pmax - pmin < 1e-9) pmax = pmin + 1.0;

  const int n = static_cast<int>(bars_.size());
  const float slot_w = chart_rect.w / static_cast<float>(n);
  const float body_w = std::max(1.0f, slot_w - 2.0f);

  for (int i = 0; i < n; ++i) {
    const auto& b = bars_[i];
    const float cx = chart_rect.x + (i + 0.5f) * slot_w;
    const float y_high = YForPrice(b.high, pmin, pmax, chart_rect);
    const float y_low = YForPrice(b.low, pmin, pmax, chart_rect);
    const float y_open = YForPrice(b.open, pmin, pmax, chart_rect);
    const float y_close = YForPrice(b.close, pmin, pmax, chart_rect);
    const Color c = (b.close >= b.open) ? kUp : kDown;
    render.DrawLineSegment({cx, y_high}, {cx, y_low}, c);
    const Rect body{
        cx - body_w * 0.5f,
        std::min(y_open, y_close),
        body_w,
        std::max(1.0f, std::abs(y_close - y_open)),
    };
    render.FillRect(body, c);
  }

  const int label_col_start =
      static_cast<int>(ctx.metrics.cols) - kPriceLabelCols;
  for (int i = 0; i < 5; ++i) {
    const float frac = static_cast<float>(i) / 4.0f;
    const double price = pmax - frac * (pmax - pmin);
    const int row_in_chart = static_cast<int>(
        std::round(frac * (chart_rect.h / ctx.metrics.cell_h)));
    const int row = kBarReservedRows + 1 + row_in_chart;
    if (row >= static_cast<int>(ctx.metrics.rows)) continue;
    std::string padded(label_col_start, ' ');
    padded.append(std::format("{:>{}.2f}", price, kPriceLabelCols));
    render.DrawTextLine(row, padded, RenderBackend::TextStyle::kAccent);
  }

  std::string time_row(static_cast<std::size_t>(ctx.metrics.cols), ' ');
  std::time_t last_label_hour = 0;
  for (int i = 0; i < n; ++i) {
    const std::time_t ts = bars_[i].ts;
    std::tm tm{};
    localtime_r(&ts, &tm);
    const std::time_t hour_bucket = ts - (ts % 3600);
    if (hour_bucket == last_label_hour) continue;
    last_label_hour = hour_bucket;
    const float cx = chart_rect.x + (i + 0.5f) * slot_w;
    const int col = static_cast<int>(cx / ctx.metrics.cell_w);
    if (col < 0 || col + 5 > label_col_start) continue;
    const std::string label =
        std::format("{:02d}:{:02d}", tm.tm_hour, tm.tm_min);
    for (std::size_t k = 0;
         k < label.size() && col + k < time_row.size(); ++k) {
      time_row[col + k] = label[k];
    }
  }
  render.DrawTextLine(static_cast<int>(ctx.metrics.rows) - 1, time_row,
                      RenderBackend::TextStyle::kAccent);

  render.DrawLineSegment(
      {chart_rect.x + chart_rect.w, chart_rect.y},
      {chart_rect.x + chart_rect.w, chart_rect.y + chart_rect.h}, kAxis);
}

}  // namespace dlterm
