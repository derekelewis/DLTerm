# Architecture

This document describes the layering between components, the request
flows that exercise them, and the threading model. Read this first
before working in any specific layer.

## The four layers

```
   ┌─────────────────────────────────────────┐
   │  Layer 1: main.cc                       │
   │  SDL loop, function bar, current_screen │
   └────────────────┬────────────────────────┘
                    │ owns + drives
   ┌────────────────▼────────────────────────┐
   │  Layer 2: Screens                       │
   │  BodyScreen, NewsScreen, PortfolioScreen,│
   │  ChartScreen, DebugScreen               │
   └──────┬─────────────────────────┬────────┘
          │ compose                  │ talk to
   ┌──────▼───────────────┐  ┌──────▼────────┐
   │  Layer 3a: Widgets   │  │  Layer 3b:    │
   │  TextBlock, Table,   │  │  IIbkrClient  │
   │  ScrollableTextBlock │  │  facade       │
   │  (call RenderBackend)│  │               │
   └──────────┬───────────┘  └──────┬────────┘
              │                     │ delegates per-feature
   ┌──────────▼─────────┐   ┌──────▼─────────┐
   │  RenderBackend     │   │  Layer 4:      │
   │  (SDL or Spy impl) │   │  Services      │
   └────────────────────┘   │  News / Portfolio
                            │  / Chart        │
                            └───────┬────────┘
                                    │ EWrapper / EClient
                            ┌───────▼────────┐
                            │  TWS API       │
                            │  (prebuilt     │
                            │   dylib)       │
                            └────────────────┘
```

### Library split

The CMake build is split so tests and CI can build the pure logic
without TWS:

| Target | Sources | Deps |
|---|---|---|
| `dlterm_core` (STATIC) | log, layout, text_grid, sdl_render_backend, diagnostics, function_bar, widgets, all screens, screen_factory | SDL3, SDL3_ttf |
| `dlterm_tws` (STATIC) | ibkr_client, services/* | TWS API, protobuf, ZLIB |
| `dlterm` (EXE) | main.cc | dlterm_core, dlterm_tws |
| `dlterm_tests` (EXE) | tests/**/*.cc | dlterm_core (+ dlterm_tws if present), GoogleTest |

CI builds with `-DDLTERM_BUILD_APP=OFF` to skip the TWS layer +
executable; tests still link the SDL backend (compiles cleanly without
a window).

**The arrows go only downward.** A widget never calls a screen. A
service never calls a widget. A screen never calls a service directly
— it goes through the `IbkrClient` facade.

## Files at each layer

| Layer | Files | Lines |
|---|---|---|
| 1. Main loop | `src/main.cc` | 384 |
| 2. Screens | `src/{body,news,portfolio,chart}_screen.{h,cc}`, `src/screen.h`, `src/screen_factory.{h,cc}` | ~750 |
| 3a. Widgets | `src/widgets.{h,cc}` | ~285 |
| 3b. IbkrClient + Dispatcher | `src/ibkr_client.{h,cc}` | ~470 |
| 4. Services | `src/services/{news,portfolio,chart}_service.{h,cc}`, `src/services/service_context.h` | ~870 |
| Misc | `src/function_bar.{h,cc}`, `src/text_grid.{h,cc}`, `src/layout.{h,cc}` | ~700 |

## Screen lifecycle

The fundamental abstraction. Defined in `src/screen.h`.

```cpp
class Screen {
 public:
  virtual void OnEnter(ScreenContext&) {}
  virtual void OnExit(ScreenContext&) {}
  virtual void OnFrame(ScreenContext&) {}   // drain client, update state
  virtual void Render(ScreenContext&) = 0;
  virtual bool OnEvent(const SDL_Event&, ScreenContext&) { return false; }

  virtual std::size_t BodyLineCount() const { return 0; }
  virtual std::size_t BodyWordCount() const { return 0; }
};
```

The main loop guarantees this order per frame:

```
loop:
  for each SDL event:
    main loop tries function-bar/global keys first
    if not consumed:
      current_screen.OnEvent(event)
  current_screen.OnFrame(context)   // pull live data, update state
  current_screen.Render(context)    // draw
```

On a screen swap (user types a command, or ESC):

```
old_screen.OnExit(context)
current_screen = MakeScreenFor(command_result)
new_screen.OnEnter(context)
```

## ScreenContext

`src/screen.h`. Mutable bag of references the main loop builds each
frame. Screens read what they need.

```cpp
struct ScreenContext {
  RenderBackend* render;        // abstracted drawing — no SDL leak
  GridMetrics metrics;
  int win_w_px, win_h_px;
  float pixel_density;          // for converting mouse points → pixels
  Uint64 now_ms;
  IIbkrClient* client;          // interface, not concrete type
  FunctionBar* bar;             // screens may set bar->status
  const std::vector<std::string>* frame_statuses;  // drained from TWS
                                                    // this frame
};
```

