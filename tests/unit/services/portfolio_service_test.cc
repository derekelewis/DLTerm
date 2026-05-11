#include "services/portfolio_service.h"

#include <gtest/gtest.h>

#include <string>

#include "Contract.h"
#include "Decimal.h"
#include "services/service_context.h"

namespace dlterm {
namespace {

Contract MakePosContract(int con_id, const std::string& symbol) {
  Contract c;
  c.conId = con_id;
  c.symbol = symbol;
  c.secType = "STK";
  c.currency = "USD";
  c.exchange = "SMART";
  return c;
}

Decimal MakeDecimal(long long whole) {
  return DecimalFunctions::stringToDecimal(std::to_string(whole));
}

class PortfolioServiceTest : public ::testing::Test {
 protected:
  ServiceContext ctx;
  PortfolioService svc{ctx};
};

TEST_F(PortfolioServiceTest, SubscribeBeforeAccountsQueuesPending) {
  svc.Subscribe();
  // No accounts yet — Snapshot has no totals.
  auto snap = svc.Snapshot();
  EXPECT_TRUE(snap.accounts.empty());
  EXPECT_FALSE(snap.ready);
}

TEST_F(PortfolioServiceTest, ManagedAccountsTriggersStartAndAllocatesIds) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111,DU2222");
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.accounts.size(), 2u);
  EXPECT_EQ(snap.accounts[0], "DU1111");
  EXPECT_EQ(snap.accounts[1], "DU2222");
  // Snapshot starts non-ready until both accounts complete updates+positions.
  EXPECT_FALSE(snap.ready);
  // 3 ids per account allocated: acct, pos, pnl.
  EXPECT_TRUE(svc.OwnsReqId(1001));
  EXPECT_TRUE(svc.OwnsReqId(1006));
}

TEST_F(PortfolioServiceTest, OnAccountUpdateMultiOnlyAcceptsWhitelistedKeys) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111");
  svc.OnAccountUpdateMulti(1001, "DU1111", "NetLiquidation", "10000");
  svc.OnAccountUpdateMulti(1001, "DU1111", "GarbageKey", "999");
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.totals.size(), 1u);
  EXPECT_EQ(snap.totals[0].values.at("NetLiquidation"), "10000");
  EXPECT_FALSE(snap.totals[0].values.contains("GarbageKey"));
}

TEST_F(PortfolioServiceTest, OnPositionMultiAndPnLBuildSnapshot) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111");
  // Account update + end.
  svc.OnAccountUpdateMulti(1001, "DU1111", "NetLiquidation", "10000");
  svc.OnAccountUpdateMultiEnd(1001);
  // Position.
  svc.OnPositionMulti(1002, "DU1111", MakePosContract(265598, "AAPL"),
                       MakeDecimal(100), 150.0);
  svc.OnPositionMultiEnd(1002);
  // After both ends, snapshot is "ready".
  auto snap = svc.Snapshot();
  EXPECT_TRUE(snap.ready);
  ASSERT_EQ(snap.positions.size(), 1u);
  EXPECT_EQ(snap.positions[0].symbol, "AAPL");
  EXPECT_DOUBLE_EQ(snap.positions[0].position, 100.0);
  EXPECT_DOUBLE_EQ(snap.positions[0].avg_cost, 150.0);
  EXPECT_EQ(snap.positions[0].accounts, "DU1111");
}

TEST_F(PortfolioServiceTest, ZeroPositionRemoves) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111");
  svc.OnPositionMulti(1002, "DU1111", MakePosContract(265598, "AAPL"),
                       MakeDecimal(100), 150.0);
  svc.OnPositionMulti(1002, "DU1111", MakePosContract(265598, "AAPL"),
                       MakeDecimal(0), 150.0);
  EXPECT_TRUE(svc.Snapshot().positions.empty());
}

TEST_F(PortfolioServiceTest, MultiAccountPositionsAreAggregated) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111,DU2222");
  // Same conId in two accounts → merged.
  svc.OnPositionMulti(1002, "DU1111", MakePosContract(265598, "AAPL"),
                       MakeDecimal(50), 140.0);
  svc.OnPositionMulti(1005, "DU2222", MakePosContract(265598, "AAPL"),
                       MakeDecimal(50), 160.0);
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.positions.size(), 1u);
  EXPECT_DOUBLE_EQ(snap.positions[0].position, 100.0);
  // Weighted-avg cost = (50*140 + 50*160) / 100 = 150
  EXPECT_DOUBLE_EQ(snap.positions[0].avg_cost, 150.0);
  EXPECT_EQ(snap.positions[0].accounts, "DU1111,DU2222");
}

TEST_F(PortfolioServiceTest, PnLPopulatesAccountTotals) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111");
  svc.OnPnL(1003, /*daily=*/250.0, /*unrealized=*/1500.0, /*realized=*/0.0);
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.totals.size(), 1u);
  // std::to_string of 250.0 → "250.000000"
  EXPECT_NE(snap.totals[0].values.at("RT_DailyPnL").find("250"),
            std::string::npos);
}

TEST_F(PortfolioServiceTest, ResetClearsState) {
  svc.Subscribe();
  svc.OnManagedAccounts("DU1111");
  svc.OnPositionMulti(1002, "DU1111", MakePosContract(265598, "AAPL"),
                       MakeDecimal(100), 150.0);
  svc.OnReset();
  EXPECT_TRUE(svc.Snapshot().accounts.empty());
  EXPECT_TRUE(svc.Snapshot().positions.empty());
}

}  // namespace
}  // namespace dlterm
