# TWS API notes

Hard-won knowledge about Interactive Brokers' C++ TWS API. Things that
either aren't in the docs, are mis-documented, or that we learned by
trial-and-error during integration.

Refer to this BEFORE adding a new TWS feature. The patterns here are
load-bearing across the services layer.

## Debugging TWS interactions

The dispatcher (`src/ibkr_client.cc`) emits a `TRACE` log for every
EWrapper inbound callback under category `tws.dispatch`, plus a
`DEBUG` log for every outbound facade call under `tws`. Enable with:

```bash
DLTERM_LOG=info,tws=debug,tws.dispatch=trace ./build/dlterm 2>&1 | tee run.log
DLTERM_LOG_FILE=run.log ./build/dlterm        # (alternative) file-only sink
```

Or open the `DEBUG` overlay screen in-app (`DEBUG` command) to view
the same lines in the body without leaving the terminal. See
`docs/logging.md` for the filter syntax.

## Connection sequence

```
Connect(host, port, client_id)
  → EClientSocket::eConnect(host, port, client_id)
    → handshake; server sends managedAccounts(...) almost immediately
  → EReader reader(client, &signal); reader.start()  // background thread
  → main thread loops calling reader.processMsgs() each frame
```

`nextValidId(int)` is the **canonical "ready" signal.** It arrives
asynchronously after eConnect returns. Anything that needs a request
id must wait for it.

### The `nextValidId` race pattern

A subscription kicked off the same frame as `Connect` will fire before
`nextValidId` returns — which means the gateway hasn't told us a valid
id yet, and the call dies. We solve this in every service:

```cpp
void StartTickerSession(symbol) {
  if (!ready_) {              // nextValidId hasn't fired
    pending_symbol_ = symbol;
    pending_subscribe_ = true;
    return;
  }
  Fire(symbol);
}

void OnReady() {              // dispatcher calls this from nextValidId
  ready_ = true;
  if (pending_subscribe_) {
    pending_subscribe_ = false;
    Fire(pending_symbol_);
  }
}
```

Use this for any new service that needs a `reqId`.

### Eager calls slam the socket

Calling certain methods (e.g. `reqNewsProviders`) **before** the
handshake completes causes the gateway to close the socket without
an error message. Symptom: err 509 "exception while reading socket"
in the dispatcher log.

Fix: queue everything and replay from `OnReady`. Don't call any
`reqXxx` from `OnConnect`-equivalent code.

## EWrapper / EClient split

- **EClient** is the call interface: you call it; the writer thread
  serializes onto the socket.
- **EWrapper** is the callback interface: TWS calls back into your
  methods from the EReader thread.

`EClientSocket` is the concrete `EClient` we use. It is internally
thread-safe; calling `reqXxx(...)` while holding our dispatcher mutex
is fine.

## Request IDs

Every reqXxx that has callbacks takes an `int reqId` you allocate.
Use it to route callbacks back to the right state machine. We share a
single monotonic counter across services (`ServiceContext::next_req_id`,
starts at 1000, `++` per allocation).

`nextValidId(int)` gives an initial value from the gateway; we set
`next_req_id = max(next_req_id, value)` from it.

### `OwnsReqId(int)` pattern

The `error(int reqId, ...)` callback doesn't tell you which subsystem
the error belongs to. The dispatcher asks each service `bool
OwnsReqId(int)`. The first one that returns true gets the error.
We currently use this only for `NewsService` to silence
`error 200 "No security definition has been found"` on broadtape
probes for sources the account isn't entitled to.

## The shared mutex

`Dispatcher::mu_` guards every facade call AND every EWrapper
callback. Services never lock themselves. See `docs/architecture.md`
for the rationale.

## News API

### `reqHistoricalNews`

Returns a batch of recent headlines. Args we use:
`(reqId, conId, providerCodes, startDateTime="", endDateTime="",
totalResults=50, NoOpts())`. The `providerCodes` is a `+`-joined
string like `"BRFG+BRFUPDN+DJNL"`. Fallback to that if
`reqNewsProviders` hasn't returned yet.

### `reqMktData` with `mdoff,292:<providers>` for live news

To stream live tick news on a ticker:
```cpp
client_->reqMktData(streamId, contract, "mdoff,292:BRFG+BRFUPDN+DJNL",
                    false, false, TagValueListSPtr());
```

`mdoff,292` is the documented tick type for "news ticks only, no price
ticks". The `:` and the provider list are required — `mdoff,292`
alone is invalid for STK contracts and the gateway silently drops
your subscription.

**Subscription requirement (gotcha):** the underlying STK contract
needs basic Top Market Data entitlement. Without it you get
`error 10089 "Requested market data requires additional subscription
for API"`. For unfunded paper accounts, the "US Equity and Options
Add-On Streaming Bundle (NP)" at ~$4.50/mo is the cheapest path.

