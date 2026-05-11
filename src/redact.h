#ifndef DLTERM_SRC_REDACT_H_
#define DLTERM_SRC_REDACT_H_

#include <string>
#include <string_view>

namespace dlterm {

// Returns true when account-number redaction is on. Reads
// `DLTERM_REDACT_ACCOUNTS` once on first call; "0" / "false" / "no"
// (case-insensitive) disable; everything else (including unset) keeps
// redaction on.
bool IsAccountRedactionEnabled();

// Masks an IB-style account code with length-preserving asterisks:
//   "DU1234567" -> "*********"
//   "U123"      -> "****"
// Empty input is returned unchanged.
std::string RedactAccount(std::string_view account);

// Splits a comma-separated account list, redacts each entry, and
// rejoins with commas (no whitespace). Surrounding whitespace on
// entries is stripped.
std::string RedactAccountList(std::string_view list);

// Toggle-aware wrappers: return the input unchanged when redaction is
// disabled. Use these at call sites so log/render code doesn't have to
// branch on the toggle.
std::string MaybeRedactAccount(std::string_view account);
std::string MaybeRedactAccountList(std::string_view list);

}  // namespace dlterm

#endif  // DLTERM_SRC_REDACT_H_
