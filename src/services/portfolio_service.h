#ifndef DLTERM_SRC_SERVICES_PORTFOLIO_SERVICE_H_
#define DLTERM_SRC_SERVICES_PORTFOLIO_SERVICE_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Contract.h"
#include "Decimal.h"
#include "ibkr_client.h"
#include "services/service_context.h"

namespace dlterm {

// Real-time multi-account portfolio data. Owns all subscription
// bookkeeping for reqAccountUpdatesMulti / reqPositionsMulti / reqPnL
// / reqPnLSingle. Called by the dispatcher under the shared mutex.
class PortfolioService {
 public:
  explicit PortfolioService(ServiceContext& ctx) : ctx_(ctx) {}

  // Public (dispatcher holds the lock).
  void Subscribe();
  void Unsubscribe();
  PortfolioSnapshot Snapshot() const;

  // Lifecycle.
  void OnReady();                                      // nextValidId
  void OnManagedAccounts(const std::string& list);     // capture list
  void OnReset();                                      // disconnect

  // True if any of this service's request ids match.
  bool OwnsReqId(int req_id) const;

  // EWrapper hooks.
  void OnAccountUpdateMulti(int req_id, const std::string& account,
                             const std::string& key, const std::string& value);
  void OnAccountUpdateMultiEnd(int req_id);
  void OnPositionMulti(int req_id, const std::string& account,
                        const Contract& contract, Decimal pos, double avg_cost);
  void OnPositionMultiEnd(int req_id);
  void OnPnL(int req_id, double daily, double unrealized, double realized);
  void OnPnLSingle(int req_id, Decimal pos, double daily, double unrealized,
                    double realized, double value);

 private:
  struct PositionRow {
    std::string symbol;
    std::string sec_type;
    std::string currency;
    double position = 0;
    double avg_cost = 0;
    double market_value = 0;
    double daily_pnl = 0;
    double unrealized_pnl = 0;
    double realized_pnl = 0;
  };

  void Start();

  ServiceContext& ctx_;
  std::vector<std::string> managed_accounts_;
  bool active_ = false;
  bool pending_subscribe_ = false;

  std::map<int, std::string> acct_update_ids_;
  std::map<int, std::string> pos_multi_ids_;
  std::map<int, std::string> pnl_ids_;
  std::map<int, std::pair<std::string, int>> pnl_single_ids_;

  std::map<std::pair<std::string, int>, PositionRow> positions_;
  std::map<std::string, AccountTotals> account_totals_;
  std::set<std::string> account_updates_done_;
  std::set<std::string> positions_done_;
};

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_PORTFOLIO_SERVICE_H_
