#include "services/quote_service.h"

#include <cmath>
#include <cstdlib>
#include <string>

#include "Contract.h"
#include "EClient.h"
#include "futures.h"
#include "log.h"
#include "services/contracts.h"

namespace dlterm {

namespace {

constexpr const char* kCatQuote = "tws.quote";

// TWS tick types (from TickTypes.h in the TWS API headers).
constexpr int kTickBidSize       = 0;
constexpr int kTickBid           = 1;
constexpr int kTickAsk           = 2;
constexpr int kTickAskSize       = 3;
constexpr int kTickLast          = 4;
constexpr int kTickLastSize      = 5;
constexpr int kTickHigh          = 6;
constexpr int kTickLow           = 7;
constexpr int kTickVolume        = 8;
constexpr int kTickClose         = 9;
constexpr int kTickOpen          = 14;
constexpr int kTickLastTimestamp = 45;

bool IsValidPrice(double p) { return std::isfinite(p) && p >= 0.0; }

}  // namespace

void QuoteService::Request(const std::string& symbol, ChartInstrument kind) {
  Cancel();
  snap_ = QuoteSnapshot{};
  snap_.symbol = symbol;
  snap_.kind = kind;
  req_id_ = ctx_.NextReqId();
  pending_subscribe_ = true;
  if (ctx_.client && req_id_ != 0) {
    // Fire immediately if nextValidId has already arrived; otherwise
    // queue until OnReady().
    OnReady();
  }
}

void QuoteService::Cancel() {
  if (req_id_ != 0 && ctx_.client) {
    ctx_.client->cancelMktData(req_id_);
  }
  req_id_ = 0;
  snap_ = QuoteSnapshot{};
  pending_subscribe_ = false;
}

void QuoteService::OnReady() {
  if (!pending_subscribe_ || req_id_ == 0 || !ctx_.client) return;
  Contract c;
  if (snap_.kind == ChartInstrument::kContinuousFuture) {
    const std::string exch = ExchangeForContinuousFuture(snap_.symbol);
    if (exch.empty()) {
      ctx_.EmitStatus("Unknown futures symbol: " + snap_.symbol);
      pending_subscribe_ = false;
      return;
    }
    c = MakeContinuousFutureContract(snap_.symbol, exch);
  } else if (snap_.kind == ChartInstrument::kFutureLocal) {
    const std::string_view root = RootFromFuturesLocalSymbol(snap_.symbol);
    const std::string exch = ExchangeForContinuousFuture(root);
    if (exch.empty()) {
      ctx_.EmitStatus("Unknown futures root: " + std::string(root));
      pending_subscribe_ = false;
      return;
    }
    c = MakeLocalFutureContract(snap_.symbol, exch);
  } else {
    c = MakeStockContract(snap_.symbol);
  }
  DLTERM_LOG_DEBUG(kCatQuote, "reqMktData reqId={} symbol={} kind={}", req_id_,
                    snap_.symbol, static_cast<int>(snap_.kind));
  ctx_.client->reqMktData(req_id_, c, /*genericTicks=*/"",
                           /*snapshot=*/false,
                           /*regulatorySnapshot=*/false,
                           TagValueListSPtr());
  pending_subscribe_ = false;
}

void QuoteService::OnReset() {
  req_id_ = 0;
  snap_ = QuoteSnapshot{};
  pending_subscribe_ = false;
}

void QuoteService::OnTickPrice(int req_id, int tick_type, double price) {
  if (req_id != req_id_) return;
  if (!IsValidPrice(price)) return;  // -1 / DBL_MAX = no data
  switch (tick_type) {
    case kTickBid:   snap_.bid = price; break;
    case kTickAsk:   snap_.ask = price; break;
    case kTickLast:  snap_.last = price; snap_.ready = true; break;
    case kTickHigh:  snap_.high = price; break;
    case kTickLow:   snap_.low = price; break;
    case kTickClose: snap_.close = price; break;
    case kTickOpen:  snap_.open = price; break;
    default: break;
  }
}

void QuoteService::OnTickSize(int req_id, int tick_type, long long size) {
  if (req_id != req_id_) return;
  if (size < 0) return;
  switch (tick_type) {
    case kTickBidSize:  snap_.bid_size = size; break;
    case kTickAskSize:  snap_.ask_size = size; break;
    case kTickLastSize: snap_.last_size = size; break;
    case kTickVolume:   snap_.volume = size; break;
    default: break;
  }
}

void QuoteService::OnTickString(int req_id, int tick_type,
                                 const std::string& value) {
  if (req_id != req_id_) return;
  if (tick_type != kTickLastTimestamp) return;
  if (value.empty()) return;
  // Value is an epoch-seconds string per TWS docs.
  const long long ts = std::strtoll(value.c_str(), nullptr, 10);
  if (ts <= 0) return;
  snap_.last_ts = static_cast<std::time_t>(ts);
}

}  // namespace dlterm
