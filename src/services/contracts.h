#ifndef DLTERM_SRC_SERVICES_CONTRACTS_H_
#define DLTERM_SRC_SERVICES_CONTRACTS_H_

#include <string>

#include "Contract.h"

namespace dlterm {

// Contract builders shared by the TWS services. Stocks go via SMART/USD;
// futures use the per-root exchange table in futures.{h,cc}.

inline Contract MakeStockContract(const std::string& symbol) {
  Contract c;
  c.symbol = symbol;
  c.secType = "STK";
  c.exchange = "SMART";
  c.currency = "USD";
  return c;
}

inline Contract MakeContinuousFutureContract(const std::string& symbol,
                                              const std::string& exchange) {
  Contract c;
  c.symbol = symbol;
  c.secType = "CONTFUT";
  c.exchange = exchange;
  c.currency = "USD";
  return c;
}

inline Contract MakeLocalFutureContract(const std::string& local_symbol,
                                         const std::string& exchange) {
  Contract c;
  c.localSymbol = local_symbol;  // e.g. "CLM6"
  c.secType = "FUT";
  c.exchange = exchange;
  c.currency = "USD";
  return c;
}

}  // namespace dlterm

#endif  // DLTERM_SRC_SERVICES_CONTRACTS_H_