The `RenderBackend` interface (`src/render_backend.h`) wraps drawing
primitives (`DrawTextLine`, `FillRect`, `DrawLineSegment`, …).
Production uses `SdlRenderBackend` (wraps SDL3 + glyph caches);
tests use `SpyRenderBackend` (records every call). The `IIbkrClient`
interface (`src/ibkr_client.h`) is implemented by both the live
`IbkrClient` and `tests/support/fake_ibkr_client.h`.

Screens may write to `bar->status` for short-lived messages.
`frame_statuses` is provided as a peek (read-only); the function bar
status is already set to the last one. NewsScreen uses it to detect
`"Historical loaded:"` / `"Streaming BroadTape:"` for the loaded flag.

## Request flows

### Flow 1: user types `AAPL NEWS`

```
Main loop sees ENTER key
  → RunCommand("AAPL NEWS")              [function_bar.cc]
    → CommandResult{kind=kStartNews, symbol="AAPL"}
  → IbkrClient::Connect(host, port, id)  if not connected
  → MakeScreenFor(result)                [screen_factory.cc]
    → unique_ptr<NewsScreen>("AAPL")
  → old_screen->OnExit(ctx)
  → current_screen = new screen
  → new_screen->OnEnter(ctx)             [news_screen.cc]
    → client->SubscribeNews("AAPL")
      → Dispatcher::StartTickerNews      [ibkr_client.cc]
        (lock + delegate)
      → NewsService::StartTickerSession  [services/news_service.cc]
        → reqContractDetails(reqId, AAPL/STK/SMART/USD)

(EReader thread, ~50ms later):
TWS sends contractDetails(reqId, ...)
  → Dispatcher::contractDetails(...)     [under lock]
    → NewsService::OnContractDetails     [captures conId]

TWS sends contractDetailsEnd(reqId)
  → Dispatcher::contractDetailsEnd
    → NewsService::OnContractDetailsEnd
      → fires reqHistoricalNews(conId, provider_codes, 50)

TWS sends historicalNews(...)  N times
  → each adds a NewsHeadline to headlines_

TWS sends historicalNewsEnd(reqId)
  → NewsService::OnHistoricalNewsEnd
    → emits status "Historical loaded: N headlines, streaming live"
    → fires reqMktData(streamId, AAPL_STK, "mdoff,292:PROVIDERS")

(Per-frame in main loop):
client.Pump()                            [drives reader thread queue]
client.DrainStatus()                     [main loop sees status]
  → main loop assigns bar.status = last
  → main loop puts vector in ctx.frame_statuses

NewsScreen::OnFrame(ctx)
  → client->DrainHeadlines()             [N new ones]
  → for each, insert sorted into stream
  → if status contains "Historical loaded:", flip loaded_ flag
  → rebuild feed_block_

NewsScreen::Render(ctx)
  → feed_block_.Render(ctx)
  → DrawHoverRect if hovered

(Later, when TWS pushes a live tickNews):
TWS sends tickNews(streamId, ts, provider, articleId, headline, ...)
  → NewsService::OnTickNews
    → dedupe via seen_article_ids_, append to headlines_

NewsScreen::OnFrame picks it up
  → marks article_id with flash_until_ms = now+5s
  → next render shows it bold via bold_body_cache
```

### Flow 2: user clicks an article

```
SDL_EVENT_MOUSE_BUTTON_DOWN
  → main loop forwards to current_screen->OnEvent
  → NewsScreen::OnFeedEvent
    → MouseYToBodyRow(window, metrics, e.button.y)  [widgets.cc]
    → look up headlines_[idx]
    → client->RequestArticle(provider, articleId)
      → Dispatcher → NewsService → reqNewsArticle
    → set reading_ = true, populate reader_block_ with placeholder

(EReader thread, ~1s later):
TWS sends newsArticle(reqId, type, text)
  → NewsService::OnNewsArticle  → stores in article_ slot

Next frame:
NewsScreen::OnFrame
  → client->DrainArticle()
  → if reading_ && id matches, StripHtml + LayoutText →
    reader_block_.SetBody(...)

NewsScreen::Render
  → reading_ true → reader_block_.Render
```

### Flow 3: ESC out of reader

```
SDL_EVENT_KEY_DOWN, k=ESCAPE
  → main loop: hand to current_screen first
  → NewsScreen::OnReaderEvent sees ESC
    → reading_ = false; clear reader_block_
    → return true (consumed)
  → main loop: consumed → don't drop to body
  → HandleEscape(&bar) clears function-bar input
```

If `NewsScreen::OnEvent` had returned `false` (not in reader mode),
the main loop's ESC path would have replaced `current_screen` with a
fresh `SplashScreen(host, port, client_id)`.

