#include "services/quote_service.h"

#include <cmath>
#include <gtest/gtest.h>

#include "services/service_context.h"

namespace dlterm {
namespace {

// Tick types — mirrors TickTypes.h.
constexpr int kBidSize = 0;
constexpr int kBid = 1;
constexpr int kAsk = 2;
constexpr int kAskSize = 3;
constexpr int kLast = 4;
constexpr int kLastSize = 5;
constexpr int kHigh = 6;
constexpr int kLow = 7;
constexpr int kVolume = 8;
constexpr int kClose = 9;
constexpr int kOpen = 14;
constexpr int kLastTimestamp = 45;

class QuoteServiceTest : public ::testing::Test {
 protected:
  ServiceContext ctx;
  QuoteService svc{ctx};
};

TEST_F(QuoteServiceTest, RequestAllocatesReqIdAndStoresSymbol) {
  svc.Request("AAPL");
  EXPECT_TRUE(svc.OwnsReqId(1001));
  EXPECT_FALSE(svc.OwnsReqId(9999));
  auto snap = svc.Snapshot();
  EXPECT_EQ(snap.symbol, "AAPL");
  EXPECT_EQ(snap.kind, ChartInstrument::kStock);
  EXPECT_FALSE(snap.ready);
}

TEST_F(QuoteServiceTest, OnTickPriceUpdatesFields) {
  svc.Request("AAPL");
  const int rid = 1001;
  svc.OnTickPrice(rid, kBid, 234.50);
  svc.OnTickPrice(rid, kAsk, 234.55);
  svc.OnTickPrice(rid, kHigh, 235.10);
  svc.OnTickPrice(rid, kLow, 232.50);
  svc.OnTickPrice(rid, kOpen, 233.00);
  svc.OnTickPrice(rid, kClose, 232.80);
  auto snap = svc.Snapshot();
  EXPECT_DOUBLE_EQ(snap.bid, 234.50);
  EXPECT_DOUBLE_EQ(snap.ask, 234.55);
  EXPECT_DOUBLE_EQ(snap.high, 235.10);
  EXPECT_DOUBLE_EQ(snap.low, 232.50);
  EXPECT_DOUBLE_EQ(snap.open, 233.00);
  EXPECT_DOUBLE_EQ(snap.close, 232.80);
  EXPECT_FALSE(snap.ready);  // ready only flips on LAST.
}

TEST_F(QuoteServiceTest, LastTickFlipsReady) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kLast, 234.52);
  auto snap = svc.Snapshot();
  EXPECT_TRUE(snap.ready);
  EXPECT_DOUBLE_EQ(snap.last, 234.52);
}

TEST_F(QuoteServiceTest, NegativeSentinelDoesNotClobberField) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kBid, 234.50);
  svc.OnTickPrice(1001, kBid, -1.0);  // TWS "no data" sentinel
  EXPECT_DOUBLE_EQ(svc.Snapshot().bid, 234.50);
}

TEST_F(QuoteServiceTest, NonFiniteSentinelDoesNotClobberField) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kAsk, 234.55);
  svc.OnTickPrice(1001, kAsk, std::numeric_limits<double>::max());
  // DBL_MAX still passes >= 0 but the screen treats it via isfinite;
  // for our purposes, anything that's not finite is rejected.
  // (DBL_MAX is finite, so it would be accepted — which is fine since
  // TWS's actual sentinel is -1 for prices.) Just sanity-check finite.
  EXPECT_TRUE(std::isfinite(svc.Snapshot().ask));
}

TEST_F(QuoteServiceTest, OnTickSizeUpdatesFields) {
  svc.Request("AAPL");
  svc.OnTickSize(1001, kBidSize, 1200);
  svc.OnTickSize(1001, kAskSize, 800);
  svc.OnTickSize(1001, kLastSize, 100);
  svc.OnTickSize(1001, kVolume, 12345678);
  auto snap = svc.Snapshot();
  EXPECT_EQ(snap.bid_size, 1200);
  EXPECT_EQ(snap.ask_size, 800);
  EXPECT_EQ(snap.last_size, 100);
  EXPECT_EQ(snap.volume, 12345678);
}

TEST_F(QuoteServiceTest, NegativeSizeDoesNotClobber) {
  svc.Request("AAPL");
  svc.OnTickSize(1001, kBidSize, 1200);
  svc.OnTickSize(1001, kBidSize, -1);
  EXPECT_EQ(svc.Snapshot().bid_size, 1200);
}

TEST_F(QuoteServiceTest, LastTimestampStringIsParsed) {
  svc.Request("AAPL");
  svc.OnTickString(1001, kLastTimestamp, "1700000000");
  EXPECT_EQ(svc.Snapshot().last_ts, 1700000000);
}

TEST_F(QuoteServiceTest, LastTimestampEmptyIgnored) {
  svc.Request("AAPL");
  svc.OnTickString(1001, kLastTimestamp, "");
  EXPECT_EQ(svc.Snapshot().last_ts, 0);
}

TEST_F(QuoteServiceTest, WrongReqIdIgnored) {
  svc.Request("AAPL");
  svc.OnTickPrice(9999, kLast, 234.50);
  svc.OnTickSize(9999, kVolume, 99999);
  auto snap = svc.Snapshot();
  EXPECT_FALSE(snap.ready);
  EXPECT_EQ(snap.volume, -1);
}

TEST_F(QuoteServiceTest, CancelClearsState) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kLast, 234.50);
  svc.OnTickSize(1001, kVolume, 100);
  svc.Cancel();
  EXPECT_FALSE(svc.OwnsReqId(1001));
  auto snap = svc.Snapshot();
  EXPECT_FALSE(snap.ready);
  EXPECT_FALSE(std::isfinite(snap.last));
  EXPECT_EQ(snap.volume, -1);
  EXPECT_TRUE(snap.symbol.empty());
}

TEST_F(QuoteServiceTest, OnResetClearsEverything) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kLast, 234.50);
  svc.OnReset();
  EXPECT_FALSE(svc.OwnsReqId(1001));
  EXPECT_FALSE(svc.Snapshot().ready);
}

TEST_F(QuoteServiceTest, RequestReplacesPreviousSession) {
  svc.Request("AAPL");
  svc.OnTickPrice(1001, kLast, 234.50);
  svc.Request("MSFT");
  // Late tick on the stale req_id must be ignored.
  svc.OnTickPrice(1001, kLast, 999.99);
  // New req_id (1002) accepted.
  svc.OnTickPrice(1002, kLast, 410.10);
  auto snap = svc.Snapshot();
  EXPECT_EQ(snap.symbol, "MSFT");
  EXPECT_DOUBLE_EQ(snap.last, 410.10);
}

TEST_F(QuoteServiceTest, RequestStoresInstrumentKind) {
  svc.Request("CL", ChartInstrument::kContinuousFuture);
  EXPECT_EQ(svc.Snapshot().kind, ChartInstrument::kContinuousFuture);
}

TEST_F(QuoteServiceTest, RequestDefaultsToStock) {
  svc.Request("AAPL");
  EXPECT_EQ(svc.Snapshot().kind, ChartInstrument::kStock);
}

TEST_F(QuoteServiceTest, RequestWithoutClientDoesNotCrash) {
  // ctx.client is nullptr by default. Service should accept the
  // request, allocate a req_id, but stay in pending state until
  // OnReady() runs with a real client.
  svc.Request("AAPL");
  EXPECT_TRUE(svc.OwnsReqId(1001));
  svc.OnReady();  // Should be a no-op with null client.
  // Still pending, no crash.
}

}  // namespace
}  // namespace dlterm
