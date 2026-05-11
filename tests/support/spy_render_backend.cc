#include "spy_render_backend.h"

#include <algorithm>

namespace dlterm::test {

SpyRenderBackend::SpyRenderBackend(int cols, int rows, int cell_w_px,
                                    int cell_h_px, float pixel_density)
    : pixel_density_(pixel_density) {
  metrics_.cols = cols;
  metrics_.rows = rows;
  metrics_.cell_w = cell_w_px;
  metrics_.cell_h = cell_h_px;
  grid_.assign(rows, std::string(static_cast<std::size_t>(cols), ' '));
}

int SpyRenderBackend::WindowWidthPx() const {
  return metrics_.cols * metrics_.cell_w;
}

int SpyRenderBackend::WindowHeightPx() const {
  return metrics_.rows * metrics_.cell_h;
}

void SpyRenderBackend::WriteCells(int row, std::string_view text) {
  if (row < 0 || row >= metrics_.rows) return;
  std::string& dst = grid_[row];
  const std::size_t cap = dst.size();
  for (std::size_t i = 0; i < text.size() && i < cap; ++i) {
    dst[i] = text[i];
  }
}

void SpyRenderBackend::DrawTextLine(int row, std::string_view text,
                                     TextStyle style, int y_offset_px) {
  Call c;
  c.kind = Call::Kind::kText;
  c.row = row;
  c.text = std::string{text};
  c.style = style;
  c.y_offset_px = y_offset_px;
  calls_.push_back(std::move(c));
  // For the bar row (row 0) the function bar shifts text by y_offset_px;
  // we still record into the same grid row.
  WriteCells(row, text);
}

void SpyRenderBackend::FillRect(const Rect& r, Color color) {
  Call c;
  c.kind = Call::Kind::kFill;
  c.rect = r;
  c.color = color;
  calls_.push_back(std::move(c));
}

void SpyRenderBackend::DrawRectOutline(const Rect& r, Color color) {
  Call c;
  c.kind = Call::Kind::kOutline;
  c.rect = r;
  c.color = color;
  calls_.push_back(std::move(c));
}

void SpyRenderBackend::DrawLineSegment(Point a, Point b, Color color) {
  Call c;
  c.kind = Call::Kind::kLine;
  c.a = a;
  c.b = b;
  c.color = color;
  calls_.push_back(std::move(c));
}

void SpyRenderBackend::FillTriangle(Point a, Point b, Point ctri,
                                     Color color) {
  Call c;
  c.kind = Call::Kind::kTriangle;
  c.a = a;
  c.b = b;
  c.c = ctri;
  c.color = color;
  calls_.push_back(std::move(c));
}

std::string SpyRenderBackend::GridSnapshot() const {
  std::string out;
  for (std::size_t i = 0; i < grid_.size(); ++i) {
    if (i > 0) out.push_back('\n');
    out.append(grid_[i]);
  }
  return out;
}

std::string SpyRenderBackend::Row(int row) const {
  if (row < 0 || row >= static_cast<int>(grid_.size())) return {};
  std::string out = grid_[row];
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::size_t SpyRenderBackend::FillRectCount() const {
  return static_cast<std::size_t>(std::count_if(
      calls_.begin(), calls_.end(),
      [](const Call& c) { return c.kind == Call::Kind::kFill; }));
}

std::size_t SpyRenderBackend::OutlineCount() const {
  return static_cast<std::size_t>(std::count_if(
      calls_.begin(), calls_.end(),
      [](const Call& c) { return c.kind == Call::Kind::kOutline; }));
}

std::size_t SpyRenderBackend::LineSegmentCount() const {
  return static_cast<std::size_t>(std::count_if(
      calls_.begin(), calls_.end(),
      [](const Call& c) { return c.kind == Call::Kind::kLine; }));
}

std::size_t SpyRenderBackend::TriangleCount() const {
  return static_cast<std::size_t>(std::count_if(
      calls_.begin(), calls_.end(),
      [](const Call& c) { return c.kind == Call::Kind::kTriangle; }));
}

std::size_t SpyRenderBackend::TextCount() const {
  return static_cast<std::size_t>(std::count_if(
      calls_.begin(), calls_.end(),
      [](const Call& c) { return c.kind == Call::Kind::kText; }));
}

void SpyRenderBackend::Clear() {
  calls_.clear();
  for (auto& row : grid_) row.assign(row.size(), ' ');
}

}  // namespace dlterm::test
