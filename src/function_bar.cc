#include "function_bar.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "futures.h"
#include "render_backend.h"
#include "text_grid.h"

namespace dlterm {
namespace {

constexpr std::string_view kPrompt = "";
constexpr std::size_t kBarLeftPadCols = 2;
constexpr std::size_t kBarPrefixCols = kBarLeftPadCols + kPrompt.size();

constexpr float kBarHPadPx = 8.0f;
constexpr float kTriangleInsetPx = 4.0f;
constexpr float kTriangleWidthPx = 22.0f;
constexpr float kTriangleHeightPx = 40.0f;
constexpr float kCornerRadiusPx = 6.0f;
constexpr float kRectInnerPadPx = 4.0f;

void DrawRoundedRect(RenderBackend& r, const Rect& rect, float radius,
                     Color color) {
  const float x0 = rect.x;
  const float y0 = rect.y;
  const float x1 = rect.x + rect.w;
  const float y1 = rect.y + rect.h;
  r.DrawLineSegment({x0 + radius, y0}, {x1 - radius, y0}, color);
  r.DrawLineSegment({x0 + radius, y1}, {x1 - radius, y1}, color);
  r.DrawLineSegment({x0, y0 + radius}, {x0, y1 - radius}, color);
  r.DrawLineSegment({x1, y0 + radius}, {x1, y1 - radius}, color);
  r.DrawLineSegment({x0 + radius, y0}, {x0, y0 + radius}, color);
  r.DrawLineSegment({x1 - radius, y0}, {x1, y0 + radius}, color);
  r.DrawLineSegment({x0 + radius, y1}, {x0, y1 - radius}, color);
  r.DrawLineSegment({x1 - radius, y1}, {x1, y1 - radius}, color);
}

std::vector<std::string> Tokenize(std::string_view s) {
  std::vector<std::string> tokens;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    const std::size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i > start) {
      std::string tok;
      tok.reserve(i - start);
      for (std::size_t j = start; j < i; ++j) {
        tok.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(s[j]))));
      }
      tokens.push_back(std::move(tok));
    }
  }
  return tokens;
}

bool IsValidSymbol(const std::string& s) {
  if (s.empty() || s.size() > 8) return false;
  bool has_alpha = false;
  for (char c : s) {
    const auto uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || c == '.')) return false;
    if (std::isalpha(uc)) has_alpha = true;
  }
  return has_alpha;
}

std::string Basename(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) return std::string{path};
  return std::string{path.substr(slash + 1)};
}

std::string FormatInfo(const TerminalInfo& info) {
  std::string out;
  out.append(
      "DLTERM 0.1                                                       INFO\n"
      "\n");
  out.append(std::format("WINDOW (POINTS)        {} x {}\n", info.win_w_pt,
                         info.win_h_pt));
  out.append(std::format("WINDOW (PIXELS)        {} x {}\n", info.win_w_px,
                         info.win_h_px));
  out.append(
      std::format("PIXEL DENSITY          {:.2f}\n", info.pixel_density));
  out.append(std::format("GRID                   {} cols x {} rows\n",
                         info.cols, info.rows));
  out.append(std::format("CELL SIZE              {} x {} px\n", info.cell_w,
                         info.cell_h));
  out.append(std::format("FONT                   {} @ {}pt\n",
                         Basename(info.font_path), info.font_pt));
  out.append(std::format("RENDERER               {}\n", info.renderer_name));
  out.append(std::format("BODY                   {} lines, {} words\n",
                         info.body_lines, info.body_words));
  return out;
}

void Touch(FunctionBar* bar) { bar->last_change_ms = SDL_GetTicks(); }

}  // namespace

std::size_t BarMaxInputCols(int total_cols) {
  const int budget =
      total_cols - static_cast<int>(kBarPrefixCols) - 1;  // 1 for cursor
  return budget > 0 ? static_cast<std::size_t>(budget) : 0;
}

