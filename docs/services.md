# Services

The TWS-feature state machines. Each service owns one TWS feature
(news, portfolio, chart) end-to-end: its subscription bookkeeping,
its state, its `EWrapper` callback handlers.

Services do not lock themselves. The `Dispatcher` (in
`src/ibkr_client.cc`) takes a shared mutex around every call into a
service, then delegates. This keeps services simple and testable in
isolation.

## Files

```
src/services/
  service_context.h       — shared bag (EClient*, req_id counter, status queue)
  contracts.h             — Contract builders shared by services (STK/CONTFUT/FUT)
  news_service.{h,cc}     — per-ticker NEWS + BroadTape + article fetch
  portfolio_service.{h,cc} — multi-account positions + P&L
  chart_service.{h,cc}    — historical bars + live updates
  quote_service.{h,cc}    — live Level-I quote (reqMktData)
  info_service.{h,cc}     — TWS metadata for the splash screen
```

## ServiceContext

`src/services/service_context.h`. The shared state every service
needs:

```cpp
struct ServiceContext {
  EClient* client = nullptr;
  int next_req_id = 1000;
  std::vector<std::string> status_queue;

  int NextReqId() { return ++next_req_id; }
  void EmitStatus(std::string msg) { status_queue.push_back(std::move(msg)); }
};
```

- `client` is the `EClientSocket` to send requests on. The dispatcher
  sets it once (from `Impl::Impl()`).
- `next_req_id` is the global monotonic request-id counter. TWS
  requires request IDs unique within a connection — so all services
  share this counter via `ctx.NextReqId()`.
- `status_queue` is the shared message stream that `IbkrClient::
  DrainStatus()` returns each frame. Services append via
  `ctx.EmitStatus("…")`.

All `ServiceContext` access is serialized by the dispatcher's mutex.

### Testing services in isolation

Services check `ctx_.client` for null before any `EClient` call, so
unit tests construct a `ServiceContext{}` with `client = nullptr`,
then drive the EWrapper hooks directly:

```cpp
ServiceContext ctx;
ChartService svc{ctx};
svc.Request("MSFT");                          // allocates req_id
svc.OnHistoricalData(req_id, MakeBar(...));  // simulate TWS callback
svc.OnHistoricalDataEnd(req_id);
EXPECT_TRUE(svc.Snapshot().ready);
```

See `tests/unit/services/` for the canonical patterns.

## The Dispatcher

`src/ibkr_client.cc`. The lone `EWrapper` instance TWS sees. Every
override is a 1–3 line forward under the mutex, plus a `TRACE` log
under the `tws.dispatch` category for debugging:

```cpp
void Dispatcher::historicalNews(int reqId, ...) override {
  std::lock_guard<std::mutex> lock(mu_);
  DLTERM_LOG_TRACE("tws.dispatch",
      "historicalNews reqId={} provider={} ...", reqId, providerCode);
  news_.OnHistoricalNews(reqId, ...);
}
```

To debug TWS interactions:

```bash
DLTERM_LOG=info,tws.dispatch=trace ./build/dlterm 2>&1 | tee run.log
```

Then in the running app: type a command, watch outbound `tws` DEBUG
lines and inbound `tws.dispatch` TRACE lines line up. Or open the
`DEBUG` overlay screen to see the same lines in-app.

The dispatcher also owns the public-facing facade methods that
`IbkrClient` forwards to. Same locking pattern:

```cpp
PortfolioSnapshot SnapshotPortfolio() const {
  std::lock_guard<std::mutex> lock(mu_);
  return portfolio_.Snapshot();
}
```

### EWrapper → Service routing

