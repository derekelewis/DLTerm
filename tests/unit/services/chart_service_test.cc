#include "services/chart_service.h"

#include <gtest/gtest.h>

#include "Decimal.h"
#include "bar.h"
#include "services/service_context.h"

namespace dlterm {
namespace {

Bar MakeBar(const char* time_str, double open, double high, double low,
            double close) {
  Bar b;
  b.time = time_str;
  b.open = open;
  b.high = high;
  b.low = low;
  b.close = close;
  // Default-constructed Decimal evaluates to 0 via decimalToDouble.
  return b;
}

class ChartServiceTest : public ::testing::Test {
 protected:
  ServiceContext ctx;
  ChartService svc{ctx};
};

TEST_F(ChartServiceTest, RequestAllocatesReqIdAndOwnsIt) {
  svc.Request("MSFT");
  // ChartService::OwnsReqId queries against the active req id.
  EXPECT_TRUE(svc.OwnsReqId(1001));
  EXPECT_FALSE(svc.OwnsReqId(9999));
}

TEST_F(ChartServiceTest, OnHistoricalDataAppendsBarsForActiveReq) {
  svc.Request("MSFT");
  const int rid = 1001;
  svc.OnHistoricalData(rid, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalData(rid, MakeBar("1700000300", 104, 110, 102, 108));
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.bars.size(), 2u);
  EXPECT_DOUBLE_EQ(snap.bars[0].close, 104);
  EXPECT_DOUBLE_EQ(snap.bars[1].close, 108);
  EXPECT_FALSE(snap.ready);
}

TEST_F(ChartServiceTest, WrongReqIdIgnored) {
  svc.Request("MSFT");
  svc.OnHistoricalData(/*req_id=*/9999,
                        MakeBar("1700000000", 100, 105, 99, 104));
  EXPECT_TRUE(svc.Snapshot().bars.empty());
}

TEST_F(ChartServiceTest, OnHistoricalDataEndMarksReady) {
  svc.Request("MSFT");
  const int rid = 1001;
  svc.OnHistoricalData(rid, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalDataEnd(rid);
  EXPECT_TRUE(svc.Snapshot().ready);
}

TEST_F(ChartServiceTest, UpdateReplacesLastBarWhenSameTimestamp) {
  svc.Request("MSFT");
  const int rid = 1001;
  svc.OnHistoricalData(rid, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalDataEnd(rid);

  svc.OnHistoricalDataUpdate(rid, MakeBar("1700000000", 100, 107, 99, 106));
  auto snap = svc.Snapshot();
  ASSERT_EQ(snap.bars.size(), 1u);
  EXPECT_DOUBLE_EQ(snap.bars[0].high, 107);
  EXPECT_DOUBLE_EQ(snap.bars[0].close, 106);
}

TEST_F(ChartServiceTest, UpdateWithNewTimestampAppends) {
  svc.Request("MSFT");
  const int rid = 1001;
  svc.OnHistoricalData(rid, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalDataEnd(rid);
  svc.OnHistoricalDataUpdate(rid, MakeBar("1700000300", 104, 110, 102, 108));
  EXPECT_EQ(svc.Snapshot().bars.size(), 2u);
}

TEST_F(ChartServiceTest, CancelClearsState) {
  svc.Request("MSFT");
  svc.OnHistoricalData(1001, MakeBar("1700000000", 100, 105, 99, 104));
  svc.Cancel();
  EXPECT_FALSE(svc.OwnsReqId(1001));
  EXPECT_TRUE(svc.Snapshot().bars.empty());
}

TEST_F(ChartServiceTest, RequestReplacesPreviousSession) {
  svc.Request("MSFT");
  svc.OnHistoricalData(1001, MakeBar("1700000000", 100, 105, 99, 104));
  svc.Request("AAPL");  // bumps req_id to 1002
  // Old req_id no longer accepted.
  svc.OnHistoricalData(1001, MakeBar("1700000000", 200, 205, 199, 204));
  EXPECT_TRUE(svc.Snapshot().bars.empty());
  svc.OnHistoricalData(1002, MakeBar("1700000000", 200, 205, 199, 204));
  EXPECT_EQ(svc.Snapshot().bars.size(), 1u);
}

TEST_F(ChartServiceTest, OnResetClearsEverything) {
  svc.Request("MSFT");
  svc.OnHistoricalData(1001, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnReset();
  EXPECT_TRUE(svc.Snapshot().bars.empty());
  EXPECT_FALSE(svc.OwnsReqId(1001));
}

TEST_F(ChartServiceTest, RequestStoresInstrumentKindOnSnapshot) {
  svc.Request("CL", ChartInstrument::kContinuousFuture);
  EXPECT_EQ(svc.Snapshot().kind, ChartInstrument::kContinuousFuture);
}

TEST_F(ChartServiceTest, RequestDefaultsToStock) {
  svc.Request("MSFT");
  EXPECT_EQ(svc.Snapshot().kind, ChartInstrument::kStock);
}

TEST_F(ChartServiceTest, RefreshKeepsBarsUntilNewEnd) {
  // Initial load: 2 bars finalized.
  svc.Request("CL", ChartInstrument::kContinuousFuture);
  const int rid1 = 1001;
  svc.OnHistoricalData(rid1, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalData(rid1, MakeBar("1700000300", 104, 110, 102, 108));
  svc.OnHistoricalDataEnd(rid1);
  ASSERT_EQ(svc.Snapshot().bars.size(), 2u);

  // Refresh: ctx_.client is null in this test so the underlying
  // cancelHistoricalData/reqHistoricalData calls are no-ops, but
  // service-side state still advances. The new req_id is rid1+1.
  svc.Refresh();
  const int rid2 = 1002;

  // While the new batch streams in, the displayed bars MUST stay
  // intact — no flicker.
  svc.OnHistoricalData(rid2, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalData(rid2, MakeBar("1700000300", 104, 110, 102, 108));
  svc.OnHistoricalData(rid2, MakeBar("1700000600", 108, 112, 106, 111));
  EXPECT_EQ(svc.Snapshot().bars.size(), 2u);

  // historicalDataEnd swaps the new batch in atomically.
  svc.OnHistoricalDataEnd(rid2);
  EXPECT_EQ(svc.Snapshot().bars.size(), 3u);
}

TEST_F(ChartServiceTest, RefreshIgnoresStaleReqId) {
  svc.Request("CL", ChartInstrument::kContinuousFuture);
  svc.OnHistoricalData(1001, MakeBar("1700000000", 100, 105, 99, 104));
  svc.OnHistoricalDataEnd(1001);

  svc.Refresh();
  // Late-arriving bar from the prior request must be ignored.
  svc.OnHistoricalData(1001, MakeBar("1700001234", 999, 999, 999, 999));
  EXPECT_EQ(svc.Snapshot().bars.size(), 1u);
  EXPECT_DOUBLE_EQ(svc.Snapshot().bars[0].close, 104);
}

TEST_F(ChartServiceTest, RefreshWithoutActiveRequestIsNoOp) {
  svc.Refresh();  // Nothing happens, doesn't crash.
  EXPECT_TRUE(svc.Snapshot().bars.empty());
}

}  // namespace
}  // namespace dlterm
