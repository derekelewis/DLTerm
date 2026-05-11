#ifndef DLTERM_SRC_SERVICES_CHART_SERVICE_H_
#define DLTERM_SRC_SERVICES_CHART_SERVICE_H_

#include <string>
#include <vector>

#include "ibkr_client.h"
#include "services/service_context.h"

struct Bar;

namespace dlterm {

// Manages a single live historical-bar subscription
// (reqHistoricalData with keepUpToDate=true). All public methods are
// called by the dispatcher while holding the shared mutex; the
// service itself does not lock.
class ChartService {
 public:
  explicit ChartService(ServiceContext& ctx) : ctx_(ctx) {}

  // Public-facing operations (dispatcher holds the lock).
  void Request(const std::string& symbol,
                ChartInstrument kind = ChartInstrument::kStock);
  // Re-issue the same historicalData request to pick up new bars
  // without flicker — bars stay on display until the new batch's
  // historicalDataEnd swaps them in. Used for CONTFUT, which does
  // not support keepUpToDate=true.
  void Refresh();
  void Cancel();
  ChartSnapshot Snapshot() const;

  // Lifecycle.
  void OnReady();  // nextValidId arrived
  void OnReset();  // disconnect

  // Returns true if this service owns the given request id.
  bool OwnsReqId(int req_id) const { return req_id == req_id_; }

  // EWrapper hooks (forwarded by the dispatcher).
  void OnHistoricalData(int req_id, const Bar& bar);
  void OnHistoricalDataEnd(int req_id);
  void OnHistoricalDataUpdate(int req_id, const Bar& bar);

 private:
  ServiceContext& ctx_;
  int req_id_ = 0;
  std::string symbol_;
  ChartInstrument kind_ = ChartInstrument::kStock;
  bool ready_ = false;
  std::vector<ChartBar> bars_;
  // Holding buffer used during a Refresh — the new batch builds up
  // here and atomically replaces `bars_` on historicalDataEnd.
  std::vector<ChartBar> incoming_bars_;
  bool replacing_ = false;
  bool pending_subscribe_ = false;
  std::string pending_symbol_;
  ChartInstrument pending_kind_ = ChartInstrument::kStock;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_CHART_SERVICE_H_