### BroadTape (general news)

`secType="NEWS"`, `symbol="<SRC>:<SRC>_ALL"`, `exchange=<SRC>`,
`currency=""`. Known valid sources (per gateway as of 2026-05):
`BRFG`, `BRFUPDN`, `DJ`, `DJNL`, `BZ`, `DJTOP`, `FLY`. Probe via
`reqContractDetails` first; ones the account doesn't have return
`error 200`. NewsService probes all 7 and subscribes to whichever
resolve.

Subscribe via `reqMktData(reqId, broadtape_contract, "mdoff,292", ...)`
— no provider list needed for BroadTape sources.

### `reqNewsArticle`

Single-shot fetch. `articleType=0` is supposed to mean plain-text but
TWS frequently returns HTML in plain-text articles too. **Always run
the body through `StripHtml`** regardless of the `articleType` field.
(See `src/news_screen.cc::StripHtml`.)

### Headline dedupe

Live subscriptions replay the last few headlines. Always dedupe by
`articleId` before showing. The NewsService keeps a
`std::set<std::string> seen_article_ids_` and checks on every
`OnHistoricalNews` / `OnTickNews`.

### Headline metadata strip

Headlines can come with a metadata prefix like
`{A:0.5:K:bus:T:eqty:V:Bzeq=us}`. Strip it for display. See
`src/news_screen.cc::CleanHeadline`.

## Portfolio API

### Multi-account vs single-account

`reqAccountUpdates(true, "")` is the legacy single-account stream. For
a *Financial Advisor* or *paper trading multi-account* setup, use:

- `reqAccountUpdatesMulti(reqId, account, "", false)` — per-account.
- `reqPositionsMulti(reqId, account, "")` — per-account positions.
- `reqPnL(reqId, account, "")` — account-rollup live P&L.
- `reqPnLSingle(reqId, account, "", conId)` — per-position live P&L.

The `managedAccounts(...)` callback (fires automatically) gives you
the account list to iterate.

### `ledgerAndNLV` parameter — IMPORTANT

`reqAccountUpdatesMulti(..., ledgerAndNLV=false)` — keep this **false**.
Setting it `true` returns *only* `NetLiquidation` (one value) and
nothing else. We hit this; spent time confused why Buying Power was
blank.

### Sentinel values

P&L fields can come back as `DBL_MAX` (or near-infinity) meaning
"not computed yet". Filter with:

```cpp
double SafeNumeric(double v) {
  if (!std::isfinite(v) || std::abs(v) > 1e15) return 0.0;
  return v;
}
```

(See `src/services/portfolio_service.cc` anonymous namespace.)

### `pnlSingle` lazy subscription

`reqPositionsMulti` gives you positions but not live per-position
P&L. To get live P&L for each position, fire `reqPnLSingle` per
conId as positions arrive. The PortfolioService does this in
`OnPositionMulti`.

## Chart API

### `reqHistoricalData(... keepUpToDate=true)`

```cpp
client_->reqHistoricalData(reqId, contract,
    /* endDateTime */ "",
    /* durationStr */ "1 D",
    /* barSizeSetting */ "5 mins",
    /* whatToShow */ "TRADES",
    /* useRTH */ 1,
    /* formatDate */ 2,       // epoch-second timestamps
    /* keepUpToDate */ true,
    TagValueListSPtr());
```

**Constraint:** `keepUpToDate=true` requires `endDateTime=""`. Pass
any timestamp there and the gateway returns an error.

Callbacks:
- `historicalData(reqId, Bar)` — replay of initial bars.
- `historicalDataEnd(reqId, start, end)` — initial replay done.
- `historicalDataUpdate(reqId, Bar)` — live updates after end. Either
  the same `time` (refreshing the current bar) or a brand-new
  timestamp (a new bar started).

Always cancel with `cancelHistoricalData(reqId)` when switching screens.

## Quote API

### `reqMktData` for live Level-I quotes

```cpp
client_->reqMktData(reqId, contract, /*genericTicks=*/"",
                     /*snapshot=*/false,
                     /*regulatorySnapshot=*/false,
                     TagValueListSPtr());
```

Empty `genericTicks` is fine — TWS returns the standard set (bid/ask/
last/sizes/high/low/open/close/volume/last_timestamp). Pass extra
codes like `"233"` for RTVolume if you need them.

Callbacks come back fanned out across three EWrapper methods:
- `tickPrice(reqId, TickType field, double price, const TickAttrib&)`
  — `field` ∈ {BID=1, ASK=2, LAST=4, HIGH=6, LOW=7, OPEN=14, CLOSE=9, …}.