CommandResult RunCommand(std::string_view command, const TerminalInfo& info) {
  CommandResult r;
  const std::vector<std::string> tokens = Tokenize(command);
  if (tokens.empty()) return r;

  if (tokens.size() == 1) {
    const std::string& verb = tokens[0];
    if (verb == "INFO") {
      r.kind = CommandKind::kStaticText;
      r.text = FormatInfo(info);
      return r;
    }
    if (verb == "NEWS") {
      r.kind = CommandKind::kStartGeneralNews;
      return r;
    }
    if (verb == "POS") {
      r.kind = CommandKind::kStartPortfolio;
      return r;
    }
    if (verb == "DEBUG") {
      r.kind = CommandKind::kStartDebug;
      return r;
    }
    if (verb == "EXIT" || verb == "QUIT") {
      r.kind = CommandKind::kQuit;
      return r;
    }
    // Bare symbol opens the quote screen. Auto-detect futures local
    // symbols (e.g. CLM6) the same way `<SYM> CHART` does.
    if (IsValidSymbol(verb)) {
      if (IsFuturesLocalSymbol(verb)) {
        const std::string_view root = RootFromFuturesLocalSymbol(verb);
        if (ExchangeForContinuousFuture(root).empty()) {
          r.status = "Unknown futures root: " + std::string(root);
          return r;
        }
        r.kind = CommandKind::kStartQuote;
        r.symbol = verb;
        r.instrument = ChartInstrument::kFutureLocal;
        return r;
      }
      r.kind = CommandKind::kStartQuote;
      r.symbol = verb;
      r.instrument = ChartInstrument::kStock;
      return r;
    }
    r.status = "Unknown command: " + verb;
    return r;
  }

  if (tokens.size() == 2 && tokens[1] == "NEWS") {
    if (!IsValidSymbol(tokens[0])) {
      r.status = "Invalid symbol: " + tokens[0];
      return r;
    }
    r.kind = CommandKind::kStartNews;
    r.symbol = tokens[0];
    return r;
  }

  if (tokens.size() == 2 && tokens[1] == "CHART") {
    if (!IsValidSymbol(tokens[0])) {
      r.status = "Invalid symbol: " + tokens[0];
      return r;
    }
    // Auto-detect futures local symbols (alpha root + month code +
    // year digits, e.g. CLM6, ESM26). These get the live FUT path.
    if (IsFuturesLocalSymbol(tokens[0])) {
      const std::string_view root = RootFromFuturesLocalSymbol(tokens[0]);
      if (ExchangeForContinuousFuture(root).empty()) {
        r.status = "Unknown futures root: " + std::string(root);
        return r;
      }
      r.kind = CommandKind::kStartChart;
      r.symbol = tokens[0];
      r.instrument = ChartInstrument::kFutureLocal;
      return r;
    }
    r.kind = CommandKind::kStartChart;
    r.symbol = tokens[0];
    r.instrument = ChartInstrument::kStock;
    return r;
  }

  // <SYM> FUT -> continuous-futures quote (mirrors how <SYM> alone opens
  // a stock quote).
  if (tokens.size() == 2 && tokens[1] == "FUT") {
    if (!IsValidSymbol(tokens[0])) {
      r.status = "Invalid symbol: " + tokens[0];
      return r;
    }
    if (ExchangeForContinuousFuture(tokens[0]).empty()) {
      r.status = "Unknown futures symbol: " + tokens[0];
      return r;
    }
    r.kind = CommandKind::kStartQuote;
    r.symbol = tokens[0];
    r.instrument = ChartInstrument::kContinuousFuture;
    return r;
  }

  // <SYM> FUT CHART -> continuous-futures chart.
  if (tokens.size() == 3 && tokens[1] == "FUT" && tokens[2] == "CHART") {
    if (!IsValidSymbol(tokens[0])) {
      r.status = "Invalid symbol: " + tokens[0];
      return r;
    }
    if (ExchangeForContinuousFuture(tokens[0]).empty()) {
      r.status = "Unknown futures symbol: " + tokens[0];
      return r;
    }
    r.kind = CommandKind::kStartChart;
    r.symbol = tokens[0];
    r.instrument = ChartInstrument::kContinuousFuture;
    return r;
  }

  std::string joined;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0) joined.push_back(' ');
    joined.append(tokens[i]);
  }
  r.status = "Unknown command: " + joined;
  return r;
}

