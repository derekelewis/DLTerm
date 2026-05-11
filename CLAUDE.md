# CLAUDE.md

High-level orientation for working in this repo. Detailed docs live in
`docs/` — this file is the index.

## What this is

**dlterm** is a Bloomberg-terminal-style desktop app: a monospace text
grid (SDL3 + IBM Plex Mono) with a function bar at the top and a
single-screen body below. Commands typed into the function bar
(`INFO`, `AAPL NEWS`, `POS`, `<SYMBOL> CHART`, etc.) switch the body
view. Live data comes from a local Interactive Brokers TWS or IB
Gateway via the C++ TWS API.

## High-level architecture

```
┌─────────────────────────────────────────────────────────────┐
│                       main.cc (event loop)                   │
└────────────────┬────────────────┬───────────────────────────┘
                 │                │
                 ▼                ▼
        ┌─────────────────┐  ┌─────────────────┐
        │  Screen (each   │  │  IbkrClient     │
        │   command = a   │  │   (facade +     │
        │   screen file)  │  │   Dispatcher)   │
        └────────┬────────┘  └────────┬────────┘
                 │                    │
                 ▼                    ▼
        ┌─────────────────┐  ┌─────────────────┐
        │  Widgets        │  │  Services       │
        │  (TextBlock,    │  │  (News, Portfolio,│
        │   Table, …)     │  │   Chart)        │
        └─────────────────┘  └─────────────────┘
                                      │
                                      ▼
                            ┌─────────────────┐
                            │   TWS API       │
                            │  (libTwsSocket  │
                            │   Client.dylib) │
                            └─────────────────┘
```

- **`main.cc`** owns SDL + the function bar + a `current_screen`
  pointer. The main loop pumps events to the screen, ticks the client,
  and renders.
- **Screens** (`src/*_screen.{h,cc}`) implement the `Screen`
  interface. Each one is self-contained: lifecycle, event handling,
  rendering. To add a function, write a screen file.
- **Widgets** (`src/widgets.{h,cc}`) are stateless helpers + reusable
  view primitives (`TextBlock`, `ScrollableTextBlock`, `Table`,
  `DrawHoverRect`, etc.). Screens compose widgets.
- **Services** (`src/services/*.{h,cc}`) own TWS-feature state
  (`NewsService`, `PortfolioService`, `ChartService`). Each one is one
  state machine for one feature.
- **`IbkrClient`** (`src/ibkr_client.{h,cc}`) is the thin facade plus
  a `Dispatcher` (the EWrapper TWS sees). The dispatcher's only job
  is routing each EWrapper callback to the right service under a
  shared mutex.

## Critical conventions

- **C++23, Google-ish style**: `.h`/`.cc` filenames, `snake_case`
  members with trailing `_`, `CamelCase` types/functions,
  `kCamelCase` constants, 2-space indent. Configured via
  `.clang-format`.
- **22pt font**, monospace grid. Cell width/height are computed at
  startup and on resize via `dlterm::ComputeGridMetrics`.
- **Pixel density**: mouse events arrive in window points; the grid
  is sized in pixels. Always convert with
  `MouseYToBodyRow(ctx.pixel_density, ctx.metrics, event_y)`.
- **Drawing**: screens never call SDL directly — they go through
  `ctx.render` (the `RenderBackend` interface). Production wraps SDL;
  tests inject a `SpyRenderBackend`. See `src/render_backend.h`.
- **TWS access**: `ctx.client` is `IIbkrClient*` (interface). The live
  `IbkrClient` and the `tests::FakeIbkrClient` both implement it.
- **Threading**: TWS callbacks fire on the `EReader` background
  thread. The `Dispatcher` takes one shared mutex (`mu_` inside it)
  around every override and around every facade call. Services
  never lock themselves; they assume the caller holds the lock.
- **Screen lifecycle**: `OnEnter` → (frame loop: `OnFrame` then
  `OnEvent`s then `Render`) → `OnExit`. Switching screens always
  calls `OnExit` on the outgoing and `OnEnter` on the incoming.