| Callback | Service | Notes |
|---|---|---|
| `contractDetails`, `contractDetailsEnd` | `NewsService` | Used by both per-ticker resolve AND broadtape probes; service distinguishes via internal stage. |
| `historicalNews`, `historicalNewsEnd` | `NewsService` | |
| `tickNews` | `NewsService` | Handles both broadtape streams and per-ticker live tail. |
| `newsArticle` | `NewsService` | |
| `newsProviders` | `NewsService` + `InfoService` | Fanned out; news service builds the codes list for BroadTape, info service keeps code+name pairs for the splash. |
| `historicalData`, `historicalDataEnd`, `historicalDataUpdate` | `ChartService` | |
| `tickPrice`, `tickSize`, `tickString` | `QuoteService` | TWS tick types defined in `EWrapper.h::enum TickType`. Only BID/ASK/LAST/HIGH/LOW/OPEN/CLOSE + BID_SIZE/ASK_SIZE/LAST_SIZE/VOLUME + LAST_TIMESTAMP are kept; the rest are dropped. |
| `managedAccounts` | `PortfolioService` + `InfoService` | Fanned out to both. |
| `accountUpdateMulti(End)`, `positionMulti(End)` | `PortfolioService` | |
| `pnl`, `pnlSingle` | `PortfolioService` | |
| `currentTime` | `InfoService` | Server-time reply, drives the splash skew display. |
| `nextValidId` | **All services** (`OnReady`) | Each replays its queued requests; `InfoService` also fires the first `reqCurrentTime` here. |
| `connectionClosed` | Dispatcher (status). | Plus all services reset on disconnect. |
| `error` | `NewsService::OnError` first → fallback to status | Used to silence broadtape `200 No security definition` probe failures. |

### `OnReady` ordering

When `nextValidId` fires the dispatcher calls every service's
`OnReady()` in turn. This is the place to replay deferred subscriptions
that were issued before the connection was ready.

### `OnReset`

Called by the dispatcher on `Disconnect`. Each service must reset all
state — subscriptions, IDs, accumulated payloads.

## NewsService

`src/services/news_service.{h,cc}`.

The biggest service (~330 lines). Hosts three flows in one state
machine:

1. **Per-ticker news**: contract resolution → historical batch →
   live ticker subscription. Stages: `kResolving` → `kFetchingHist`
   → `kStreamingTicker`.
2. **BroadTape (general news)**: probes 7 known source codes via
   `reqContractDetails`, subscribes to whichever return valid
   contracts via `reqMktData("mdoff,292")`. Stages: `kBroadtapeDiscover`
   → `kBroadtapeStream`. Falls back to `StartTickerSession("SPY")`
   if no BroadTape contracts are available.
3. **Article fetch**: single-slot. `reqNewsArticle` + the
   `newsArticle` callback.

### Public API

```cpp
void StartTickerSession(const std::string& symbol);
void StartGeneralSession();
void CancelActive();
int RequestArticle(const std::string& provider, const std::string& id);
std::vector<NewsHeadline> DrainHeadlines();
std::optional<ArticlePayload> DrainArticle();
```

### Key invariants

- **Article dedupe**: `seen_article_ids_` (a `std::set<std::string>`)
  prevents duplicate headlines from showing up when TWS replays
  recent history on a live subscription. Both `OnHistoricalNews` and
  `OnTickNews` check it via `insert(...).second`.
- **Provider codes**: cached in `provider_codes_` (joined with `+`)
  via `OnNewsProviders`. Used as the `providerCodes` arg to
  `reqHistoricalNews` and `mdoff,292:<providers>` on `reqMktData`.
  If empty when needed, the service falls back to
  `"BRFG+BRFUPDN+DJNL"`.
- **BroadTape sources**: the constant `kBroadtapeSources[] = {"BRFG",
  "BRFUPDN", "DJ", "DJNL", "BZ", "DJTOP", "FLY"}` is the gateway's
  documented valid set. Probes for sources the account isn't
  entitled to return `error 200`, which the dispatcher routes back
  to `NewsService::OnError` for silent handling.

## PortfolioService

`src/services/portfolio_service.{h,cc}`.

Multi-account portfolio (~270 lines). For each account in
`managed_accounts_`, fires three concurrent subscriptions:

- `reqAccountUpdatesMulti(..., ledgerAndNLV=false)` → account totals
  (Net Liq, Buying Power, Cash, etc.).
- `reqPositionsMulti(...)` → per-position rows (lazy `reqPnLSingle`
  per new conId).
- `reqPnL(...)` → account-rollup live P&L.

Per-position live updates come through `pnlSingle`. The service stores
rows keyed by `(account, conId)` and aggregates to per-symbol rows
in `Snapshot()` — positions in the same ticker across multiple
accounts merge.

### Key gotchas

