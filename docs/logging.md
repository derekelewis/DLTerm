# Logging & diagnostics

dlterm has an in-tree structured logger (`src/log.{h,cc}`), a global
diagnostics snapshot (`src/diagnostics.{h,cc}`), and a `DEBUG` overlay
screen that shows both. There are no external logging dependencies.

## Macros

```cpp
#include "log.h"

DLTERM_LOG_TRACE("category", "fmt {} {}", a, b);
DLTERM_LOG_DEBUG("category", ...);
DLTERM_LOG_INFO("category",  ...);
DLTERM_LOG_WARN("category",  ...);
DLTERM_LOG_ERROR("category", ...);

DLTERM_DCHECK(condition);  // assert in debug only
DLTERM_CHECK(condition);   // always-on assert; abort on fail
```

The macros forward to `dlterm::log::EmitFormat` which:

1. Checks `Enabled(level, category)` (cheap — one map lookup) and
   skips formatting when filtered.
2. Otherwise, formats with `std::format` and emits.

Format strings are checked at compile time (C++23 `std::format_string`).

## Levels

```
TRACE < DEBUG < INFO < WARN < ERROR
```

| Level | Use for |
|---|---|
| `TRACE` | Per-callback dispatch, every EWrapper hook. Very chatty. |
| `DEBUG` | State transitions, outbound TWS requests, command parses. |
| `INFO` | User-visible lifecycle events (connect, disconnect, screen open). |
| `WARN` | Recoverable problems (TWS connection closed, retried). |
| `ERROR` | Failures the user should know about. |

Default level is `INFO` — `DEBUG`/`TRACE` are silenced unless the
filter says otherwise.

## Categories

Free-form string tags. Existing categories:

| Category | What fires |
|---|---|
| `main` | App startup / shutdown. |
| `command` | Function-bar `RunCommand` results. |
| `tws` | Outbound facade calls (`SubscribeNews`, `RequestChart`, …) and connect/disconnect lifecycle. |
| `tws.dispatch` | Every EWrapper inbound callback (after the dispatcher mutex is taken). |

Subcategories use a `parent.child` dotted notation. Filters that name
a parent match all children: `tws=trace` enables `tws`, `tws.dispatch`,
`tws.anything-else`. The longest-prefix filter wins.

## `DLTERM_LOG` env var

The filter spec is parsed at startup (or first emit, whichever comes
first). Format:

```
[default-level][,category=level]*
```

Examples:

```
DLTERM_LOG=info                   # default; only INFO+ fires
DLTERM_LOG=debug                  # everything DEBUG+
DLTERM_LOG=info,tws=trace         # INFO global, TRACE on tws + tws.*
DLTERM_LOG=warn,tws.dispatch=trace
                                  # quiet by default, only TRACE on dispatch
```

The default when the var is unset is `info`.

Level names are case-insensitive. `WARN` and `WARNING` both work.

## File sink

```
DLTERM_LOG_FILE=/tmp/dlterm.log   # mirror to file (line-buffered append)
```

Lines are written to both stderr and the file. The file is opened
once at startup and flushed after each line.

## Output format

```
HH:MM:SS.mmm LEVEL [category] message
```

Example:

```
14:32:01.123 INFO  [tws] Connect host=127.0.0.1 port=4002 client_id=1
14:32:01.456 TRACE [tws.dispatch] historicalNews reqId=1003 provider=BRFG id=ABC time=2026-05-09 ...
14:32:02.001 DEBUG [command] input='AAPL NEWS' kind=2
```

The level field is padded to 5 chars so columns line up.

## Ring buffer

Every emitted line also lands in an in-memory ring buffer (capacity
512). The DEBUG overlay screen reads the tail to display recent log
output without needing a tail tool on a file.

```cpp
#include "log.h"
auto entries = dlterm::log::RecentEntries(50);  // up to 50 most recent
for (const auto& e : entries) {
  // e.timestamp_ms, e.level, e.category, e.message
}
```

## Assertions

```cpp
DLTERM_CHECK(font != nullptr);          // always evaluates; abort on fail
DLTERM_DCHECK(req_id != 0);             // no-op when -DNDEBUG
```

A failed `DLTERM_CHECK` prints the file/line/expression to stderr and
calls `std::abort()`. Used at hard-failure boundaries (init code).
`DLTERM_DCHECK` is for invariants that should always hold but whose
check has runtime cost; sprinkled in widgets and services.

## Diagnostics

`src/diagnostics.{h,cc}`. Thread-safe global snapshot for the DEBUG
overlay. Producers:

```cpp
dlterm::diag::RecordFrame(dt_ms);                    // main loop, per frame
dlterm::diag::SetCurrentScreen("PortfolioScreen");   // on screen swap
dlterm::diag::SetTwsConnected(true);                 // on connect/disconnect
dlterm::diag::SetTwsTarget("127.0.0.1", 4002, 1);   // once at startup
```

Consumer:

```cpp
auto s = dlterm::diag::Get();  // copy of the current Snapshot
// s.frame_avg_ms (60-frame rolling), s.frame_last_ms, s.frame_count
// s.current_screen, s.tws_connected, s.tws_host, s.tws_port, s.tws_client_id
```

## DEBUG overlay screen

`src/debug_screen.{h,cc}`. Triggered by typing `DEBUG` in the function
bar. Shows the diagnostics snapshot at the top and the log ring-buffer
tail at the bottom. ESC returns to the body screen as usual.

## Performance

- The fast path for a filtered call is one map lookup + one `std::less`
  comparison under a mutex. Acceptable for hot paths but not free.
- For very chatty callbacks (the EReader thread can deliver thousands
  of `historicalData` rows in a second), the call is `TRACE` so it's
  off by default.
- `std::format` is only called when the level passes the filter
  (`EmitFormat` checks `Enabled` first).

## Tips

- To debug a specific TWS feature: `DLTERM_LOG=info,tws.dispatch=trace`.
- To capture a session for later review: add `DLTERM_LOG_FILE=run.log`.
- To temporarily silence noisy categories during a test: `DLTERM_LOG=info,tws.dispatch=warn`.
- Tests use `dlterm::log::SetFilterForTesting(spec)` and
  `dlterm::log::Shutdown()` to control the logger without depending on
  env vars.
