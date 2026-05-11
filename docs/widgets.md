# Widgets

Stateless helpers and small stateful classes that screens compose.
All live in `src/widgets.{h,cc}`.

The widget layer was extracted in a refactor pass after the screen
interface stabilized. The principle: **only extract what's actually
duplicated.** New widgets should be added when a third screen needs
the same thing — not speculatively.

## Stateless helpers

### `std::size_t BodyCols(const GridMetrics& m)`

The number of columns available for body content. Clamps negative to
0, converts `int` to `size_t`. Used everywhere a screen needs to know
"how wide is my body area in characters?"

### `std::string TruncateToCols(std::string_view s, std::size_t cols)`

Returns at most `cols` characters from `s`. If truncation happened,
the last visible char becomes `~` to indicate the cut. Returns empty
string when `cols == 0`. Used by every screen that produces lines
that might overflow.

```cpp
TruncateToCols("Hello, world!", 8) → "Hello, ~"
TruncateToCols("Short", 100)       → "Short"
```

### `int MouseYToBodyRow(float pixel_density, const GridMetrics& m, float event_y)`

Converts an SDL motion/button event's y-coordinate to the 0-based
body row. **Handles the pixel-density gotcha**: SDL events arrive in
window points while the grid is in pixels. This helper multiplies by
`pixel_density` before dividing by `cell_h`.

Returns `-1` when the event y falls above the body area (function bar
rows). Always use this rather than rolling your own math.

```cpp
const int body_row = MouseYToBodyRow(ctx.pixel_density, ctx.metrics,
                                      e.motion.y);
if (body_row < 0) return;  // event was on the function bar
```

### `void DrawHoverRect(RenderBackend&, const GridMetrics&, int body_row, Color color = white)`

Draws a 1-pixel outline rectangle around the given body row. Used for
hover/selection highlights. Pass a `body_row` from `MouseYToBodyRow`.
Default color is white; pass a different `dlterm::Color` to override.

No-op if `body_row < 0`. Currently only `NewsScreen` uses it; will
likely become more general as more screens get clickable rows.

## Stateful widgets

### `class TextBlock`

```cpp
class TextBlock {
 public:
  void SetLines(std::vector<std::string> lines);
  void SetBoldRows(std::vector<int> body_rows);  // sorted ascending
  void Clear();
  std::size_t LineCount() const;
  const std::vector<std::string>& Lines() const;
  void Render(ScreenContext& ctx) const;
};
```

**A vector of strings rendered into the body grid, with optional bold
rows.** This is the workhorse — replaces the `for (i in body_rows)
DrawLine(...)` loop that every screen used to inline.

- `lines` are body-grid rows. Index `i` draws at grid row
  `kBarReservedRows + i`.
- `bold_rows` are body-row indices (0-based, same as `lines`'s index)
  that should render in `ctx.bold_body_cache` instead of
  `ctx.body_cache`. Must be sorted ascending — `Render` binary-searches.
- Lines past the available grid height are silently truncated.

Used by `BodyScreen`, `PortfolioScreen`, and `NewsScreen` (feed mode).

Typical usage in a screen:

```cpp
void Foo::OnFrame(ScreenContext& ctx) {
  std::vector<std::string> lines = BuildContent(ctx);
  block_.SetLines(std::move(lines));
}
void Foo::Render(ScreenContext& ctx) { block_.Render(ctx); }
```

### `class ScrollableTextBlock`

```cpp
class ScrollableTextBlock {
 public:
  void SetHeader(std::vector<std::string> lines);   // pinned at top
  void SetBody(std::vector<std::string> lines);     // scrollable
  void ResetScroll();
  bool OnScrollEvent(const SDL_Event&, const GridMetrics&);
  void Render(ScreenContext& ctx) const;
  std::size_t HeaderLineCount() const;
  std::size_t BodyLineCount() const;
  int ScrollTop() const;
};
```

**Pinned header + scrollable body.** Used for the news article
reader. Suitable for any "long-form content with a small fixed
header" screen.

- Header lines render at the top and never scroll.
- Body lines render below, windowed by `scroll_top_`.
- `OnScrollEvent` handles Up/Down (one line), PageUp/PageDown (one
  page), Home/End (extremes), and `SDL_EVENT_MOUSE_WHEEL`. Returns
  `true` if it handled the event.

Typical usage:

```cpp
// On entering article view:
reader_block_.SetHeader({title, subtitle});
reader_block_.SetBody(LayoutText(article_text, BodyCols(ctx.metrics)));

// In OnEvent:
bool MyScreen::OnEvent(const SDL_Event& e, ScreenContext& ctx) {
  return reader_block_.OnScrollEvent(e, ctx.metrics);
}

// In Render:
reader_block_.Render(ctx);
```

### `class Table`

```cpp
class Table {
 public:
  enum class Align { kLeft, kRight };
  struct Column {
    std::string header;
    int width;          // chars
    Align align = Align::kLeft;
  };
  void SetColumns(std::vector<Column> cols);
  void Clear();
  void AddRow(std::vector<std::string> cells);
  std::vector<std::string> Format(std::size_t display_cols) const;
};
```

**Column-aligned table. Caller pre-formats numeric cells; the table
handles padding and truncation.**

- Set columns once (typically in `OnEnter`).
- Each frame, `Clear()` and re-add rows.
- `Format(cols)` returns a vector of strings (header row + data rows),
  ready to feed into a `TextBlock`.
- 1-space gap between columns. Lines exceeding `cols` get truncated
  with `~`.

The numeric formatting (`std::format("{:+.2f}", x)` etc.) stays in
the caller. The table only cares about alignment and width.

Used by `PortfolioScreen`. Will be the right tool whenever you need
columns.

```cpp
table_.SetColumns({
  {"SYMBOL", 8,  Table::Align::kLeft},
  {"POS",    8,  Table::Align::kRight},
  {"PRICE",  12, Table::Align::kRight},
});
table_.AddRow({"AAPL", "100", std::format("{:.2f}", 192.45)});
auto lines = table_.Format(BodyCols(ctx.metrics));
text_block_.SetLines(std::move(lines));
```

## What's deliberately NOT a widget

These were considered and rejected because only one screen uses each:

- **Candlestick / sparkline plot**: only `ChartScreen`. Lives there.
- **HTML strip**: only the news article reader. Lives in
  `news_screen.cc`.
- **News-headline metadata strip** (`{A:…:K:…}`): same. Lives in
  `news_screen.cc::CleanHeadline`.

If you find yourself wanting one of these in a new screen, extract it
to `widgets.{h,cc}` then.

## Adding a new widget

1. Add the type to `src/widgets.h`. Keep the API small and screen-
   agnostic.
2. Implement in `src/widgets.cc`. Use only `ScreenContext`'s rendering
   bits (renderer, caches, metrics, window) — no client or bar.
3. Update this doc — add an entry to the widget catalog above and any
   relevant principles.
4. Replace the duplicated logic in 2+ screens with the new widget.
   (If you can't find 2 screens that want it, it's premature.)

## Common pitfalls

- **Don't make widgets aware of `IbkrClient`.** Widgets render and
  capture input; they don't talk to TWS.
- **Don't let widgets cache `ScreenContext`.** It's per-frame.
  Receive it as a function argument.
- **Don't put screen-specific state in a widget.** If `TextBlock`
  ends up with feed-vs-reader-vs-portfolio modes, you've gone too
  far. Split it.