- **`ledgerAndNLV=false` is the right call** — passing `true` returns
  *only* `NetLiquidation` (we got bit by this). False returns the
  full set including `BuyingPower`, `TotalCashValue`, etc.
- **Sentinel values**: IB sends `DBL_MAX` (or near it) for
  not-yet-computed P&L fields. The service runs every numeric through
  `SafeNumeric()` which clamps non-finite or oversized values to 0.
- **Cross-account merge**: `Snapshot()` aggregates by
  `(symbol, sec_type, currency)`. Sums position/market_value/PnL,
  position-weighted average cost, comma-joined account list.
- **Subscribe order**: `Subscribe()` requires `managed_accounts_` to
  be populated, which comes from `managedAccounts` callback. If
  called before that fires, sets `pending_subscribe_ = true` and
  replays via `OnManagedAccounts`.

## InfoService

`src/services/info_service.{h,cc}`.

Smallest service. Owns the TWS metadata shown on the splash screen:
managed-accounts list, news-provider list (code + name), and the
latest `currentTime` reply. Reads `serverVersion()` and
`TwsConnectionTime()` straight off the EClient in `Snapshot()` —
they're synchronous and cheap.

### Public API

```cpp
void RequestCurrentTime();
InfoSnapshot Snapshot() const;
```

### Behavior

- `OnReady()` fires `reqCurrentTime()` so the splash has a server
  timestamp as soon as the link is up.
- `OnManagedAccounts(list)` and `OnNewsProviders(...)` are fanned to
  this service in parallel with the existing
  `PortfolioService`/`NewsService` consumers.
- The splash polls every 5s by calling `client->RequestServerTime()`
  to keep the skew display fresh.

## ChartService

`src/services/chart_service.{h,cc}`.

Smallest service (~120 lines). One historical-bars subscription with
`keepUpToDate=true` for live updates.

### Public API

```cpp
void Request(const std::string& symbol);
void Cancel();
ChartSnapshot Snapshot() const;
```

### Behavior

- `Request("AAPL")` fires `reqHistoricalData(reqId, AAPL_STK_SMART_USD,
  "", "1 D", "5 mins", "TRADES", 1, 2, true, …)`. RTH only;
  formatDate=2 → epoch-second timestamps.
- `historicalData` callbacks build the initial bar vector.
- `historicalDataEnd` flips `ready_` and emits the loaded status.
- `historicalDataUpdate` callbacks (fired continuously while live)
  either update the last bar in place (same `ts`) or append a new
  bar at the boundary.

### Pending-request pattern

If `Request()` is called before `nextValidId` arrives, the service
sets `pending_subscribe_ = true` and `pending_symbol_`. `OnReady()`
fires the queued `reqHistoricalData`. This is the same pattern
`NewsService` and `PortfolioService` use.

## QuoteService

`src/services/quote_service.{h,cc}`.

Single-symbol live Level-I quote via `reqMktData`. Same structural
shape as `ChartService`: pending-subscribe pattern, one active req_id,
one `QuoteSnapshot` member.

### Public API

```cpp
void Request(const std::string& symbol,
              ChartInstrument kind = ChartInstrument::kStock);
void Cancel();
QuoteSnapshot Snapshot() const;
```

### Behavior

- `Request("AAPL")` allocates a req_id, builds a `Contract` via the
  shared helpers in `services/contracts.h` (`MakeStockContract` /
  `MakeContinuousFutureContract` / `MakeLocalFutureContract`), and
  fires `reqMktData(reqId, contract, "", false, false, …)`.
- `tickPrice` callbacks update `bid` / `ask` / `last` / `high` / `low`
  / `open` / `close`. The first non-sentinel `LAST` tick flips
  `snap_.ready = true`.
- `tickSize` callbacks update `bid_size` / `ask_size` / `last_size` /
  `volume`. `Decimal` → `double` → `long long`.
- `tickString` for `LAST_TIMESTAMP` (tick type 45) parses an
  epoch-seconds string into `snap_.last_ts`.
- **Sentinel handling**: TWS sends `-1.0` (price) or `-1` (size) when
  a value is unavailable. The service drops these — a previously-good
  field is never clobbered. Non-finite (NaN/inf) prices are also
  dropped.
- `Cancel()` issues `cancelMktData(req_id)` and clears state.
- Wrong-`req_id` callbacks are ignored (`if (req_id != req_id_) return`)
  — same pattern as `ChartService`.

