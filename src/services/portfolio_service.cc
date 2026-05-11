#include "services/portfolio_service.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "EClient.h"

namespace dlterm {

namespace {

constexpr const char* kAccountValueKeys[] = {
    "NetLiquidation",  "BuyingPower",   "AvailableFunds",
    "TotalCashValue",  "UnrealizedPnL", "RealizedPnL",
};

bool IsWhitelistedAccountKey(const std::string& key) {
  for (const char* k : kAccountValueKeys) {
    if (key == k) return true;
  }
  return false;
}

std::vector<std::string> SplitCommaList(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ',') {
      if (i > start) out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

double SafeNumeric(double v) {
  if (!std::isfinite(v)) return 0.0;
  if (std::abs(v) > 1e15) return 0.0;
  return v;
}

}  // namespace

void PortfolioService::Subscribe() {
  if (active_) return;
  active_ = true;
  if (managed_accounts_.empty()) {
    pending_subscribe_ = true;
    return;
  }
  Start();
}

void PortfolioService::Unsubscribe() {
  if (!active_) return;
  std::vector<int> acct_ids, pos_ids, pnl_ids, pnl_single_ids_local;
  for (auto& [id, _] : acct_update_ids_) acct_ids.push_back(id);
  for (auto& [id, _] : pos_multi_ids_) pos_ids.push_back(id);
  for (auto& [id, _] : pnl_ids_) pnl_ids.push_back(id);
  for (auto& [id, _] : pnl_single_ids_) pnl_single_ids_local.push_back(id);
  acct_update_ids_.clear();
  pos_multi_ids_.clear();
  pnl_ids_.clear();
  pnl_single_ids_.clear();
  positions_.clear();
  account_totals_.clear();
  account_updates_done_.clear();
  positions_done_.clear();
  active_ = false;
  pending_subscribe_ = false;
  if (!ctx_.client) return;
  for (int id : acct_ids) ctx_.client->cancelAccountUpdatesMulti(id);
  for (int id : pos_ids) ctx_.client->cancelPositionsMulti(id);
  for (int id : pnl_ids) ctx_.client->cancelPnL(id);
  for (int id : pnl_single_ids_local) ctx_.client->cancelPnLSingle(id);
}

void PortfolioService::OnReady() {
  if (active_ && pending_subscribe_ && !managed_accounts_.empty()) Start();
}

void PortfolioService::OnManagedAccounts(const std::string& list) {
  managed_accounts_ = SplitCommaList(list);
  std::sort(managed_accounts_.begin(), managed_accounts_.end());
  if (active_ && pending_subscribe_) Start();
}

void PortfolioService::OnReset() {
  Unsubscribe();
  managed_accounts_.clear();
}

void PortfolioService::Start() {
  pending_subscribe_ = false;
  for (const auto& a : managed_accounts_) {
    const int aid = ctx_.NextReqId();
    acct_update_ids_[aid] = a;
    const int pid = ctx_.NextReqId();
    pos_multi_ids_[pid] = a;
    const int lid = ctx_.NextReqId();
    pnl_ids_[lid] = a;
    if (!ctx_.client) continue;
    ctx_.client->reqAccountUpdatesMulti(aid, a, "", false);
    ctx_.client->reqPositionsMulti(pid, a, "");
    ctx_.client->reqPnL(lid, a, "");
  }
}

bool PortfolioService::OwnsReqId(int req_id) const {
  return acct_update_ids_.contains(req_id) ||
         pos_multi_ids_.contains(req_id) ||
         pnl_ids_.contains(req_id) ||
         pnl_single_ids_.contains(req_id);
}

void PortfolioService::OnAccountUpdateMulti(int /*req_id*/,
                                            const std::string& account,
                                            const std::string& key,
                                            const std::string& value) {
  if (!IsWhitelistedAccountKey(key)) return;
  auto& tot = account_totals_[account];
  tot.account = account;
  tot.values[key] = value;
}

void PortfolioService::OnAccountUpdateMultiEnd(int req_id) {
  auto it = acct_update_ids_.find(req_id);
  if (it == acct_update_ids_.end()) return;
  account_updates_done_.insert(it->second);
}

void PortfolioService::OnPositionMulti(int /*req_id*/,
                                        const std::string& account,
                                        const Contract& contract,
                                        Decimal pos, double avg_cost) {
  const double pos_d = DecimalFunctions::decimalToDouble(pos);
  std::pair<std::string, int> key{account, contract.conId};
  if (pos_d == 0.0) {
    positions_.erase(key);
    return;
  }
  auto [it, inserted] = positions_.try_emplace(key);
  auto& p = it->second;
  p.symbol = contract.symbol;
  p.sec_type = contract.secType;
  p.currency = contract.currency;
  p.position = pos_d;
  p.avg_cost = avg_cost;
  if (inserted) {
    const int sid = ctx_.NextReqId();
    pnl_single_ids_[sid] = key;
    if (ctx_.client) ctx_.client->reqPnLSingle(sid, account, "", contract.conId);
  }
}

void PortfolioService::OnPositionMultiEnd(int req_id) {
  auto it = pos_multi_ids_.find(req_id);
  if (it == pos_multi_ids_.end()) return;
  positions_done_.insert(it->second);
}

void PortfolioService::OnPnL(int req_id, double daily, double unrealized,
                              double realized) {
  auto it = pnl_ids_.find(req_id);
  if (it == pnl_ids_.end()) return;
  auto& tot = account_totals_[it->second];
  tot.account = it->second;
  tot.values["RT_DailyPnL"] = std::to_string(SafeNumeric(daily));
  tot.values["RT_UnrealizedPnL"] = std::to_string(SafeNumeric(unrealized));
  tot.values["RT_RealizedPnL"] = std::to_string(SafeNumeric(realized));
}

void PortfolioService::OnPnLSingle(int req_id, Decimal pos, double daily,
                                    double unrealized, double realized,
                                    double value) {
  auto it = pnl_single_ids_.find(req_id);
  if (it == pnl_single_ids_.end()) return;
  auto pos_it = positions_.find(it->second);
  if (pos_it == positions_.end()) return;
  auto& p = pos_it->second;
  p.position = DecimalFunctions::decimalToDouble(pos);
  p.market_value = SafeNumeric(value);
  p.daily_pnl = SafeNumeric(daily);
  p.unrealized_pnl = SafeNumeric(unrealized);
  p.realized_pnl = SafeNumeric(realized);
}

PortfolioSnapshot PortfolioService::Snapshot() const {
  PortfolioSnapshot snap;
  snap.accounts = managed_accounts_;
  bool all_ready = !managed_accounts_.empty();
  for (const auto& acct : managed_accounts_) {
    AccountTotals t;
    t.account = acct;
    auto it = account_totals_.find(acct);
    if (it != account_totals_.end()) t.values = it->second.values;
    snap.totals.push_back(std::move(t));
    if (!account_updates_done_.contains(acct) ||
        !positions_done_.contains(acct)) {
      all_ready = false;
    }
  }
  snap.ready = all_ready;

  struct Agg {
    std::string symbol;
    std::string sec_type;
    std::string currency;
    double position = 0;
    double weighted_cost = 0;
    double cost_basis_pos = 0;
    double market_value = 0;
    double daily_pnl = 0;
    double unrealized_pnl = 0;
    double realized_pnl = 0;
    std::set<std::string> accts;
  };
  std::map<std::tuple<std::string, std::string, std::string>, Agg> agg;
  for (const auto& [key, row] : positions_) {
    const auto& acct = key.first;
    const std::tuple gkey{row.symbol, row.sec_type, row.currency};
    auto& a = agg[gkey];
    a.symbol = row.symbol;
    a.sec_type = row.sec_type;
    a.currency = row.currency;
    a.position += row.position;
    a.weighted_cost += row.position * row.avg_cost;
    a.cost_basis_pos += row.position;
    a.market_value += row.market_value;
    a.daily_pnl += row.daily_pnl;
    a.unrealized_pnl += row.unrealized_pnl;
    a.realized_pnl += row.realized_pnl;
    a.accts.insert(acct);
  }
  for (auto& [_, a] : agg) {
    if (a.position == 0) continue;
    PortfolioPosition p;
    p.symbol = a.symbol;
    p.sec_type = a.sec_type;
    p.currency = a.currency;
    p.position = a.position;
    p.avg_cost = a.cost_basis_pos != 0
                     ? a.weighted_cost / a.cost_basis_pos
                     : 0.0;
    p.market_value = a.market_value;
    p.daily_pnl = a.daily_pnl;
    p.unrealized_pnl = a.unrealized_pnl;
    p.realized_pnl = a.realized_pnl;
    bool first = true;
    for (const auto& acct : a.accts) {
      if (!first) p.accounts.push_back(',');
      p.accounts.append(acct);
      first = false;
    }
    snap.positions.push_back(std::move(p));
  }
  std::sort(snap.positions.begin(), snap.positions.end(),
            [](const PortfolioPosition& a, const PortfolioPosition& b) {
              return std::abs(a.market_value) > std::abs(b.market_value);
            });
  return snap;
}

}  // namespace dlterm
