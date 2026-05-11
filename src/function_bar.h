#ifndef DLTERM_SRC_FUNCTION_BAR_H_
#define DLTERM_SRC_FUNCTION_BAR_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "ibkr_client.h"
#include "render_backend.h"
#include "text_grid.h"

namespace dlterm {

struct FunctionBar {
  std::string input;
  std::size_t cursor = 0;
  std::string status;
  Uint64 last_change_ms = 0;
};

inline constexpr int kBarReservedRows = 2;

std::size_t BarMaxInputCols(int total_cols);

struct TerminalInfo {
  int win_w_pt = 0;
  int win_h_pt = 0;
  int win_w_px = 0;
  int win_h_px = 0;
  float pixel_density = 1.0f;
  int cols = 0;
  int rows = 0;
  int cell_w = 0;
  int cell_h = 0;
  int font_pt = 0;
  std::string font_path;
  std::string renderer_name;
  std::size_t body_lines = 0;
  std::size_t body_words = 0;
};

enum class CommandKind {
  kNone,
  kStaticText,
  kStartNews,
  kStartGeneralNews,
  kStartPortfolio,
  kStartChart,
  kStartQuote,
  kStartDebug,
  kQuit,
};

struct CommandResult {
  CommandKind kind = CommandKind::kNone;
  std::string text;
  std::string symbol;
  std::string status;
  ChartInstrument instrument = ChartInstrument::kStock;
};

CommandResult RunCommand(std::string_view command, const TerminalInfo& info);

void DrawFunctionBar(RenderBackend& render, const FunctionBar& bar,
                     const GridMetrics& metrics, Color outline_color,
                     Uint64 now_ms);

void HandleTextInput(FunctionBar* bar, std::string_view utf8,
                     std::size_t max_input_cols);
void HandleBackspace(FunctionBar* bar);
void HandleDelete(FunctionBar* bar);
void HandleEscape(FunctionBar* bar);
void HandleCursorLeft(FunctionBar* bar);
void HandleCursorRight(FunctionBar* bar);
void HandleCursorHome(FunctionBar* bar);
void HandleCursorEnd(FunctionBar* bar);

}  // namespace dlterm

#endif  // DLTERM_SRC_FUNCTION_BAR_H_
