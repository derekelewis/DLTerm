#include "futures.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace dlterm {

std::string ExchangeForContinuousFuture(std::string_view symbol) {
  // NYMEX — energy.
  if (symbol == "CL" || symbol == "NG" || symbol == "HO" || symbol == "RB") {
    return "NYMEX";
  }
  // COMEX — metals.
  if (symbol == "GC" || symbol == "SI" || symbol == "HG" || symbol == "PL" ||
      symbol == "PA") {
    return "COMEX";
  }
  // CME — equity index + FX.
  if (symbol == "ES" || symbol == "NQ" || symbol == "RTY" || symbol == "YM" ||
      symbol == "MES" || symbol == "MNQ") {
    return "CME";
  }
  // CBOT — rates + grains.
  if (symbol == "ZB" || symbol == "ZN" || symbol == "ZF" || symbol == "ZT" ||
      symbol == "ZC" || symbol == "ZW" || symbol == "ZS" || symbol == "ZL" ||
      symbol == "ZM") {
    return "CBOT";
  }
  return "";
}

bool IsFuturesLocalSymbol(std::string_view s) {
  if (s.size() < 3) return false;  // root + month + year, minimum.
  // Trailing 1-2 digits (year).
  std::size_t i = s.size();
  std::size_t year_len = 0;
  while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])) &&
         year_len < 2) {
    --i;
    ++year_len;
  }
  if (year_len == 0) return false;
  // Month code: exactly one alpha char.
  if (i == 0) return false;
  if (!std::isalpha(static_cast<unsigned char>(s[i - 1]))) return false;
  --i;
  // Remaining root: 1+ alpha chars.
  if (i == 0) return false;
  for (std::size_t j = 0; j < i; ++j) {
    if (!std::isalpha(static_cast<unsigned char>(s[j]))) return false;
  }
  return true;
}

std::string_view RootFromFuturesLocalSymbol(std::string_view s) {
  if (!IsFuturesLocalSymbol(s)) return {};
  std::size_t i = s.size();
  std::size_t year_len = 0;
  while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])) &&
         year_len < 2) {
    --i;
    ++year_len;
  }
  // Drop the month code.
  if (i > 0) --i;
  return s.substr(0, i);
}

}  // namespace dlterm
