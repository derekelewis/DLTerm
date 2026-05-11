#include "redact.h"

#include <gtest/gtest.h>

namespace dlterm {
namespace {

TEST(RedactAccount, MasksEntireDemoAccount) {
  EXPECT_EQ(RedactAccount("DU1234567"), "*********");
}

TEST(RedactAccount, MasksEntireRealAccount) {
  EXPECT_EQ(RedactAccount("U1234567"), "********");
}

TEST(RedactAccount, PreservesLengthExactly) {
  const std::string in = "DU1234567";
  EXPECT_EQ(RedactAccount(in).size(), in.size());
}

TEST(RedactAccount, EmptyStaysEmpty) {
  EXPECT_EQ(RedactAccount(""), "");
}

TEST(RedactAccount, MasksShortInput) {
  EXPECT_EQ(RedactAccount("DU12"), "****");
}

TEST(RedactAccountList, RedactsEachEntryAndRejoinsWithComma) {
  EXPECT_EQ(RedactAccountList("DU1234567,U7654321"),
            "*********,********");
}

TEST(RedactAccountList, IgnoresWhitespaceAroundEntries) {
  EXPECT_EQ(RedactAccountList(" DU111 , U22222 "), "*****,******");
}

TEST(RedactAccountList, SkipsEmptyTokens) {
  EXPECT_EQ(RedactAccountList(",DU111,,"), "*****");
}

TEST(RedactAccountList, EmptyInput) {
  EXPECT_EQ(RedactAccountList(""), "");
}

}  // namespace
}  // namespace dlterm