- `tickSize(reqId, TickType field, Decimal size)` — sizes are Decimal;
  convert via `DecimalFunctions::decimalToDouble`. `field` ∈
  {BID_SIZE=0, ASK_SIZE=3, LAST_SIZE=5, VOLUME=8, …}.
- `tickString(reqId, TickType field, const std::string& value)` —
  used for `LAST_TIMESTAMP=45` (epoch-seconds in a string).

Full `TickType` enum is in `EWrapper.h`.

**Sentinels:** TWS sends `-1.0` for prices and `-1` for sizes when a
value is unavailable (e.g. preopen / nothing traded yet). Both
`DBL_MAX` and `-1` can show up depending on the field. The defensive
pattern in `QuoteService` is "only update on `isfinite(price) && price
>= 0`" for prices and `size >= 0` for sizes — a previously-good field
is never clobbered.

**Reqd entitlement:** delayed data needs no subscription but realtime
LAST/BID/ASK on equities requires basic Top Market Data. Without it
you'll get `error 10089` and only the previous day's `CLOSE` will
flow. Same constraint as the news live-tail. See `news` section above
for the cheapest paper-account bundle.

Cancel with `cancelMktData(reqId)` on screen exit.

### Continuous futures (`CONTFUT`) are historical-only

```cpp
Contract c;
c.symbol = "CL";
c.secType = "CONTFUT";
c.exchange = "NYMEX";     // futures want the trading exchange, not SMART
c.currency = "USD";
client_->reqHistoricalData(reqId, c, "", "1 D", "5 mins", "TRADES",
                            /*useRTH=*/1, /*formatDate=*/2,
                            /*keepUpToDate=*/false,  // REQUIRED
                            TagValueListSPtr());
```

Per the TWS docs, continuous futures load the latest contract's
history and are **intended for backtesting only**. They do **not**
support `keepUpToDate=true` — pass `false` or the gateway returns an
error. `ChartService` flips `keepUpToDate` based on
`ChartInstrument`.

To get live-ish updates anyway, `ChartScreen` polls
`client->RefreshChart()` every 30s when the instrument is CONTFUT.
`ChartService::Refresh()` re-issues the historical request under a
fresh req_id and **accumulates new bars in a holding buffer**
(`incoming_bars_`); only on `historicalDataEnd` are they swapped
into the displayed `bars_`. This avoids the empty-then-refilled
flicker you'd get from a naive cancel + re-request.

The exchange is mandatory and contract-specific: `CL/NG/HO/RB`
→ `NYMEX`, `GC/SI/HG/PL/PA` → `COMEX`, `ES/NQ/RTY/YM` → `CME`,
`ZN/ZF/ZT/ZB/ZC/ZW/ZS/ZL/ZM` → `CBOT`. The table lives in
`src/futures.cc`; extend it when you need a new root.

**`useRTH=0` for futures.** Futures live on Globex (CL: Sun 18:00 ET
through Fri 17:00 ET with a 1h daily break). Passing `useRTH=1`
restricts results to the pit session (e.g. CL pit is 9:00–14:30 ET);
on a Sunday evening or in pre-market hours the API returns the
*previous Friday's* RTH bars and the chart looks frozen. For CONTFUT
and FUT we pass `useRTH=0`; stocks keep `useRTH=1` (default NYSE
9:30–16:00).

### Specific-expiry futures by `localSymbol` (live tail)

```cpp
Contract c;
c.localSymbol = "CLM6";   // CL June 2026
c.secType = "FUT";
c.exchange = "NYMEX";
c.currency = "USD";
client_->reqHistoricalData(reqId, c, "", "1 D", "5 mins", "TRADES",
                            /*useRTH=*/0, /*formatDate=*/2,
                            /*keepUpToDate=*/true,  // works on FUT
                            TagValueListSPtr());
