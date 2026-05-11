#include "redact.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace dlterm {

namespace {

bool ReadEnabledFromEnv() {
  const char* v = std::getenv("DLTERM_REDACT_ACCOUNTS");
  if (!v || !*v) return true;
  std::string s;
  s.reserve(8);
  for (const char* p = v; *p && s.size() < 8; ++p) {
    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
  }
  if (s == "0" || s == "false" || s == "no" || s == "off") return false;
  return true;
}

std::string_view Trim(std::string_view s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

}  // namespace

bool IsAccountRedactionEnabled() {
  static const bool enabled = ReadEnabledFromEnv();
  return enabled;
}

std::string RedactAccount(std::string_view account) {
  return std::string(account.size(), '*');
}

std::string RedactAccountList(std::string_view list) {
  std::string out;
  out.reserve(list.size());
  std::size_t i = 0;
  bool first = true;
  while (i <= list.size()) {
    const std::size_t comma = list.find(',', i);
    const std::size_t end = (comma == std::string_view::npos) ? list.size()
                                                              : comma;
    const std::string_view tok = Trim(list.substr(i, end - i));
    if (!tok.empty()) {
      if (!first) out.push_back(',');
      out.append(RedactAccount(tok));
      first = false;
    }
    if (comma == std::string_view::npos) break;
    i = comma + 1;
  }
  return out;
}

std::string MaybeRedactAccount(std::string_view account) {
  if (!IsAccountRedactionEnabled()) return std::string{account};
  return RedactAccount(account);
}

std::string MaybeRedactAccountList(std::string_view list) {
  if (!IsAccountRedactionEnabled()) return std::string{list};
  return RedactAccountList(list);
}

}  // namespace dlterm
