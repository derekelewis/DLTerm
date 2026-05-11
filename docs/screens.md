# Screens

Each command in dlterm maps to a `Screen` subclass that owns its own
state, drains its own data from `IbkrClient`, and renders the body
area. The function bar and the main event loop are not screen
concerns.

## The Screen interface

Defined in `src/screen.h`.

```cpp
class Screen {
 public:
  virtual void OnEnter(ScreenContext&) {}
  virtual void OnExit(ScreenContext&) {}
  virtual void OnFrame(ScreenContext&) {}
  virtual void Render(ScreenContext&) = 0;
  virtual bool OnEvent(const SDL_Event&, ScreenContext&) { return false; }
  virtual std::size_t BodyLineCount() const { return 0; }
  virtual std::size_t BodyWordCount() const { return 0; }
};
```

### When each method is called

| Method | When | Typical work |
|---|---|---|
| `OnEnter` | Right after the screen becomes current. | Start TWS subscriptions, lay out static content. |
| `OnExit` | Right before the screen is destroyed (or replaced). | Cancel TWS subscriptions, free large state. |
| `OnFrame` | Once per render frame, after `client.Pump()`. | Drain `IbkrClient` (`DrainHeadlines`, `Snapshot*`, `DrainArticle`); update internal state. |
| `OnEvent` | For each SDL event the main loop didn't claim first. | Mouse hover/click, scroll, screen-private keys. Return `true` to consume. |
| `Render` | Once per render frame, after `OnFrame`. | Draw the body using widgets + `DrawLine`. |
| `BodyLineCount`/`BodyWordCount` | When user types `INFO` (it grabs these stats from the outgoing screen). | Return the displayed line/word count if meaningful, else 0. |

### Key contracts

- `OnEvent` returning `true` **consumes the event** — the main loop
  will not handle it. Use this for ESC inside a modal sub-state
  (e.g. NewsScreen reader → feed). Default: `false`.
- `Render` is called every frame regardless of whether anything
  changed. Most screens cache pre-built body lines in `OnFrame` and
  just `block_.Render(ctx)` here. Cheap.
- `OnEnter` runs synchronously. Don't block; start async subscriptions
  and let `OnFrame` collect results.

## ScreenContext fields

See `src/screen.h`. The fields you'll use most:

- `ctx.render` — the `RenderBackend*` for all drawing. Use
  `ctx.render->DrawTextLine(...)`, `FillRect`, `DrawLineSegment`,
  etc. Production wraps SDL; tests inject a `SpyRenderBackend`.
- `ctx.metrics` — `GridMetrics` (rows, cols, cell_w, cell_h).
- `ctx.pixel_density` — needed by `MouseYToBodyRow` to convert event
  coordinates from points to pixels.
- `ctx.client` — the `IIbkrClient*` facade. Tests inject
  `FakeIbkrClient`.
- `ctx.bar` — write `ctx.bar->status = "…"` to surface a message in
  the function bar.
- `ctx.frame_statuses` — a read-only peek at the status messages
  drained from TWS this frame. NewsScreen uses it to detect
  `"Historical loaded:"` / `"Streaming BroadTape:"`.

## Adding a new screen

End-to-end checklist (this is what the screen refactor was designed
to make easy):

