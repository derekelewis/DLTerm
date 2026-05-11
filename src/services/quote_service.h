#ifndef DLTERM_SRC_SERVICES_QUOTE_SERVICE_H_
#define DLTERM_SRC_SERVICES_QUOTE_SERVICE_H_

#include <string>

#include "ibkr_client.h"
#include "services/service_context.h"

namespace dlterm {

// Manages a single live quote subscription (reqMktData) for one symbol.
// All public methods are called by the dispatcher while holding the
// shared mutex; the service itself does not lock.
class QuoteService {
 public:
  explicit QuoteService(ServiceContext& ctx) : ctx_(ctx) {}

  // Public-facing operations (dispatcher holds the lock).
  void Request(const std::string& symbol,
                ChartInstrument kind = ChartInstrument::kStock);
  void Cancel();
  QuoteSnapshot Snapshot() const { return snap_; }

  // Lifecycle.
  void OnReady();  // nextValidId arrived
  void OnReset();  // disconnect

  bool OwnsReqId(int req_id) const { return req_id != 0 && req_id == req_id_; }

  // EWrapper hooks (forwarded by the dispatcher).
  void OnTickPrice(int req_id, int tick_type, double price);
  void OnTickSize(int req_id, int tick_type, long long size);
  void OnTickString(int req_id, int tick_type, const std::string& value);

 private:
  ServiceContext& ctx_;
  int req_id_ = 0;
  QuoteSnapshot snap_;
  bool pending_subscribe_ = false;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_QUOTE_SERVICE_H_