### Flow 4: window resize

```
SDL_EVENT_WINDOW_RESIZED
  → main loop: recompute metrics = ComputeGridMetrics(window, font)
  → forward event to current_screen->OnEvent
    → screens re-layout (LayoutText for body text, ScrollableTextBlock
      reclamps scroll, etc.)
  → main loop: clamp function-bar input width
```

## Threading model

Two threads matter:

- **Main thread**: SDL events, rendering, `client.Pump()`, all
  `current_screen->*` calls. Drives the message queue forward via
  `processMsgs()`.
- **EReader thread**: owned by the TWS API library. Reads bytes off
  the socket, parses, and invokes EWrapper callbacks. Started by
  `EReader::start()` in `IbkrClient::Connect`.

### The shared mutex

`Dispatcher::mu_` (`src/ibkr_client.cc`). Every public facade method
and every EWrapper override locks this mutex. Services therefore
**never lock themselves**: they assume the caller already holds the
lock. This keeps the services testable in isolation.

The mutex is taken for the duration of the callback / facade call,
including the call into `EClient::reqXxx`. This is fine: the
underlying TWS client is internally thread-safe and the request
methods are non-blocking (they enqueue to the writer thread).

### Why `client.Pump()` is non-blocking

The old version called `signal.waitForSignal()` (block up to 2000ms),
which froze the UI when nothing was streaming. The current `Pump()`
just calls `reader->processMsgs()` — the EReader thread queues
messages whenever they arrive; `processMsgs` drains the queue
non-blockingly on the calling thread. See `src/ibkr_client.cc`
`IbkrClient::Pump`.

## The Dispatcher pattern

`src/ibkr_client.cc`. The single EWrapper that TWS sees. Each
override is **a 1–3 line forward** under the lock:

```cpp
void contractDetails(int reqId, const ContractDetails& d) override {
  std::lock_guard<std::mutex> lock(mu_);
  news_.OnContractDetails(reqId, d);
}
```

The dispatcher knows which service owns which callback:

| EWrapper callback | Service |
|---|---|
| `contractDetails`, `contractDetailsEnd` | `NewsService` |
| `historicalNews`, `historicalNewsEnd` | `NewsService` |
| `tickNews`, `newsProviders`, `newsArticle` | `NewsService` |
| `managedAccounts` | `PortfolioService` |
| `accountUpdateMulti(End)`, `positionMulti(End)` | `PortfolioService` |
| `pnl`, `pnlSingle` | `PortfolioService` |
| `historicalData(End/Update)` | `ChartService` |
| `nextValidId`, `connectionClosed` | broadcast to all services |
| `error` | tries `NewsService.OnError` first (broadtape probes); falls through to status |

When a callback's req-id ownership is ambiguous, the dispatcher asks
the service via `OwnsReqId(int)`. The only callback that currently
uses this is `error()`.

## Where logic lives — a quick lookup

| Logic | File |
|---|---|
| Mouse → body row math | `src/widgets.cc::MouseYToBodyRow` |
| HTML strip for article body | `src/news_screen.cc::StripHtml` |
| News headline metadata strip (`{A:...:K:...}`) | `src/news_screen.cc::CleanHeadline` |
| Candlestick draw | `src/chart_screen.cc::Render` |
| Portfolio cross-account merge | `src/services/portfolio_service.cc::Snapshot` |
| BroadTape source codes | `src/services/news_service.cc::kBroadtapeSources` |
| Article dedupe set | `src/services/news_service.cc::seen_article_ids_` |
| Bold-flash logic | `src/news_screen.cc::OnFrame` + `RebuildFeedLines` |
| `nextValidId` request replay | `src/ibkr_client.cc::Dispatcher::nextValidId` |

## Observability seams

- **Logging**: `src/log.{h,cc}` — see `docs/logging.md`. Every EWrapper
  callback in `Dispatcher` (`src/ibkr_client.cc`) emits a `TRACE` line
  under category `tws.dispatch`; outbound facade calls emit `DEBUG`
  under `tws`.
- **Diagnostics**: `src/diagnostics.{h,cc}` — global snapshot read by
  the `DEBUG` overlay screen. Producers in `main.cc` (frame timing,
  screen name) and the `IbkrClient` lifecycle.
- **Testing**: see `docs/testing.md`. The `RenderBackend` and
  `IIbkrClient` interfaces are the two main mocking seams.

## When to break the rules

The arrows-go-down convention is firm, but in practice:

- A screen reading the function bar to set status text is OK
  (`ctx.bar->status = ...`). It's writing, not reading; one-way.
- A widget knowing about `ScreenContext` is OK — widgets get one
  passed in to render. They use only the rendering bits (renderer,
  caches, metrics).
- Services never see screens or widgets. This is firm.
