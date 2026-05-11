#include "services/chart_service.h"

#include "Contract.h"
#include "Decimal.h"
#include "EClient.h"
#include "bar.h"
#include "futures.h"
#include "log.h"
#include "services/contracts.h"

namespace dlterm {

void ChartService::Request(const std::string& symbol, ChartInstrument kind) {
  Cancel();
  symbol_ = symbol;
  kind_ = kind;
  bars_.clear();
  incoming_bars_.clear();
  replacing_ = false;
  ready_ = false;
  req_id_ = ctx_.NextReqId();
  if (ctx_.client && req_id_ != 0) {
    // We have a client (post-eConnect). Fire immediately if nextValidId
    // has already arrived; otherwise queue until OnReady().
    pending_subscribe_ = true;
    pending_symbol_ = symbol;
    pending_kind_ = kind;
    OnReady();  // OnReady is idempotent; replays if already-ready.
  }
}

void ChartService::Refresh() {
  // No active chart to refresh.
  if (symbol_.empty()) return;
  // Don't stack refreshes — let the in-flight one finish first.
  if (replacing_) return;
  // Re-issue under a new req_id; keep displayed bars_ until the new
  // batch's historicalDataEnd swaps them in.
  if (req_id_ != 0 && ctx_.client) {
    ctx_.client->cancelHistoricalData(req_id_);
  }
  incoming_bars_.clear();
  replacing_ = true;
  req_id_ = ctx_.NextReqId();
  pending_subscribe_ = true;
  pending_symbol_ = symbol_;
  pending_kind_ = kind_;
  OnReady();  // No-op when ctx_.client is null.
}

void ChartService::Cancel() {
  if (req_id_ != 0 && ctx_.client) {
    ctx_.client->cancelHistoricalData(req_id_);
  }
  req_id_ = 0;
  symbol_.clear();
  kind_ = ChartInstrument::kStock;
  bars_.clear();
  incoming_bars_.clear();
  replacing_ = false;
  ready_ = false;
  pending_subscribe_ = false;
  pending_symbol_.clear();
  pending_kind_ = ChartInstrument::kStock;
}

ChartSnapshot ChartService::Snapshot() const {
  ChartSnapshot s;
  s.symbol = symbol_;
  s.kind = kind_;
  s.ready = ready_;
  s.bars = bars_;
  return s;
}

void ChartService::OnReady() {
  if (!pending_subscribe_ || req_id_ == 0 || !ctx_.client) return;
  Contract c;
  bool keep_up_to_date = true;
  // Stocks: RTH only (9:30–16:00 NYSE) is the sane default.
  // Futures (especially CL/ES) live in the Globex session that's
  // outside any "RTH" window most of the day, so use 0 to get the
  // full electronic-session series.
  int use_rth = 1;
  if (pending_kind_ == ChartInstrument::kContinuousFuture) {
    const std::string exch = ExchangeForContinuousFuture(pending_symbol_);
    if (exch.empty()) {
      ctx_.EmitStatus("Unknown futures symbol: " + pending_symbol_);
      pending_subscribe_ = false;
      pending_symbol_.clear();
      return;
    }
    c = MakeContinuousFutureContract(pending_symbol_, exch);
    // CONTFUT historical is "backtesting only" per TWS docs — live tail
    // updates aren't supported.
    keep_up_to_date = false;
    use_rth = 0;
  } else if (pending_kind_ == ChartInstrument::kFutureLocal) {
    // Specific expiry by local symbol (e.g. "CLM6"). Exchange comes
    // from the alpha root via the same table CONTFUT uses.
    const std::string_view root = RootFromFuturesLocalSymbol(pending_symbol_);
    const std::string exch = ExchangeForContinuousFuture(root);
    if (exch.empty()) {
      ctx_.EmitStatus("Unknown futures root: " + std::string(root));
      pending_subscribe_ = false;
      pending_symbol_.clear();
      return;
    }
    c = MakeLocalFutureContract(pending_symbol_, exch);
    // Regular FUT supports live tail via keepUpToDate=true. Use 0 RTH
    // to capture the full Globex session.
    keep_up_to_date = true;
    use_rth = 0;
  } else {
    c = MakeStockContract(pending_symbol_);
  }
  ctx_.client->reqHistoricalData(req_id_, c, "", "1 D", "5 mins", "TRADES",
                                  use_rth, 2, keep_up_to_date,
                                  TagValueListSPtr());
  pending_subscribe_ = false;
  pending_symbol_.clear();
}

void ChartService::OnReset() {
  req_id_ = 0;
  symbol_.clear();
  kind_ = ChartInstrument::kStock;
  bars_.clear();
  incoming_bars_.clear();
  replacing_ = false;
  ready_ = false;
  pending_subscribe_ = false;
  pending_symbol_.clear();
  pending_kind_ = ChartInstrument::kStock;
}

void ChartService::OnHistoricalData(int req_id, const Bar& bar) {
  if (req_id != req_id_) return;
  DLTERM_DCHECK(req_id_ != 0);
  ChartBar cb;
  cb.ts = static_cast<std::time_t>(std::stoll(bar.time));
  cb.open = bar.open;
  cb.high = bar.high;
  cb.low = bar.low;
  cb.close = bar.close;
  cb.volume = DecimalFunctions::decimalToDouble(bar.volume);
  if (replacing_) {
    incoming_bars_.push_back(cb);
  } else {
    bars_.push_back(cb);
  }
}

void ChartService::OnHistoricalDataEnd(int req_id) {
  if (req_id != req_id_) return;
  if (replacing_) {
    bars_ = std::move(incoming_bars_);
    incoming_bars_.clear();
    replacing_ = false;
  }
  ready_ = true;
  ctx_.EmitStatus("Chart loaded: " + std::to_string(bars_.size()) + " bars");
}

void ChartService::OnHistoricalDataUpdate(int req_id, const Bar& bar) {
  if (req_id != req_id_) return;
  const std::time_t ts = static_cast<std::time_t>(std::stoll(bar.time));
  if (!bars_.empty() && bars_.back().ts == ts) {
    auto& last = bars_.back();
    last.open = bar.open;
    last.high = bar.high;
    last.low = bar.low;
    last.close = bar.close;
    last.volume = DecimalFunctions::decimalToDouble(bar.volume);
    return;
  }
  ChartBar cb;
  cb.ts = ts;
  cb.open = bar.open;
  cb.high = bar.high;
  cb.low = bar.low;
  cb.close = bar.close;
  cb.volume = DecimalFunctions::decimalToDouble(bar.volume);
  bars_.push_back(cb);
}

}  // namespace dlterm