void DrawFunctionBar(RenderBackend& render, const FunctionBar& bar,
                     const GridMetrics& metrics, Color outline_color,
                     Uint64 now_ms) {
  const float bar_w = static_cast<float>(metrics.cols * metrics.cell_w);
  const float bar_h = static_cast<float>(kBarReservedRows * metrics.cell_h);
  const float rect_h =
      static_cast<float>(metrics.cell_h) + 2 * kRectInnerPadPx;
  const float rect_top = (bar_h - rect_h) / 2.0f;
  const float text_top = rect_top + kRectInnerPadPx;
  const int text_y_offset = static_cast<int>(text_top);

  const Rect main_rect{kBarHPadPx, rect_top, bar_w - 2 * kBarHPadPx, rect_h};
  DrawRoundedRect(render, main_rect, kCornerRadiusPx, outline_color);

  const float tri_x = kBarHPadPx + kTriangleInsetPx;
  const float tri_y = (bar_h - kTriangleHeightPx) / 2.0f;
  render.FillTriangle(
      {tri_x, tri_y},
      {tri_x, tri_y + kTriangleHeightPx},
      {tri_x + kTriangleWidthPx, tri_y + kTriangleHeightPx / 2.0f},
      outline_color);

  if (!bar.status.empty()) {
    std::string status_line;
    status_line.reserve(kBarLeftPadCols + bar.status.size());
    status_line.append(kBarLeftPadCols, ' ');
    status_line.append(bar.status);
    render.DrawTextLine(0, status_line, RenderBackend::TextStyle::kAccent,
                        text_y_offset);
    return;
  }

  std::string input_line;
  input_line.reserve(kBarPrefixCols + bar.input.size());
  input_line.append(kBarLeftPadCols, ' ');
  input_line.append(kPrompt);
  input_line.append(bar.input);
  render.DrawTextLine(0, input_line, RenderBackend::TextStyle::kAccent,
                      text_y_offset);

  const Uint64 since_change = now_ms - bar.last_change_ms;
  const bool visible = (since_change / 500) % 2 == 0;
  if (visible) {
    const int cursor_col =
        static_cast<int>(kBarPrefixCols) + static_cast<int>(bar.cursor);
    const Rect cursor_rect{
        static_cast<float>(cursor_col * metrics.cell_w),
        text_top,
        static_cast<float>(metrics.cell_w),
        static_cast<float>(metrics.cell_h),
    };
    render.FillRect(cursor_rect, outline_color);
  }
}

void HandleTextInput(FunctionBar* bar, std::string_view utf8,
                     std::size_t max_input_cols) {
  bar->status.clear();
  for (char raw : utf8) {
    const unsigned char uc = static_cast<unsigned char>(raw);
    if (uc < 0x20 || uc >= 0x7F) continue;
    if (bar->input.size() >= max_input_cols) break;
    const char up = static_cast<char>(std::toupper(uc));
    bar->input.insert(bar->cursor, 1, up);
    ++bar->cursor;
  }
  Touch(bar);
}

void HandleBackspace(FunctionBar* bar) {
  bar->status.clear();
  if (bar->cursor > 0) {
    bar->input.erase(bar->cursor - 1, 1);
    --bar->cursor;
  }
  Touch(bar);
}

void HandleDelete(FunctionBar* bar) {
  bar->status.clear();
  if (bar->cursor < bar->input.size()) {
    bar->input.erase(bar->cursor, 1);
  }
  Touch(bar);
}

void HandleEscape(FunctionBar* bar) {
  bar->input.clear();
  bar->cursor = 0;
  bar->status.clear();
  Touch(bar);
}

void HandleCursorLeft(FunctionBar* bar) {
  if (bar->cursor > 0) --bar->cursor;
  Touch(bar);
}

void HandleCursorRight(FunctionBar* bar) {
  if (bar->cursor < bar->input.size()) ++bar->cursor;
  Touch(bar);
}

void HandleCursorHome(FunctionBar* bar) {
  bar->cursor = 0;
  Touch(bar);
}

void HandleCursorEnd(FunctionBar* bar) {
  bar->cursor = bar->input.size();
  Touch(bar);
}

}  // namespace dlterm
