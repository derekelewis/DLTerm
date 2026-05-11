#ifndef DLTERM_SRC_FUTURES_H_
#define DLTERM_SRC_FUTURES_H_

#include <string>
#include <string_view>

namespace dlterm {

// Returns the IBKR exchange name for a continuous-futures root symbol
// (e.g. "CL" -> "NYMEX", "ES" -> "CME"). Returns empty if the symbol
// is not in the built-in table; callers should treat that as
// unsupported and surface an error.
std::string ExchangeForContinuousFuture(std::string_view symbol);

// True if `s` looks like an IB futures local symbol — alpha root,
// followed by a one-letter month code, followed by 1-2 digit year.
// Examples: "CLM6", "ESM26", "GCQ6". The validator is intentionally
// strict to avoid misclassifying odd ticker shapes.
bool IsFuturesLocalSymbol(std::string_view s);

// Extracts the root (alpha prefix) from a futures local symbol.
// Assumes the caller already verified via IsFuturesLocalSymbol;
// returns empty otherwise.
std::string_view RootFromFuturesLocalSymbol(std::string_view s);

}  // namespace dlterm

#endif  // DLTERM_SRC_FUTURES_H_
