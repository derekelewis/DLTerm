# Commands

The function bar at the top of the window accepts Bloomberg-style
commands: whitespace-separated tokens, RETURN to dispatch, ESC to
clear. All input is upper-cased; symbols and verbs match
case-insensitively.

This doc is the canonical command reference and the recipe for
adding new verbs.

## Existing commands

| Command | Effect | Screen |
|---|---|---|
| `INFO` | Show window/grid/font diagnostics. | `BodyScreen` (static) |
| `NEWS` | Stream BroadTape general news (BRFG, DJNL, BZ, FLY, …). | `NewsScreen("")` |
| `<SYMBOL> NEWS` | Per-ticker news: 50 historical + live stream. | `NewsScreen(symbol)` |
| `POS` | Multi-account merged positions table with live P&L. | `PortfolioScreen` |
| `<SYMBOL>` | Live quote for the symbol via `reqMktData` (bid/ask/last + sizes, day's high/low/open/prev close, volume, last-tick time). Auto-detects futures local symbols (`CLM6`, `ESM26`); everything else is a stock. | `QuoteScreen(symbol, kStock or kFutureLocal)` |
| `<SYMBOL> FUT` | Live quote on a continuous-future (CONTFUT). Symbol must be in `ExchangeForContinuousFuture` (e.g. `CL`, `ES`, `GC`). | `QuoteScreen(symbol, kContinuousFuture)` |
| `<SYMBOL> CHART` | 1-day, 5-min candlesticks, live-updating. Parser auto-detects futures local symbols (alpha root + month code + 1-2 digit year, e.g. `CLM6`, `ESM26`) and routes them through the live FUT path; everything else is treated as a stock. | `ChartScreen(symbol, kStock or kFutureLocal)` |
| `<SYMBOL> FUT CHART` | Continuous-futures chart (historical-only, no live tail — polled every 30s). Symbol must be in the `ExchangeForContinuousFuture` table — `CL`, `ES`, `GC`, `ZN`, etc. | `ChartScreen(symbol, kContinuousFuture)` |
| `DEBUG` | Live diagnostics overlay: frame timing, TWS state, log tail. | `DebugScreen` |
| `EXIT` / `QUIT` | Terminate the app cleanly (same as ⌘Q or closing the window). | (none — sets `running = false` in `main.cc`) |

`<SYMBOL>` is 1–8 characters, ASCII letters plus `.`. Anything else
yields `"Invalid symbol: …"` in the function-bar status.

ESC at any time returns to the body view (or, inside the news reader
sub-mode, returns to the feed).

## Tokenization & parsing

`src/function_bar.cc::Tokenize` splits on whitespace and upper-cases
each token. `RunCommand(command, info)` walks the token list and
returns a `CommandResult`:

```cpp
struct CommandResult {
  CommandKind kind = CommandKind::kNone;
  std::string text;     // for kStaticText
  std::string symbol;   // for kStartNews / kStartChart
  std::string status;   // diagnostic string for the function bar
  ChartInstrument instrument = ChartInstrument::kStock;  // for kStartChart
};
```

`CommandKind` (in `src/function_bar.h`):

- `kNone` — nothing to do (empty command).
- `kStaticText` — main loop swaps to `BodyScreen(text)`.
- `kStartNews` — main loop swaps to `NewsScreen(symbol)`.
- `kStartGeneralNews` — `NewsScreen("")`.
- `kStartPortfolio` — `PortfolioScreen`.
- `kStartChart` — `ChartScreen(symbol)`.
- `kStartQuote` — `QuoteScreen(symbol)`.
- `kStartDebug` — `DebugScreen`.
- `kQuit` — main loop exits; no screen swap.

The parser is intentionally flat: explicit handler per shape, no
generic verb registry. New commands add one branch in `RunCommand`
and one enum entry.

## The screen factory

`src/screen_factory.cc::MakeScreenFor(result, sample_text)` maps
`CommandKind` → `unique_ptr<Screen>`:

```cpp
case CommandKind::kStartChart:
  return std::make_unique<ChartScreen>(result.symbol);
case CommandKind::kStartPortfolio:
  return std::make_unique<PortfolioScreen>();
case CommandKind::kStartNews:
  return std::make_unique<NewsScreen>(result.symbol);
case CommandKind::kStartGeneralNews:
  return std::make_unique<NewsScreen>("");
case CommandKind::kStartQuote:
  return std::make_unique<QuoteScreen>(result.symbol, result.instrument);
case CommandKind::kStaticText:
  return std::make_unique<BodyScreen>(result.text);
default:
  return nullptr;  // main loop keeps current screen, may show r.status
```

This is the **single registration point.** `main.cc` doesn't enumerate
screens.

## The dispatch flow

```
KEY_DOWN: RETURN
  → main.cc reads bar.input
  → RunCommand(bar.input, info)              [function_bar.cc]
  → if r.kind != kNone && r.kind != kStaticText:
      Connect lazily (if not connected) — sets bar.status on failure
  → MakeScreenFor(r, sample_text)            [screen_factory.cc]
  → if new screen:
      current_screen->OnExit(ctx)
      current_screen = std::move(new_screen)
      current_screen->OnEnter(ctx)
  → bar.input.clear()
  → if r.status non-empty: bar.status = r.status
```

`OnExit` is responsible for tearing down any TWS subscriptions the
old screen started. `OnEnter` starts the new ones.

## Adding a new verb — checklist

Example: a `QUOTE` command that shows a live quote box for a symbol.

1. **`src/function_bar.h`** — add `kStartQuote` to `CommandKind`.

2. **`src/function_bar.cc::RunCommand`** — add the parse branch:
   ```cpp
   if (tokens.size() == 2 && tokens[1] == "QUOTE") {
     if (!IsValidSymbol(tokens[0])) {
       r.status = "Invalid symbol: " + tokens[0];
       return r;
     }
     r.kind = CommandKind::kStartQuote;
     r.symbol = tokens[0];
     return r;
   }
   ```

3. **`src/quote_screen.{h,cc}`** — implement `class QuoteScreen :
   public Screen`. `OnEnter` subscribes; `OnExit` cancels.

4. **`src/screen_factory.cc::MakeScreenFor`** — add the arm:
   ```cpp
   case CommandKind::kStartQuote:
     return std::make_unique<QuoteScreen>(result.symbol);
   ```

5. **`CMakeLists.txt`** — add `src/quote_screen.cc` to the
   `add_executable(dlterm ...)` list.

6. **`docs/commands.md`** (this file) — add a row to the table above.

7. If a new TWS feature is involved, also create a service in
   `src/services/` and a facade method in `IbkrClient`. See
   `docs/services.md` for that recipe.

## Symbol validation

`IsValidSymbol` (`src/function_bar.cc`):

```cpp
bool IsValidSymbol(const std::string& s) {
  if (s.empty() || s.size() > 8) return false;
  for (char c : s) {
    if (!(std::isalpha(c) || c == '.')) return false;
  }
  return true;
}
```

Reuse this. Don't reinvent symbol validation per command.

## Help / fallback behavior

Unknown commands set `r.status = "Unknown command: <joined tokens>"`,
and `main.cc` flashes that in the function bar instead of swapping
the screen.

There's no `HELP` command yet. The body of the default screen lists
the available verbs (the sample text); typing `INFO` shows
diagnostics.

## Connection lifecycle

The main loop maintains a single `IbkrClient` and connects lazily on
the first command that needs TWS (`kStartNews`, `kStartGeneralNews`,
`kStartPortfolio`, `kStartChart`). On `Disconnect` (or `connectionClosed`
from TWS), all services reset; subsequent commands re-establish the
connection.

Connection parameters:

- Host: `DLTERM_TWS_HOST` env, default `127.0.0.1`.
- Port: `DLTERM_TWS_PORT` env, default `4002` (IB Gateway paper).
- Client ID: `DLTERM_TWS_CLIENT_ID` env, default `1`.

See `docs/build.md` for build-time config and `docs/tws-api.md` for
the connection sequence and gotchas.

## Future verbs (sketches)

These have been discussed but not implemented. Listed here so the
parsing recipe doesn't surprise you when they land.

- `<SYMBOL> OPT [<EXPIRY>]` — options chain.
- `WATCH <SYM> <SYM> …` — multi-symbol watchlist.
- `<SYMBOL> CHART <RANGE>` — let the user override `"1 D"` and
  `"5 mins"`. The current `ChartScreen` hardcodes both.

None of these are gated; just follow the checklist.
