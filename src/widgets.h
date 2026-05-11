#ifndef DLTERM_SRC_WIDGETS_H_
#define DLTERM_SRC_WIDGETS_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "render_backend.h"
#include "screen.h"
#include "text_grid.h"

namespace dlterm {

// ---- Tier 1: stateless helpers ----

// Number of grid columns available for the body (just `metrics.cols`,
// clamped >= 0 and converted to size_t).
std::size_t BodyCols(const GridMetrics& m);

// Trims `s` to at most `cols` chars; if it had to trim, the last
// visible char becomes `~` to indicate truncation.
std::string TruncateToCols(std::string_view s, std::size_t cols);

// Converts a window-coord mouse-y (e.motion.y / e.button.y) to the
// body-grid row number (0 = first body row). Returns -1 if the y
// coordinate falls above the body area (function bar) or if metrics
// are degenerate.
int MouseYToBodyRow(float pixel_density, const GridMetrics& m, float event_y);

// Draws a 1-px outline around the given body row. Used for hover /
// selection. body_row is 0-based within the body area.
void DrawHoverRect(RenderBackend& render, const GridMetrics& m, int body_row,
                   Color color = {255, 255, 255, 255});

// ---- Tier 2: stateful widgets ----

// A vector of lines plus optional bold-row indices, rendered into the
// body grid. Body rows are 0-based within the body area (0 = first row
// after the function bar). When a row index appears in `bold_rows`,
// that row is rendered with TextStyle::kBodyBold.
class TextBlock {
 public:
  void SetLines(std::vector<std::string> lines) { lines_ = std::move(lines); }
  // body_rows must be sorted ascending (we binary-search).
  void SetBoldRows(std::vector<int> body_rows) {
    bold_rows_ = std::move(body_rows);
  }
  void Clear() {
    lines_.clear();
    bold_rows_.clear();
  }
  std::size_t LineCount() const { return lines_.size(); }
  const std::vector<std::string>& Lines() const { return lines_; }
  void Render(ScreenContext& ctx) const;

 private:
  std::vector<std::string> lines_;
  std::vector<int> bold_rows_;
};

// Pinned header (drawn at top, never scrolls) plus a scrollable body
// window. Suitable for news article reader, log views, long file
// dumps, etc. The widget owns scroll state and exposes scroll-handler
// behavior via OnScrollEvent.
class ScrollableTextBlock {
 public:
  void SetHeader(std::vector<std::string> lines) {
    header_ = std::move(lines);
  }
  void SetBody(std::vector<std::string> lines) {
    body_ = std::move(lines);
    scroll_top_ = 0;
  }
  void ResetScroll() { scroll_top_ = 0; }
  std::size_t HeaderLineCount() const { return header_.size(); }
  std::size_t BodyLineCount() const { return body_.size(); }
  int ScrollTop() const { return scroll_top_; }

  // Returns true if the event was a scroll/navigation key/wheel event
  // and thus consumed.
  bool OnScrollEvent(const SDL_Event& e, const GridMetrics& m);

  void Render(ScreenContext& ctx) const;

 private:
  int VisibleRows(const GridMetrics& m) const;
  int MaxScrollTop(const GridMetrics& m) const;

  std::vector<std::string> header_;
  std::vector<std::string> body_;
  int scroll_top_ = 0;
};

// A column-aligned table. Caller pre-formats numeric cells (printf,
// std::format, whatever); the table only handles padding/alignment.
// Format() produces a vector<string> ready to feed into TextBlock.
class Table {
 public:
  enum class Align { kLeft, kRight };
  struct Column {
    std::string header;
    int width;          // chars
    Align align = Align::kLeft;
  };

  void SetColumns(std::vector<Column> cols) { cols_ = std::move(cols); }
  void Clear() { rows_.clear(); }
  void AddRow(std::vector<std::string> cells) {
    rows_.push_back(std::move(cells));
  }
  // 1 space gap between columns. Each line truncated to `display_cols`.
  std::vector<std::string> Format(std::size_t display_cols) const;

 private:
  std::vector<Column> cols_;
  std::vector<std::vector<std::string>> rows_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_WIDGETS_H_
