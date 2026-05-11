#ifndef DLTERM_TESTS_SUPPORT_SPY_RENDER_BACKEND_H_
#define DLTERM_TESTS_SUPPORT_SPY_RENDER_BACKEND_H_

#include <string>
#include <string_view>
#include <vector>

#include "render_backend.h"
#include "text_grid.h"

namespace dlterm::test {

// In-memory recording backend for tests. Captures every draw call,
// and produces a simple ASCII snapshot of the rendered character grid
// for golden comparisons.
//
// Pixel-precise primitives (FillRect, lines, triangles) are recorded
// but do NOT affect the grid snapshot — assertions on those go via
// Calls() / FillRectCount() / etc.
class SpyRenderBackend : public RenderBackend {
 public:
  struct Call {
    enum class Kind { kText, kFill, kOutline, kLine, kTriangle };
    Kind kind = Kind::kText;
    int row = 0;
    std::string text;
    TextStyle style = TextStyle::kBody;
    int y_offset_px = 0;
    Rect rect;
    Point a, b, c;
    Color color;
  };

  // Default 80x24 cells, 10x18 px. cell sizes only matter for code
  // that derives px coords from cells.
  explicit SpyRenderBackend(int cols = 80, int rows = 24,
                             int cell_w_px = 10, int cell_h_px = 18,
                             float pixel_density = 1.0f);

  // Inspection / assertion helpers.
  std::string GridSnapshot() const;       // joined rows, '\n' between
  std::string Row(int row) const;         // single row, trailing spaces stripped
  const std::vector<Call>& Calls() const { return calls_; }
  std::size_t FillRectCount() const;
  std::size_t OutlineCount() const;
  std::size_t LineSegmentCount() const;
  std::size_t TriangleCount() const;
  std::size_t TextCount() const;
  void Clear();

  // RenderBackend overrides.
  GridMetrics Metrics() const override { return metrics_; }
  int WindowWidthPx() const override;
  int WindowHeightPx() const override;
  float PixelDensity() const override { return pixel_density_; }
  void DrawTextLine(int row, std::string_view text,
                    TextStyle style = TextStyle::kBody,
                    int y_offset_px = 0) override;
  void FillRect(const Rect& r, Color c) override;
  void DrawRectOutline(const Rect& r, Color c) override;
  void DrawLineSegment(Point a, Point b, Color c) override;
  void FillTriangle(Point a, Point b, Point ctri, Color color) override;

 private:
  void WriteCells(int row, std::string_view text);

  GridMetrics metrics_;
  float pixel_density_ = 1.0f;
  std::vector<std::string> grid_;  // grid_[row], length == cols
  std::vector<Call> calls_;
};

}  // namespace dlterm::test

#endif  // DLTERM_TESTS_SUPPORT_SPY_RENDER_BACKEND_H_