```

Unlike CONTFUT, a specific FUT contract **does** support
`keepUpToDate=true` — the server pushes `historicalDataUpdate`
callbacks as new ticks land. No polling needed. The parser
auto-detects `<ROOT><MONTH><YEAR>` shaped tokens like `CLM6` /
`ESM26` and routes `<SYM> CHART` through this path
(`ChartInstrument::kFutureLocal`); root resolution reuses the
`ExchangeForContinuousFuture` table.

Month codes (IB convention): F=Jan G=Feb H=Mar J=Apr K=May M=Jun
N=Jul Q=Aug U=Sep V=Oct X=Nov Z=Dec.

### Bar time format

`formatDate=2` → `Bar::time` is a string of epoch seconds. Parse with
`std::stoll`. `formatDate=1` is `"yyyyMMdd  HH:mm:ss"` (note: two
spaces). We use 2 everywhere; it's machine-friendlier.

### Exchange field for news vs market data

When `reqContractDetails` returns a NEWS contract, the resulting
`exchange` is `"NEWS"`. **Do not** use that as the exchange when
subscribing to data — `reqMktData` wants the source code (`"BRFG"`,
etc.). Parse the source from the symbol prefix instead.

## Error catalog

Errors we've seen and what they mean. The dispatcher routes these via
`error(reqId, errorCode, errorString, advancedOrderRejectJson)`.

| Code | Meaning | What to do |
|---|---|---|
| 200 | "No security definition has been found" | Broadtape probe for a source the account isn't entitled to. Silence in `NewsService::OnError`. |
| 321 | "Error validating request: Specified provider doesn't exist or is not subscribed" | The `providerCodes` arg includes a provider the account isn't entitled to. Use only providers from `newsProviders` callback. |
| 322 | "Error processing request: Duplicate ticker id" | Reused a reqId before canceling the previous request. Allocate fresh ids per `NextReqId()`. |
| 509 | "Exception while reading socket" | Gateway closed the connection. Usually means an eager call hit before handshake completed (see `nextValidId` race). |
| 10089 | "Requested market data requires additional subscription for API" | Live ticker news (`mdoff,292:PROVIDER`) on STK requires basic Top Market Data. Add streaming bundle. |
| 2104, 2106, 2158 | "Market data farm connection is OK" | Informational. Dispatcher emits these as status messages. |

## Useful EClient methods (reference card)

| Method | Notes |
|---|---|
| `reqContractDetails(reqId, contract)` | Resolves conId, exchange, currency, etc. Single answer + `contractDetailsEnd`. |
| `reqMktData(tickerId, contract, genericTicks, snapshot, regulatorySnapshot, mktDataOptions)` | Live ticks. `genericTicks="mdoff,292:..."` for news. |
| `cancelMktData(tickerId)` | Stops a stream. |
| `reqHistoricalNews(reqId, conId, providerCodes, start, end, total, opts)` | Single batch. |
| `reqNewsArticle(reqId, provider, articleId, opts)` | Single article body. |
| `reqHistoricalData(...)` | See chart section above. |
| `cancelHistoricalData(reqId)` | Required for clean cancellation. |
| `reqAccountUpdatesMulti(reqId, account, modelCode, ledgerAndNLV)` | Multi-account totals. **Pass `ledgerAndNLV=false`.** |
| `cancelAccountUpdatesMulti(reqId)` | |
| `reqPositionsMulti(reqId, account, modelCode)` | Multi-account positions. |
| `cancelPositionsMulti(reqId)` | |
| `reqPnL(reqId, account, modelCode)` | Account-rollup live P&L. |
| `cancelPnL(reqId)` | |
| `reqPnLSingle(reqId, account, modelCode, conId)` | Per-position live P&L. |
| `cancelPnLSingle(reqId)` | |
| `reqNewsProviders()` | No reqId. Callback `newsProviders(providers)` returns the list. |

## Why we don't use `signal.waitForSignal()`

The reference example pumps the EReader thread by calling
`signal.waitForSignal()` (blocking, up to ~2 seconds) before
`processMsgs()`. This freezes the UI when nothing is streaming.

We just call `reader_->processMsgs()` directly each frame
(`IbkrClient::Pump`). The EReader thread queues messages whenever they
arrive; `processMsgs` drains the queue non-blockingly on the main
thread. ~1ms per frame when idle. See `src/ibkr_client.cc`.

## Why the dispatcher's mutex doesn't deadlock

Both the main thread and the EReader thread take `mu_`. The callbacks
on the EReader thread never call back into the main thread
synchronously — they update state which the main thread polls each
frame via `DrainXxx()` / `SnapshotXxx()`. So the lock holds are
short and one-way.

## Where TWS API headers live

The bundled API lives at:
```
/Users/dlewis/Downloads/twsapi_macunix/IBJts/source/cppclient/client
```

Useful headers:
- `EClient.h` — call interface (search for the method you want).
- `EWrapper.h` and `EWrapper_prototypes.h` — callback interface.
- `bar.h` — `struct Bar`.
- `Contract.h` — `struct Contract`, `struct ContractDetails`.

Set the path via `-DDLTERM_TWSAPI_DIR=...` to CMake if you move it.

## Testing without the gateway

The build also produces `tws_smoketest` (see `src/tws_smoketest.cc`),
a stripped-down connect + status diagnostic that doesn't open an SDL
window. Use it to verify that:

- `eConnect` succeeds against the configured host/port.
- `managedAccounts` fires (proves handshake completed).
- `nextValidId` fires (proves the server-side init is done).
- Basic `reqNewsProviders` / `reqAccountUpdatesMulti` return data.

Run with the same env vars as the main app
(`DLTERM_TWS_HOST/PORT/CLIENT_ID`).