- **ESC at the screen level** drops back to the body view, with one
  exception: a screen may consume ESC (return `true` from `OnEvent`)
  to handle a modal sub-state — currently only `NewsScreen` does this
  for the reader sub-mode (ESC from reader → back to feed).

## Build & run

```
cmake -B build
cmake --build build -j
DLTERM_TWS_HOST=127.0.0.1 DLTERM_TWS_PORT=4002 DLTERM_TWS_CLIENT_ID=1 \
  ./build/DLTerm
```

The TWS API headers + prebuilt dylib come from
`/Users/dlewis/Downloads/twsapi_macunix/IBJts/source/cppclient/client/`
(override with `-DDLTERM_TWSAPI_DIR=...`). See `docs/build.md` for the
full list of deps (SDL3, SDL3_ttf, protobuf, abseil, zlib) and the
`tws_smoketest` harness.

Tests:

```
ctest --test-dir build --output-on-failure
```

CI / no-TWS build:

```
cmake -B build-ci -DDLTERM_BUILD_APP=OFF
cmake --build build-ci -j --target dlterm_tests
ctest --test-dir build-ci --output-on-failure
```

## Documentation index

| File | Purpose |
|---|---|
| `docs/architecture.md` | The full layering; request flows (e.g. "user types AAPL NEWS"); threading model; ScreenContext details. |
| `docs/screens.md` | The `Screen` interface contract, lifecycle, how to add a new screen, catalog of existing screens. |
| `docs/widgets.md` | The widget catalog: helpers (`BodyCols`, `TruncateToCols`, `MouseYToBodyRow`, `DrawHoverRect`) and view primitives (`TextBlock`, `ScrollableTextBlock`, `Table`). |
| `docs/services.md` | The TWS service layer: `NewsService`, `PortfolioService`, `ChartService`. Dispatcher routing, `ServiceContext`, adding a new service. |
| `docs/commands.md` | Function-bar parsing (`RunCommand`), the `CommandKind` enum, the `MakeScreenFor` factory, adding a new verb. |
| `docs/tws-api.md` | Hard-won knowledge about the TWS API surface: BroadTape contracts, `mdoff,292:PROVIDER` format, market-data subscription gotchas, sentinel values, multi-account quirks, the `nextValidId` race. |
| `docs/build.md` | CMake structure, third-party deps, env vars, the smoketest, dylib rpath setup. |
| `docs/testing.md` | GoogleTest setup, the `SpyRenderBackend` + `FakeIbkrClient` test infrastructure, fixture patterns. |
| `docs/logging.md` | The in-tree logger: levels, categories, env-var filter syntax, ring buffer, `DLTERM_DCHECK` / `DLTERM_CHECK`. |

## Keeping docs in sync

When you change the code, update the matching doc:

| If you edit… | Update… |
|---|---|
| `src/screen.h` (the interface) | `docs/screens.md` |
| Add/remove a screen | `docs/screens.md` + `docs/commands.md` (factory map) |
| `src/widgets.{h,cc}` | `docs/widgets.md` |
| `src/render_backend.h` / new backend | `docs/widgets.md` + `docs/testing.md` |
| `src/services/*` | `docs/services.md` |
| `src/function_bar.{h,cc}` (commands) | `docs/commands.md` |
| TWS API integration | `docs/tws-api.md` |
| Build system / deps / dylibs | `docs/build.md` |
| Layering between components | `docs/architecture.md` |
| `src/log.{h,cc}` / new log category | `docs/logging.md` |
| Add a test file or fixture pattern | `docs/testing.md` |
| `src/ibkr_client.h` (`IIbkrClient`) | `docs/services.md` + `docs/testing.md` |

If you don't know which doc applies, default to `architecture.md` — it
covers the seams between components.

## Where to start when reading

1. **`docs/architecture.md`** to see the layering and request flows.
2. **`src/main.cc`** to see the event loop (350 lines, scannable).
3. **`src/news_screen.cc`** for the most-feature-complete screen
   example (mouse hover, click, scroll, bold flash, modal sub-mode).
4. **`src/services/news_service.cc`** for the most-state-heavy TWS
   service example.