1. **Write the screen.** Create `src/quote_screen.{h,cc}` (or whatever
   you're building). Inherit from `Screen`. Implement `OnEnter`,
   `OnExit`, `OnFrame`, `Render` (and `OnEvent` if needed).

2. **Pick a command verb.** Edit `src/function_bar.h` and add a
   `kStartQuote` (or whatever) to `enum class CommandKind`.

3. **Parse it.** Edit `src/function_bar.cc::RunCommand` and add a
   branch that returns `CommandResult{kind=kStartQuote, symbol=...}`
   for your command.

4. **Wire the factory.** Edit `src/screen_factory.cc::MakeScreenFor`
   to map `kStartQuote → std::make_unique<QuoteScreen>(...)`.

5. **Register the source file.** Edit `CMakeLists.txt` and add
   `src/quote_screen.cc` to `dlterm_core`'s source list in
   `CMakeLists.txt` (screens are part of the core lib, not the
   `DLTerm` executable target).

6. **Update docs.** Add an entry to the "Catalog of existing screens"
   section below and the command reference in `docs/commands.md`.

That's it. No `main.cc` edits, no `ScreenMode` enum, no `Render()`
signature change. The factory is the single registration point.

## How a screen interacts with the function bar

The function bar is owned by `main.cc` and handles its own input
(typed characters, cursor keys, RETURN to dispatch). Screens never
read the function bar; they only see the result of `RunCommand`.

The one exception: a screen may **write** to `ctx.bar->status` for
short messages like "loading…" or "Not connected". This text shows in
the function bar instead of the prompt for ~3 seconds.

The function bar also reserves the top 2 rows of the grid
(`kBarReservedRows`). Body content starts at row 2.

## ESC handling

ESC is handled by `main.cc`. Default behavior: drop back to
`SplashScreen` (the home screen).

A screen may **consume** ESC for a sub-mode by returning `true` from
`OnEvent` for the ESC key. Currently only `NewsScreen` does this:
inside the reader sub-mode, ESC dismisses the reader and goes back to
the feed view (still the same screen).

## Sharing data: dispatch through IbkrClient

A screen never reaches into another screen. If two screens need the
same data, both go through `IbkrClient` (or each runs its own
subscription). Right now nothing requires this — every screen has
exclusive ownership of a single TWS feature.

## Catalog of existing screens

### `SplashScreen` (`src/splash_screen.{h,cc}`)

The home / opening screen. Installed at startup and after ESC from
any other screen. Auto-connects to TWS on `OnEnter` using the
host/port/client_id from `DLTERM_TWS_{HOST,PORT,CLIENT_ID}` (or
defaults); a connect failure surfaces on the function-bar status
line but the splash still renders.

Body content (re-laid out every frame so clocks tick):

- "DLTERM 0.1" banner + today's date.
- World clocks (NEW YORK / LONDON / TOKYO) with HH:MM:SS + zone
  abbreviation + MARKET OPEN/CLOSED. Open/close is a regular-hours
  approximation (no holiday calendar). Zones come from the POSIX
  tzdb via `TZ` + `localtime_r` (libc++ on macOS has no
  `std::chrono::locate_zone`).
- TWS block (only fully populated when connected):
  STATUS, SERVER VERSION (`client->serverVersion()`), CLIENT API
  (marketing version from `IBJts/API_VersionNum.txt` parsed at
  CMake configure time, plus `MIN_CLIENT_VER..MAX_CLIENT_VER`
  protocol range — e.g. `10.46.01 (proto 100..225)`),
  CONNECTED SINCE (`client->TwsConnectionTime()`), SERVER TIME
  (latest `reqCurrentTime` reply, refreshed every 5s), ACCOUNTS,
  NEWS PROVIDERS. Account codes are fully masked
  (`DU1234567` → `*********`) via `src/redact.cc`; disable with
  `DLTERM_REDACT_ACCOUNTS=0`.

Reads `client->SnapshotInfo()` each frame; the data is owned by
`InfoService` (see `docs/services.md`).

### `BodyScreen` (`src/body_screen.{h,cc}`)

Renders a static `std::string text` via `LayoutText` + `TextBlock`.
Used only for the `INFO` command's output (`kStaticText`,
constructed from `dlterm::FormatInfo(TerminalInfo)`).

No subscriptions, no `OnFrame` work. Re-layouts on window resize.

### `NewsScreen` (`src/news_screen.{h,cc}`)

The most complex screen. Hosts both:

- **Feed mode** (default): scrolling list of headlines. Per-ticker
  (constructor `NewsScreen("AAPL")`) or BroadTape (constructor
  `NewsScreen("")`).
- **Reader sub-mode**: full article body, entered by clicking a
  headline. Internal flag `reading_`. ESC inside reader returns to
  feed.

Owns:

- `std::deque<NewsHeadline> headlines_` — newest-first.
- `std::unordered_map<article_id, Uint64> flash_until_ms_` — 5-second
  bold-flash per new live headline.
- A `TextBlock` for the feed + `ScrollableTextBlock` for the reader.

Subscription: `client->SubscribeNews(symbol)` or
`client->SubscribeGeneralNews()`.

Behaviors handled: mouse hover (hover-rect via `DrawHoverRect`),
left-click → fetch article, reader keyboard scrolling
(Up/Down/PgUp/PgDn/Home/End/wheel via `ScrollableTextBlock`),
"Historical loaded:" detection to flip the `loaded_` flag.

### `PortfolioScreen` (`src/portfolio_screen.{h,cc}`)

Multi-account merged positions table. Triggered by `POS`.

Owns:

- A `Table` widget with 7 columns (SYMBOL / POS / AVG COST / MKT
  VALUE / DAILY P&L / UNR P&L / REAL P&L).
- A `TextBlock` for the final rendered output (header + table).

Each frame, calls `client->SnapshotPortfolio()`, formats numbers, and
rebuilds the table. Live updates flow from `reqPnLSingle` callbacks
through the service.

Subscription: `client->SubscribePortfolio()` (multi-account via
`reqAccountUpdatesMulti` + `reqPositionsMulti` + `reqPnL` +
`reqPnLSingle`).

### `ChartScreen` (`src/chart_screen.{h,cc}`)

Per-ticker candlestick chart. Triggered by `<SYMBOL> CHART` (stock)
or `<SYMBOL> FUT CHART` (continuous futures). The instrument kind
(`ChartInstrument::kStock` / `kContinuousFuture`) is stored on the
screen and forwarded to `client->RequestChart`.

Does **not** use a widget for the chart area — it draws candles
directly with `SDL_RenderLine`/`SDL_RenderFillRect`. The header line
uses `DrawLine`; axis labels use `DrawLine` with right-aligned
strings. When the instrument is `kContinuousFuture` the header
inserts a `CONTFUT` label after the symbol.

Owns just a `std::vector<ChartBar> bars_` (populated by
`client->SnapshotChart()` each frame).

Subscription: `client->RequestChart(symbol, kind)` (sends
`reqHistoricalData(... "1 D", "5 mins", ...)`).
`keepUpToDate=true` for stocks, `false` for continuous futures (TWS
doesn't support live tail on CONTFUT — see `docs/tws-api.md`).

### `QuoteScreen` (`src/quote_screen.{h,cc}`)

Live single-symbol quote. Triggered by a bare `<SYMBOL>` (stock) or
`<SYMBOL> FUT` (continuous future). The instrument kind is forwarded
to `client->RequestQuote` and steers the Contract construction (STK /
CONTFUT / FUT-by-local-symbol).

Body layout (left-label, right-aligned values):

- Header: `SYMBOL  STOCK|CONT FUT|FUTURE  USD` (bold)
- `LAST  <price>  <±diff> (±pct%)` (change vs prev close)
- `BID  <price> x <size>` / `ASK  <price> x <size>`
- `OPEN / HIGH / LOW / PREV CLOSE / VOLUME` (volume is comma-grouped)
- `LAST TICK  HH:MM:SS` (local time of last trade)

Owns a single `TextBlock`. Each frame, calls
`client->SnapshotQuote()`, formats, rebuilds the block. Missing
fields (NaN price / `-1` size) render as `--`.

Subscription: `client->RequestQuote(symbol, kind)` → `reqMktData`;
canceled in `OnExit` via `client->CancelQuote()`.

## Common patterns

### "Loading…" → "Loaded N items" header transitions

Two approaches in use:

1. **Internal flag flipped on a status message.** `NewsScreen` watches
   `ctx.frame_statuses` for prefixes like `"Historical loaded:"` and
   `"Streaming BroadTape:"`. Sets `loaded_ = true` and changes header
   text on next render.

2. **Snapshot's own `ready` field.** `PortfolioSnapshot::ready` is
   true when every managed account has fired both end-of-update
   markers. `PortfolioScreen` renders different header text per state.

### Pre-built body cache

To avoid rebuilding strings each frame, most screens build a
`TextBlock` in `OnFrame` and just call `block_.Render(ctx)` in
`Render`. See `BodyScreen`, `PortfolioScreen`. The exception is
`NewsScreen` which rebuilds every frame (so the bold-flash window can
expire mid-frame) and `ChartScreen` which does immediate-mode SDL
calls.

### Switching screens cancels the previous

When the user types a new command, `main.cc` calls
`current_screen->OnExit(ctx)` before constructing the next. Each
screen's `OnExit` is responsible for tearing down its subscriptions.
This is also how ESC works.

## Things you should NOT do in a screen

- **Don't touch SDL directly outside Render.** Resize event handling
  is fine but use `ctx.metrics` for layout queries.
- **Don't read `current_screen` (you are it).** Don't peek at the
  function-bar input directly — your screen is invoked via the
  `CommandResult`; that's the contract.
- **Don't take locks.** TWS thread safety is handled inside
  `IbkrClient` / `Dispatcher`. The screen runs on the main thread.
- **Don't carry state across `OnExit`/`OnEnter` calls of different
  screens.** Each screen is a fresh instance; transient state dies
  with it.