### Tick-type constants

The TWS tick-type integers we map (file-scope `constexpr int` in
`quote_service.cc`):

| Constant | Value | Callback | Snapshot field |
|---|---|---|---|
| `kTickBidSize`       | 0  | `tickSize` | `bid_size` |
| `kTickBid`           | 1  | `tickPrice` | `bid` |
| `kTickAsk`           | 2  | `tickPrice` | `ask` |
| `kTickAskSize`       | 3  | `tickSize` | `ask_size` |
| `kTickLast`          | 4  | `tickPrice` | `last`, `ready = true` |
| `kTickLastSize`      | 5  | `tickSize` | `last_size` |
| `kTickHigh`          | 6  | `tickPrice` | `high` |
| `kTickLow`           | 7  | `tickPrice` | `low` |
| `kTickVolume`        | 8  | `tickSize` | `volume` |
| `kTickClose`         | 9  | `tickPrice` | `close` (previous close) |
| `kTickOpen`          | 14 | `tickPrice` | `open` |
| `kTickLastTimestamp` | 45 | `tickString` | `last_ts` |

Full enum lives in `EWrapper.h::enum TickType`.

## Adding a new service

Concrete checklist for a new TWS feature (e.g. "real-time bars"):

1. **Create the service.** New files `src/services/realtime_bars_service.{h,cc}`.
   - Constructor takes `ServiceContext& ctx`.
   - Public API: `Subscribe`, `Cancel`, `Snapshot` (or `Drain*`).
   - Lifecycle: `OnReady()`, `OnReset()`.
   - `OwnsReqId(int)` if your req-ids might be queried by `OnError`.
   - EWrapper hooks: `OnRealTimeBar(...)` etc.
   - **Don't lock anything.** Caller holds the lock.

2. **Wire the dispatcher.** In `src/ibkr_client.cc::Dispatcher`:
   - Add a member: `RealTimeBarsService rt_bars_{ctx_};`
   - Add EWrapper overrides that lock + forward. Example:
     ```cpp
     void realtimeBar(int reqId, long time, double open, ...) override {
       std::lock_guard<std::mutex> lock(mu_);
       rt_bars_.OnRealTimeBar(reqId, time, open, ...);
     }
     ```
   - Add to `OnDisconnect` and `nextValidId`'s `OnReady` broadcast.
   - Add public facade methods that lock + forward.

3. **Add facade in `IbkrClient`.** In `src/ibkr_client.h`, expose the
   public methods the screen will call (`SubscribeRTBars`, etc.). In
   `src/ibkr_client.cc`, implement as 2-line forwards to the
   dispatcher.

4. **Register the source file.** `CMakeLists.txt` gets
   `src/services/realtime_bars_service.cc`.

5. **Build green; smoketest by hand.** The dispatcher pattern is
   designed to make this incremental: if you forget to wire one
   callback, only that feature breaks, not the others.

6. **Update this doc.** Add a section to the service catalog above.

## Threading model recap

- Reader thread (owned by TWS API library) calls EWrapper overrides
  on the dispatcher.
- Main thread calls facade methods (`SubscribeNews`, `DrainHeadlines`,
  etc.).
- Both code paths take `Dispatcher::mu_`.
- Services never lock; they assume the caller holds it.
- Therefore: any `ctx.client->reqXxx(...)` call from inside a service
  method is fine — the IB API client is internally thread-safe and
  the request methods are non-blocking (they enqueue to the writer
  thread).

## Common pitfalls

- **Don't call back into the dispatcher from a service.** Services
  receive callbacks via their `On*` methods; they don't push events
  to the dispatcher.
- **Don't share `ServiceContext` across `IbkrClient` instances.**
  There's exactly one per connection.
- **Don't trust IB's numeric callbacks blindly.** P&L fields can be
  `DBL_MAX` for "not computed yet". Use `SafeNumeric` (defined in
  `portfolio_service.cc`'s anonymous namespace) before storing.
- **Don't keep a service's req-id around past a `Cancel`.** Both
  `Cancel` and `OnReset` zero them; subsequent callbacks for stale
  IDs are ignored by the `OwnsReqId`/`if (req_id != req_id_)`
  checks.
