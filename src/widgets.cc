#include "widgets.h"

#include <algorithm>
#include <format>
#include <string>

#include "log.h"
#include "render_backend.h"

namespace dlterm {

// ---- Tier 1 ----

std::size_t BodyCols(const GridMetrics& m) {
  return m.cols > 0 ? static_cast<std::size_t>(m.cols) : 0;
}

std::string TruncateToCols(std::string_view s, std::size_t cols) {
  if (s.size() <= cols) return std::string{s};
  if (cols == 0) return {};
  std::string out{s.substr(0, cols)};
  out.back() = '~';
  return out;
}

int MouseYToBodyRow(float pixel_density, const GridMetrics& m, float event_y) {
  if (m.cell_h <= 0) return -1;
  const float density = pixel_density > 0 ? pixel_density : 1.0f;
  const int pixel_y = static_cast<int>(event_y * density);
  const int row = pixel_y / m.cell_h;
  return row - kBarReservedRows;
}

void DrawHoverRect(RenderBackend& render, const GridMetrics& m, int body_row,
                   Color color) {
  if (body_row < 0) return;
  const Rect r{
      0.0f,
      static_cast<float>((body_row + kBarReservedRows) * m.cell_h),
      static_cast<float>(m.cols * m.cell_w),
      static_cast<float>(m.cell_h),
  };
  render.DrawRectOutline(r, color);
}

// ---- TextBlock ----

void TextBlock::Render(ScreenContext& ctx) const {
  if (!ctx.render) return;
  const int max_body_rows = ctx.metrics.rows - kBarReservedRows;
  const int line_count = static_cast<int>(lines_.size());
  DLTERM_DCHECK(std::is_sorted(bold_rows_.begin(), bold_rows_.end()));
  for (int i = 0; i < max_body_rows && i < line_count; ++i) {
    const bool bold =
        std::binary_search(bold_rows_.begin(), bold_rows_.end(), i);
    ctx.render->DrawTextLine(
        i + kBarReservedRows, lines_[i],
        bold ? RenderBackend::TextStyle::kBodyBold
             : RenderBackend::TextStyle::kBody);
  }
}

// ---- ScrollableTextBlock ----

int ScrollableTextBlock::VisibleRows(const GridMetrics& m) const {
  const int hdr = static_cast<int>(header_.size());
  return std::max(1, m.rows - kBarReservedRows - hdr);
}

int ScrollableTextBlock::MaxScrollTop(const GridMetrics& m) const {
  const int n = static_cast<int>(body_.size());
  return std::max(0, n - VisibleRows(m));
}

bool ScrollableTextBlock::OnScrollEvent(const SDL_Event& e,
                                        const GridMetrics& m) {
  const int max_top = MaxScrollTop(m);
  if (e.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Keycode k = e.key.key;
    if (k == SDLK_UP) {
      if (scroll_top_ > 0) --scroll_top_;
      return true;
    }
    if (k == SDLK_DOWN) {
      if (scroll_top_ < max_top) ++scroll_top_;
      return true;
    }
    if (k == SDLK_PAGEUP) {
      scroll_top_ = std::max(0, scroll_top_ - VisibleRows(m));
      return true;
    }
    if (k == SDLK_PAGEDOWN) {
      scroll_top_ = std::min(max_top, scroll_top_ + VisibleRows(m));
      return true;
    }
    if (k == SDLK_HOME) {
      scroll_top_ = 0;
      return true;
    }
    if (k == SDLK_END) {
      scroll_top_ = max_top;
      return true;
    }
  }
  if (e.type == SDL_EVENT_MOUSE_WHEEL) {
    scroll_top_ = std::clamp(scroll_top_ - static_cast<int>(e.wheel.y), 0,
                              max_top);
    return true;
  }
  return false;
}

void ScrollableTextBlock::Render(ScreenContext& ctx) const {
  if (!ctx.render) return;
  const int max_body_rows = ctx.metrics.rows - kBarReservedRows;
  // Header pinned at top.
  int row = 0;
  for (const auto& h : header_) {
    if (row >= max_body_rows) break;
    ctx.render->DrawTextLine(row + kBarReservedRows, h);
    ++row;
  }
  // Scrolled body window beneath.
  const int visible = VisibleRows(ctx.metrics);
  const int n = static_cast<int>(body_.size());
  const int top = std::clamp(scroll_top_, 0, std::max(0, n));
  for (int i = 0; i < visible && top + i < n && row < max_body_rows; ++i) {
    ctx.render->DrawTextLine(row + kBarReservedRows, body_[top + i]);
    ++row;
  }
}

// ---- Table ----

std::vector<std::string> Table::Format(std::size_t display_cols) const {
  std::vector<std::string> out;
  if (cols_.empty()) return out;
  auto format_cell = [](const std::string& cell, const Column& col) {
    return col.align == Align::kLeft
               ? std::format("{:<{}}", cell, col.width)
               : std::format("{:>{}}", cell, col.width);
  };
  // Header row.
  std::string header;
  for (std::size_t i = 0; i < cols_.size(); ++i) {
    if (i > 0) header.push_back(' ');
    header.append(format_cell(cols_[i].header, cols_[i]));
  }
  out.push_back(TruncateToCols(header, display_cols));
  // Data rows.
  for (const auto& cells : rows_) {
    std::string row;
    for (std::size_t i = 0; i < cols_.size(); ++i) {
      if (i > 0) row.push_back(' ');
      const std::string& cell = i < cells.size() ? cells[i] : std::string{};
      row.append(format_cell(cell, cols_[i]));
    }
    out.push_back(TruncateToCols(row, display_cols));
  }
  return out;
}

}  // namespace dlterm
